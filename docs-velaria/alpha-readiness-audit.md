# Velaria alpha — readiness audit (30 Oct 2026, evening)

> Name: renamed from Kalyx to Velaria on 31 Oct 2026. Code, macro, driver, folder and Notion/Design names that still say Kalyx are left as they are until the rename lands in the build.


Soup to nuts: what a stranger with a Mac, Resolve Studio and a Quest 3 goes through from the zip to a placed card, where it breaks today, and what has to be true before the first build leaves Bruce. Written from this week's own breakage, not from a checklist. Severity: **blocker** (a tester stops), **hiccup** (a tester gets through with the guide), **polish**.

## The journey, step by step

### 0. Before they touch anything
| Point | Today | Severity | Fix |
|---|---|---|---|
| Hardware check | Nothing tells them Resolve *Studio* is required (the free one has no video I/O) until Velaria (Quest) isn't in the list. | hiccup | Say it in the invite and on the sign-up screener; the guide now says it first. |
| Blackmagic card owners | Installing sets their Desktop Video driver aside. They find out from a line in the installer output. | **blocker for anyone with a DeckLink** | Coexistence (in the launch plan). Until then: ask on the screener and tell them explicitly before they install; make the installer *refuse* unless `--replace-blackmagic` is passed when a real driver is present. |
| Expectations | "Full resolution" is true for VR180/≤8K, not for square 360 (3840²). | hiccup | Use the exact wording from the launch plan everywhere; the guide does. |

### 1. Headset developer mode
| Point | Today | Severity | Fix |
|---|---|---|---|
| Meta developer org | Meta changes this flow every few months; the Horizon app wording differs by region. | hiccup | Keep one screenshot-led page we can update without re-sending zips; the guide links the steps, not screenshots. |
| "Allow USB debugging" prompt | Only appears with the headset on, after plugging in; our installer runs before they've done that, so the headset step silently skips. | hiccup | Installer already prints the `adb install` fallback; add a `./install.sh --headset` that only does that step and waits up to 60 s for the prompt. |
| Unknown Sources | App appears under App Library → Unknown Sources only; testers look in the main library. | polish | Guide says it. Horizon Store listing later. |

### 2. Mac install
| Point | Today | Severity | Fix |
|---|---|---|---|
| Gatekeeper | Unsigned zip → every binary quarantined; `./install.sh` fails with "cannot be opened" or the driver fails to load silently. | **blocker** | Guide has `xattr -dr com.apple.quarantine .` as a required step. Real fix: Apple Developer ID + notarised .pkg (enrolment started per the launch plan). |
| Running from the wrong folder | `sudo ./install.sh` must be run inside the release folder (`HERE`); yesterday's "command not found" was exactly this plus a lost exec bit. | hiccup | package.sh sets exec bits; installer now checks for the framework next to it and says so. |
| Python | The bridge runs on `/usr/bin/python3`; on a clean Mac the first run of python3 pops Apple's "install command line developer tools" dialog. | **blocker on a clean Mac** | Installer must check `xcode-select -p` and tell them to install CLT first (or ship the bridge as a compiled binary — see "Second Mac" below). |
| adb | `adb` is needed for USB; Homebrew's `android-platform-tools` is not something a Resolve editor has. | hiccup | Bundle platform-tools in the zip (Apache-licensed) and use the bundled one. |
| Resolve open during install | The installer warns but continues; the new driver isn't seen until Resolve restarts, the Fusion library likewise. | hiccup | Keep the warning, add it to the last line of output in capitals. |
| Fusion library | New this evening in the installer; untested on a second machine. | needs test | Dry run on a clean user account on Bruce. |
| Never installed off Bruce | Every install so far has been on the dev machine. Paths, permissions, the keystore, Homebrew assumptions — none of it is proven elsewhere. | **blocker** | Clean install on a second Mac (or a fresh macOS user on Bruce as a first proxy) is gate 2's first task. |

