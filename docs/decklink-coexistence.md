# DeckLink coexistence: design

Written 13 Nov 2026 on Linux. **Everything described here is UNTESTED**: the code has never been compiled for macOS, never loaded by Resolve, never run beside Blackmagic Desktop Video and never seen a real card. What has been tested is listed in section 11. Internal document; it does not ship.

## 1. The goal, and where we are today

Today Velaria's `DeckLinkAPI.framework` sits at `/Library/Frameworks/DeckLinkAPI.framework`, the one place every DeckLink client looks. If Blackmagic's Desktop Video is installed, the installer refuses unless `--replace-blackmagic` is passed, and then moves Blackmagic's framework to `DeckLinkAPI.framework.blackmagic-backup`. A DeckLink or UltraStudio owner has Velaria or their card, not both.

The goal: Resolve's Video I/O list shows **Velaria (Quest)** and every real Blackmagic device, and switching is Resolve's own menu. No reinstall, no second copy of Resolve's settings.

## 2. How a client reaches the driver (what we build on)

Client apps do not link against the framework. They compile Blackmagic's `DeckLinkAPIDispatch.cpp`, which:

1. creates a `CFBundle` for the fixed path `/Library/Frameworks/DeckLinkAPI.framework`;
2. looks up creator functions by name with `CFBundleGetFunctionPointerForName`;
3. calls them, and from then on talks to the returned objects through COM-style interfaces (`IUnknown` plus virtual methods).

Checked on 13 Nov against a public copy of the SDK 10.11.2 dispatch file: it looks up `CreateDeckLinkIteratorInstance_0003`, `CreateDeckLinkDiscoveryInstance_0002`, `CreateDeckLinkAPIInformationInstance_0001`, `CreateOpenGLScreenPreviewHelper_0001`, `CreateCocoaScreenPreview_0001`, `CreateVideoConversionInstance_0001` and `CreateVideoFrameAncillaryPacketsInstance_0001`. Newer SDKs add `_0004` (iterator), `_0003` (discovery) and `CreateOpenGL3ScreenPreviewHelper_0001`; our driver already exported all of these. The numeric suffix is Blackmagic's ABI version for that entry point: when an interface changes shape they add a new suffix and keep the old one.

Two consequences:

- Whoever owns that path answers for every DeckLink device on the Mac. Coexistence has to happen **inside our framework**.
- The surface we must merge is small: a handful of C functions and three tiny interfaces. Device objects themselves never need to pass through us.

## 3. The design in one paragraph

`install.sh --coexist` moves Blackmagic's framework, whole and unmodified, to `/Library/Application Support/Velaria/Blackmagic/DeckLinkAPI.framework`. Our framework, on the first creator call in a process, loads that bundle the same way a client would (`CFBundleCreate`, then `CFBundleLoadExecutableAndReturnError`) and keeps it for the life of the process. Each of our creator functions then answers for both: device enumeration is merged, everything else is passed straight through. Blackmagic's device objects go to the client **untouched**; we never wrap them, so their interfaces, reference counts, threads and private extensions behave as if Velaria were not there. If nothing is set aside, or loading fails, the driver behaves exactly as it does today.

Files: `mac/driver/src/Coexist.h` (contract), `Chain.cpp` (merged objects, portable), `CoexistLoader.cpp` (the CFBundle loader, macOS only), `Exports.cpp` (entry points).

## 4. How the shim finds and loads the original

- **Where:** one fixed path, compiled in (`kUnderlyingPath` in `Coexist.h`, `UNDER` in `install.sh`; change both or neither). It is deliberately not configurable by environment variable or by the user's `config.plist`: an override would let any process running as the user pick a library for Resolve to load.
- **Why not leave it in `/Library/Frameworks` under another name** (as `--replace-blackmagic` does): Blackmagic's code may find its own bundle by walking up from its executable or by identifier. A folder that is still called `DeckLinkAPI.framework`, in a different parent, is the least surprising thing we can give it. This is a guess about their internals, and test A0 looks for evidence either way.
- **Checks before loading:** the folder must be owned by root and not writable by group or others (the installer sets this); its bundle identifier must not be ours (loading a copy of ourselves underneath would forward every call back into the shim).
- **When:** lazily, on the first creator call, once per process (`std::call_once`). Not in a load-time constructor.
- **Never unloaded.** The bundle reference is kept until the process exits. Blackmagic's framework starts threads and talks to its driver; nothing suggests it is safe to unload.
- **Off switches:** `Coexist` = false in `~/Library/Application Support/Velaria/config.plist`, or `VELARIA_COEXIST=0` in the environment. Either makes the driver Velaria-only without reinstalling. They can only turn chaining off.

## 5. Entry points and interfaces: what is forwarded, what is merged

| Entry point | With Blackmagic loaded | Without (same as today) |
|---|---|---|
| `CreateDeckLinkIteratorInstance_0002/3/4` | **Merged.** Our iterator yields the Velaria device first, then calls `Next` on Blackmagic's iterator (made with the same suffix the client asked for) until it ends. Blackmagic's device pointers are returned as they come. | Velaria device, then end of list. |
| `CreateDeckLinkDiscoveryInstance_0001/2/3` | **Merged.** `InstallDeviceNotifications(cb)` announces the Velaria device, then installs the **client's own callback** on Blackmagic's discovery object. Arrivals and removals of real hardware reach the client directly, on Blackmagic's thread. Uninstall goes to Blackmagic first, then drops our reference. | Announces the Velaria device. |
| `CreateDeckLinkAPIInformationInstance_0001`, and `QueryInterface(IID_IDeckLinkAPIInformation)` on an iterator | **Forwarded.** The client gets Blackmagic's object and reads the real Desktop Video version. `VRLINK_API_VERSION` still forces Velaria's answer. | Velaria's object, reporting 12.8. |
| `CreateVideoConversionInstance_0001`, `CreateVideoFrameAncillaryPacketsInstance_0001`, `CreateOpenGLScreenPreviewHelper_0001`, `CreateOpenGL3ScreenPreviewHelper_0001`, `CreateCocoaScreenPreview_0001` | **Forwarded.** Blackmagic's object, untouched. | `nullptr`, as today. |

Design choices worth a second look in review:

- **No wrapper around the notification callback.** A relay object would let us log arrivals, but it would also sit between Blackmagic and the client on every hot-plug, and would have to answer `QueryInterface` for callback generations we do not know. Passing the client's callback through is less code and cannot change behaviour. Diagnosis comes from a device census at load instead (section 9).
- **No fallback between suffixes.** If the client asks for `_0004` and Blackmagic's framework does not export it, that request is Velaria-only and the log says so. Handing a `_0003` object to a client that asked for `_0004` would be an ABI mismatch.
- **Velaria is listed first.** That is today's order for existing users. Whether Resolve treats the first device as a default is unknown; test B2 records it.
- **API version comes from Blackmagic when it is loaded.** A card owner then sees exactly what they saw before installing Velaria. The cost: Resolve may read a newer version (16.x) than our virtual device was written against (12.8). Resolve chooses interfaces by IID at compile time, not by this number, as far as our logs show, so it should not matter. If test A6 shows otherwise, set `VRLINK_API_VERSION` and file it.
- **The forwarded helpers change one thing for Velaria-only playback when Blackmagic is loaded:** Resolve can now get a real video-conversion or ancillary-packets object where it used to get `nullptr`, and may use it on frames headed for our device. That is the state a Resolve user with Desktop Video has always been in, but our device has not been run that way. Test A6 covers it.

**Not exported by us, and therefore not forwarded:** any creator Blackmagic's current framework exports that is not in the table. The SDK 12.8 header declares `CreateMetalScreenPreviewHelper`, so a suffixed export for it very likely exists; I did not add one because I could not confirm its exact name. Blackmagic's own apps (Desktop Video Setup, Media Express, the firmware updater) may also use private exports. Test A0 lists every export of the real framework so missing forwarders are added before anything else is judged.

**The calls we make into Blackmagic's framework** are limited to: the suffixed creator functions (C, no arguments, one takes a `void*`), `IUnknown`, `IDeckLinkIterator::Next`, `IDeckLinkDiscovery::InstallDeviceNotifications` / `UninstallDeviceNotifications`, and for the log only `IDeckLinkAPIInformation::GetInt`, `IDeckLink::GetModelName` and `GetDisplayName`.

## 6. Reference counting and lifetime