### 3. Resolve configuration
| Point | Today | Severity | Fix |
|---|---|---|---|
| Video I/O | Must be selected by hand after a restart; no feedback in the headset that it hasn't been. | hiccup | Headset status already says "Waiting for a picture · Video I/O ▸ Velaria (Quest)"; keep. |
| Monitoring resolution / square 360 output scaling | Two project settings, one timeline setting, and the square-360 one must be undone before delivery. The single biggest "it looks wrong" generator. | **blocker-grade hiccup** | Short term: the guide and a one-paragraph card in the zip. Real fix: advertise a square DeckLink mode (launch-plan item) so nothing has to be set. Also: have the bridge *read* the project's monitoring settings and show a warning line in the headset ("Output scaling is 5760: set 3840 to monitor"). |
| Fusion library cache | Resolve reads Macros/Templates at launch; dropping a template after we updated the file can give the stale copy. | hiccup | Documented. Installer says "restart Resolve". |

### 4. First connection
| Point | Today | Severity | Fix |
|---|---|---|---|
| Source left on Adobe | The 30 Oct morning: "searching for Resolve" because Source was NDI. | hiccup | Fixed in v35 (named software, amber when forced, hint to Auto). |
| Wi-Fi discovery on guest/isolated networks | Devices can't see each other; status sits on Waiting to connect with no reason. | hiccup | Add a "Wi-Fi: looking for the Mac on <network name>" line and a USB suggestion after 20 s. |
| Headset black screen | Found tonight: a saved panel pose with a zero quaternion fails every frame (POSE_INVALID). Also the ANRs/stalls on doff/resume were downstream of the same failed frames. | **blocker** (was) | v40e/v41: saved poses validated, every layer checked before submit. Needs Monday's confirmation on the headset. |
| Restarts needed / stuck on loading | Same root cause as above as far as the logs show. | needs test | Soak over the weekend with the log collector; check `quest.log` for "dropping" or "ignoring" lines. |

### 5. Using it (placing things)
| Point | Today | Severity | Fix |
|---|---|---|---|
| Effects ▸ Velaria on a clip that already has a comp | Lands unwired (Resolve behaviour). Nelson hit it with Orient 360. | hiccup | Guide: drop on a fresh clip or wire on the Fusion page. Investigate a self-wiring template. |
| 3D Card in the wrong place | Today's black video (card without the plate composite) and the mirror slab (bad input IDs, lighting) — both fixed, but they were found by Nelson, not by a test. | process | Every macro change gets rendered on a scratch clip *before* it reaches the library (I now verify input IDs against `GetInputList()`; add a render check via a scripted scratch timeline when Resolve is free). |
| Window shows | Was a 0/1/2 slider; now a dropdown. | fixed | — |
| Model kit paths | The 3D Cards load FBX/textures from a volume path on Bruce ("The Baxter Building" for Vine, "Earth-1610" for Salmon). On any other Mac the kit is missing and the FBX loader fails the whole card. | **blocker for the 3D Cards** | First a licence check: `Vitis vinifera v2` looks like a purchased model, which usually can't be redistributed. If it can: ship the kits in the zip, installed to `/Library/Application Support/VRLink/kits` (absolute and identical on every Mac) and point the macros' default `ModelKit` there. If not: build 1 ships Canvas Control only and the Cards stay in-house, or get an own-made / CC0 kit. Either way the Card should survive a missing kit (model off, text and plate still render) — needs a Fusion test. |
| Fonts | 3D Titles use Montserrat ExtraBold Italic. Not on a clean Mac. | hiccup | Bundle the OFL font and tell the installer to put it in `~/Library/Fonts`, or switch the titles to a font Resolve ships. |
| Undo | Resolve's undo stack; works, but a tester who undoes in the headset then in Resolve can get confused about order. | polish | Guide explains it. |
| Latency | 0.4 s for one card, 1–2 s with stacked comps; fine for placement, slow for anyone expecting live. | expectation | State it. Fusion output cache on untouched clips is the real win (needs an A/B with Nelson). |