- Our iterator and discovery objects each hold one reference on their Blackmagic counterpart and release it in their destructor.
- A Blackmagic device returned by `Next` carries Blackmagic's own +1, which now belongs to the client. We add and remove nothing.
- The Velaria device is a process-wide singleton that is never destroyed; each `Next` adds the client's reference, as today.
- The client's notification callback is held once by us (released on uninstall or in the destructor) and separately by Blackmagic under its own rules.
- A discovery object released without `UninstallDeviceNotifications` uninstalls from Blackmagic in its destructor, so Blackmagic is never left holding a callback through a dead object of ours. (It holds the client's callback, not ours, so even the failure case would not call into freed Velaria memory.)
- Blackmagic's uninstall is called outside our lock: it may wait for a running callback, and that callback is client code that may call back into us.
- **Re-entrancy guard.** If some Blackmagic component, loaded underneath, itself opens `/Library/Frameworks/DeckLinkAPI.framework`, it gets us, and we would forward to Blackmagic, which could ask us again without end. Calls into Blackmagic's creator functions are marked per thread; a nested creator request on the same thread is refused with an error line in the log. Only creator calls are marked. `Next`, `Install` and `QueryInterface` are not, because the client's own code can run inside those and may legitimately ask us for more objects.
- Known small race: if a client calls `UninstallDeviceNotifications` from another thread while `InstallDeviceNotifications` is still inside Blackmagic's install, the uninstall can miss Blackmagic's side. The destructor then cleans it up. Real clients install once at start-up.

## 7. Version mismatches between Desktop Video releases

Our headers are SDK 12.8. Desktop Video on a customer's Mac can be anything; the current release is 16.5 (30 Oct 2026).

- **Why this is mostly safe:** we do not interpret Blackmagic's device interfaces at all. The only layouts we rely on are the four listed in section 5, which are the oldest and most stable in the SDK, and each creator is pinned by its suffix.
- **What can still go wrong:**
  1. A future Desktop Video stops exporting an old suffix. Then clients built against that generation lose Blackmagic devices **with or without Velaria**; we log `Blackmagic does not export: …` at load.
  2. A future Desktop Video adds a creator that a new Resolve needs. Without a forwarder in our framework that feature is missing while Velaria is installed. The same log line family (`a client asked for …`) cannot catch it, because the client's lookup fails inside CFBundle before reaching our code. Only test A0's export listing catches it. **This is the standing maintenance cost of coexistence:** re-run A0 against each new Desktop Video release.
  3. The set-aside framework is older or newer than the Blackmagic driver actually running (section 8).
- The load log records Blackmagic's bundle version, its API version, and which known creators it exports, so a support zip shows the pairing.

## 8. When Desktop Video updates and overwrites the framework

Blackmagic's installer writes its framework to `/Library/Frameworks/DeckLinkAPI.framework`. That replaces ours. Result: the card works, **Velaria (Quest) disappears from Resolve**, and the copy we set aside is now stale: it belongs to the previous Desktop Video, while the driver extension is the new one.

Handling, all in `install.sh`:

- `./install.sh --status` reports this state in words ("Desktop Video was installed or updated after Velaria; run the Velaria installer again").
- A plain `sudo ./install.sh` refuses and names `--coexist`.
- `sudo ./install.sh --coexist` treats the framework now in place as the current one: the stale copy is kept once as `…/DeckLinkAPI.framework.previous`, the new one is set aside, Velaria goes back in.
- `--uninstall` in this state leaves the new framework alone and deletes the stale copy.

Not handled, and stated as limits:

- Nothing watches for the overwrite. The customer finds out when Velaria is missing from Resolve. A launch daemon watching the path could re-run the swap, but silently re-inserting ourselves after a vendor's update is the wrong default; a line in the headset or the bridge log ("Velaria's driver was replaced by a Desktop Video update") is the better follow-up.
- If Blackmagic's installer merges into our folder rather than replacing it, leftover files of ours could remain inside their framework. Our framework contains only names theirs also has, so this should be harmless; test A9 checks the result with `codesign --verify`.
- **Uninstalling Desktop Video** while Velaria is in coexistence leaves our framework in place and their driver gone. Blackmagic's set-aside framework then loads but reports no devices or fails; Velaria should continue alone. Untested (test A10b).

## 9. Logging for the first real test

Everything goes to `~/Library/Logs/Velaria/driver.log`; `grep coexist: driver.log` gives the story of one process:

- why chaining is off, when it is (`nothing set aside`, `Coexist=false`, `not root-owned`, `is Velaria's own`);
- on success: bundle identifier, version, path, host process name and pid;
- `Blackmagic exports:` and `Blackmagic does not export:` for the twelve known creators;
- Blackmagic's API version next to the SDK version we are built against;
- a **device census**: every device Blackmagic's framework reports at load, by model and display name, and the count. This separates "loaded, no hardware" from "loaded, card seen" from "did not load";
- per call: which creator was asked for and whether the answer was `Velaria + Blackmagic` or `Velaria only`; each Blackmagic device handed out by an iterator; the result of installing on Blackmagic's discovery; any non-success code from Blackmagic.

`send-logs.sh` now includes `install.sh --status`.

One cost: the census runs an extra enumeration of Blackmagic's devices once per process. Every DeckLink app does the same at start-up, so the risk is low, but it is a call the unmodified system would not make. If it ever misbehaves it is one function to remove (`census` in `CoexistLoader.cpp`).

## 10. Install, upgrade, uninstall and rollback sequences

Default behaviour is unchanged: no flag refuses on a Blackmagic framework; `--replace-blackmagic` sets it aside at the old backup path.

| Situation | Command | What happens |
|---|---|---|
| Desktop Video present, Velaria not | `--coexist` | Blackmagic's framework moves (one rename, same volume) to the set-aside path; ownership and write bits normalised; signature checked (warning only); Velaria's framework installed. |
| Earlier `--replace-blackmagic` install | `--coexist` | The old backup moves to the set-aside path. |
| Coexistence install, new Velaria version | plain or `--coexist` | Velaria's framework replaced; set-aside copy kept. |
| Desktop Video updated over a coexistence install | `--coexist` | New framework adopted; older copy kept once as `.previous`. |
| No Desktop Video | `--coexist` | Velaria alone, with a note to run it again if Desktop Video is installed later. |
| Both set-aside locations populated | any install | Refuses; asks for `--uninstall` first. |
| Any | `--uninstall` | Velaria's framework removed; Blackmagic's restored from whichever location holds it, unless a Blackmagic framework is already in place, in which case that one stays. |

`--uninstall` also fixes an existing bug on the old path: when Desktop Video had been reinstalled over a `--replace-blackmagic` install, `mv` put the old backup **inside** the new framework folder.

The window in which no framework exists at the public path is the time between the rename and `ditto` finishing, a second or two. Resolve should be closed; the installer already says so.

**Rollback** is in the test plan (`docs/decklink-coexistence-test-plan.md`, last section): `--uninstall`, then two manual commands, then reinstalling Desktop Video as the last resort.

## 11. What has and has not been tested

Tested on Linux, 13 Nov 2026:

- `tests/chain_test`: 43 checks of the merge logic and reference counting against a fake Blackmagic framework (order, untouched pointers, error paths, install and uninstall order, the recursion guard, the API-version override). Passes with g++ under AddressSanitizer and UBSan, and with clang++.
- `Exports.cpp` and `CoexistLoader.cpp` pass a syntax-only compile against hand-written CoreFoundation declarations. That catches typos, not behaviour.
- `mac/install/tests/dryrun.sh`: 35 checks (16 new) of where the installer puts things, with macOS commands stubbed.

Not tested, at all: compiling on macOS; CFBundle loading of Blackmagic's framework from the set-aside path; whether Blackmagic's framework works from a different folder; Resolve; Desktop Video Setup; a real device; hot-plug; a Desktop Video update.

## 12. Risks, ranked

1. **Blackmagic's framework may not work from another folder.** If it hard-codes its own path, depends on sibling files by absolute path, or is checked by its driver for location, coexistence as designed fails and needs a different approach (for example, leaving theirs in place under its own name inside `/Library/Frameworks` and shipping ours as a thin stub; or an Info.plist-level redirect). Test A0 and A5 answer this within the first half hour. **This is the risk that could sink the design.**
2. **Blackmagic's own apps may need exports we do not forward.** Desktop Video Setup failing to see a card, or a firmware update failing, while Velaria is installed would be worse for a card owner than today's honest either/or. Tests A0, A7 and B7.
3. **Licence.** We now move and load Blackmagic's binary as part of our product, on top of building against their SDK headers. The SDK and Desktop Video licence terms have still not been read (session F's open item). This goes to the lawyer with the EULA.
4. **Every DeckLink app on the Mac gets the Velaria device,** not just Resolve: Media Express, OBS, Premiere's Blackmagic plug-in, Desktop Video Setup. That is true today in replace mode too, but coexistence invites people who use those apps. If one of them opens the Velaria device, a second stream server starts in that process. Option held back until stage (b) shows whether it matters: announce Velaria only when the host is Resolve.
5. **Desktop Video updates silently remove Velaria** (section 8). Support load, not data loss.
6. **Maintenance per Desktop Video release** (section 7, point 2).
7. **The refactor touches the default path.** `Exports.cpp` was rewritten to go through the new layer even when nothing is set aside. The behaviour is meant to be identical and the unit test covers it, but it has not been compiled on a Mac. If 1.0 must ship before this is proven, revert commit `a2b6f9a` (driver) and keep the installer commit, whose default path is unchanged.
8. **Unsigned code loading a signed vendor library into Resolve.** Resolve already loads our ad-hoc-signed framework, so library validation is evidently not blocking us; loading Blackmagic's signed framework from another path should be no harder. Untested on current macOS.