### 6. Getting help / getting out
| Point | Today | Severity | Fix |
|---|---|---|---|
| Logs | Driver, bridge, USB helper and NDI logs are in `~/Library/Logs/VRLink`; the headset log needs adb. Nobody will do this by hand. | hiccup | `send-logs.sh` in the zip: zips the Mac logs + `adb logcat -d` + the installed version, names it with the date, opens Finder on it. One command, in the guide. |
| Uninstall | `--uninstall` exists; never run on a machine that had a real Blackmagic driver. | needs test | Part of the second-Mac test, with a Desktop Video install first. |
| Version visibility | No "which version am I running" anywhere a tester can read it (the APK is 1.0.0 in the manifest; the driver has a build number in its plist). | hiccup | Show the build in the View panel's diagnostics line and in `install.sh` output; bump the manifest with each package. |

## Second-Mac test — done 30 Oct 19:24–19:30 on NYC

NYC: macOS 26.3.1, Apple silicon, DaVinci Resolve 21.1.0, Command Line Tools present, Apple Python 3.9.6, no adb, no Blackmagic driver — a clean machine. The 1.0.7 zip went over by scp and was given a Safari-style quarantine attribute on every file to mimic a download.

What happened, in tester order:
1. `sudo ./install.sh` on the still-quarantined folder **ran and succeeded** (exit 0) with no `xattr` step: sudo doesn't go through Gatekeeper for a shell script, and the installer clears quarantine on the driver itself. The guide's `xattr` line is gone.
2. Driver: Resolve enumerated **"Velaria (Quest)" v12.8.0** as its display device on first relaunch (Resolve log, `Decklink model name`). The bridge connected to Resolve on its own (19:26:03). The Fusion library went in (Resolve logged the Effects/Velaria folder).
3. **Bug found:** a macOS "cannot be opened because the developer cannot be verified" dialog appeared a minute later. Cause: the installer copied `vrlink_ndi` with its quarantine flag and launchd kept relaunching it every 30 s (no NDI runtime on NYC → exit 1 → retry), each retry tripping Gatekeeper. Fixed the same evening: the installer clears quarantine on every file it places (bridge, helpers, Fusion library) and only starts the NDI agent when NDI Tools is present (`--ndi` to force). Re-installed with the fix: no dialog, no NDI agent, no quarantine on the helpers.
4. `--uninstall` removed the driver, the agents and the helpers cleanly (`config.plist` is left on purpose).
5. Not tested on NYC: the headset app (no adb, no Quest there), Video I/O selection in the UI (Resolve was open during the install; the model name in the log is the proof), and a Mac with Blackmagic Desktop Video present.

Verdict: the Mac side of the install is real on a second machine. Remaining install risks are the headset half (adb, developer mode) and the Blackmagic-present case.

## What must be in place before build 1 leaves (in order)

1. ~~Clean install on a machine that isn't Bruce~~ — **done on NYC 30 Oct** (see above). Still to do on a second machine: the headset half with a Quest plugged in, and the Blackmagic-present path.
2. ~~The 3D Card kits travel with the zip~~ — **settled 2 Nov: the Vine / Salmon / Hay Bale cards are Skylight-internal and are not in the package** (`package.sh` skips every 3D Title / 3D Card). Canvas Control's 3D Scene input is the alpha's 3D path; chooseable 3D card templates come with beta/launch. (Kit sizes for the record: Vine 22 MB, Salmon 131 MB, Hay Bale 167 MB; a missing kit fails the export outright, proven 31 Oct on NYC.) ~~Montserrat~~ — in the package from 1.0.9, installed by `install.sh`; without it every title/card export fails (regression doc).
3. **Headset stability confirmed** — **the two causes found and fixed 30 Oct evening** (1.0.7+43/+44, both verified with Nelson in the headset: black screen after bridge connect = non-unit quaternions from the five-decimal layout file; loading-screen hang after a quit = relaunch inside the finished process). Still wanted: a week of Nelson's own use on 1.0.8 with the collector running; any `xrEndFrame failed` line gets looked at before shipping.
4. ~~Installer guards~~ — **done** (30–31 Oct): refuses on a present Blackmagic driver without `--replace-blackmagic` (logic untested: needs a Blackmagic Mac); checks the Command Line Tools; prints the version; `--headset` step; **adb bundled** (Google platform-tools 37.0.1, universal, in `platform-tools/` with its NOTICE, placed in Application Support on install; the system's own adb wins when present). **Soaked 31 Oct night on NYC: 10/10 uninstall → install cycles clean after fixing a `set -e` exit that had 1.0.8 skipping the Fusion library** (`docs/install-soak.md`).
5. **send-logs.sh** — in the package since 1.0.7; finds the bundled adb from 1.0.8; untested on a clean Mac (task I covers the Mac half).
6. ~~The square-360 warning in the headset~~ — **done 31 Oct as a preflight** (bridge 1.0.8, app 1.0.8+47): square/oversize timeline without output scaling (from Resolve's settings), monitoring raster smaller than the timeline, monitoring frame rate ≠ timeline, and no picture after the bridge connects (from what the driver is actually fed — `videoMonitorFormat` cannot be trusted: it read "HD 1080p 24" on Bruce while the driver received 7680×4320 at 29.97). Shown in the strip's status line with the fix in Resolve's menu words; verified on NYC by flipping the settings through scripting. The square DeckLink mode stays on the roadmap.
7. ~~Version stamping~~ — **done 31 Oct** (1.0.8): the bridge answers `hello` with its version, the installed package VERSION, Resolve's version and the project; the View tab's bottom line reads e.g. `Kalyx View 1.0.8+46 · Mac 1.0.8 · Resolve 21.1.0`. The driver has no version of its own beyond the package's (it reports Desktop Video 12.8 to Resolve by design).
8. **Docs set** (below), published as pages, not PDFs, so they can be fixed between builds.
9. **One frozen build** that Nelson has run end to end on a real project — install from the zip, not from the dev folders.

Nice-to-have before build 1 but not blocking: DeckLink coexistence; Premiere at 5760²; self-wiring Edit-page templates; Fusion output cache.

## Macro library — render regression (31 Oct, NYC)

Canvas Control and Orient 360 export cleanly on a second Mac. Every 3D Title / Card failed there for two reasons we own (font not installed; one model path absolute), both fixed in source and library; confirming pass pending a human relaunch of Resolve on NYC. Full table: `docs/macro-regression.md`.

## Docs set for the alpha

- **Setup guide** (written tonight, `docs/alpha-setup-guide.md`): requirements → developer mode → Mac install → Resolve settings → first picture → uninstall → logs.
- **Quick card** (one screen, in the zip as `READ-ME-FIRST.md`): the five things that go wrong and the one-line fix for each.
- **How-to: place a footage window** (Canvas Control): drop, wire, Window shows, crop, glow, stereo. Screenshots from the Edit page Inspector + one headset capture.
- **How-to: 3D titles** (3D Cards): drop on the plate, text, model, animation, Environment, fade.
- **How-to: patches and re-orientation**: the Patches layer, lock, Orient 360 at the end of the chain, the level grid.
- **How-to: Premiere / After Effects** via NDI Tools (monitor only).
- **Headset reference**: the strip, the four panels, every control's gesture, the View tab settings — generated from the same preview renderer so the pictures are always the current UI.
- **Known limits + roadmap** (the public roadmap page already exists).
- **Walkthrough video** (notes exist) — the single most effective doc; do it after build 1 is frozen so it shows the shipped UI.

Format: Markdown in the repo, rendered to a small docs site (same design tokens as the website) and linked from the alpha email; the setup guide also ships inside the zip as `SETUP.md`.
