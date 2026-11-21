# Velaria install soak (NYC, 31 Oct 2026 22:00–22:30 ET)

Package: `VR-Link-1.0.8.zip` as built Saturday 02:06, unpacked over `~/kalyx-test` on NYC (macOS 26.3.1, Resolve 21.1.0.17, no adb, no Blackmagic, Montserrat already in `~/Library/Fonts` from the morning's regression work). Installer run through the sudoers grant (`~/kalyx-test/install.sh` only). Scripts: `~/kalyx-test/soak-lite.sh` (the loop), `soak.sh` (the loop with a Resolve relaunch per iteration, see below).

## Bug found on iteration 1, fixed, package re-zipped

**`sudo ./install.sh` exited 1 right after "Installed adb…"**, before the USB helper, the fonts and the **Fusion library**. Cause: `find_adb()` ended with a bare `[ -z "$ADB" ] && [ -x … ] && ADB=…` line; once adb is already set that list returns 1, the function returns 1, and `set -e` kills the script. 1.0.7 never hit it on NYC (no adb, so the whole block was skipped) and Bruce has never installed from a zip. With 1.0.8 every tester would have got a driver and a bridge and **no Velaria effects in Resolve**. Fix: the function ends with `return 0` and uses `if` lines. Also: `VERSION` and `config.plist` in Application Support kept their download quarantine; cleared now. The fixed installer is in `mac/install/install.sh`, in `~/VRLink/release/VR-Link-1.0.8/` and the re-zipped `VR-Link-1.0.8.zip` (22:04).

## Ten cycles, uninstall → install, Resolve left alone

| iter | uninstall rc / s | leftovers after uninstall | install rc / s | quarantined files | agents bridge / usb / ndi | adb | fonts | Edit-page effects | driver build |
|---|---|---|---|---|---|---|---|---|---|
| 1–10 | 0 / 0–1 | none | 0 / 0–1 | 0 | 1 / 1 / 0 | 37.0.1 | 2 (pre-existing) | 5 | 20261031-020633 |

Identical on all ten. "Leftovers" checks the framework, bridge, platform-tools, VERSION, helpers, the three LaunchAgents (loaded or plist present). `config.plist` is left by design; the fonts are not removed by `--uninstall` (they are the user's fonts now; fine, but say so in the guide).

## One Resolve relaunch after a fresh install

`open -a "DaVinci Resolve"` at 22:28:18 → `Decklink model name: 'Velaria (Quest)', version: '12.8.0'` at 22:28:20 (2 s) → script server at 22:28:28 → `pm.LoadProject("KALYX TEST")` → bridge.out `connected to Resolve: KALYX TEST / Kalyx regression` at 22:29:10. Resolve left running with KALYX TEST loaded.

## What could not be soaked

- **Resolve in the loop.** The intended loop relaunched Resolve every iteration. Three attempts, three different rig failures, none of them the installer's: a stale `rq.py` client left attached to Resolve's script server made every later API call hang (the server serves one client; kill strays first); an API `Quit()` that hung Resolve in its own terminate sequence for 10+ minutes; and after a `kill -9`, a relaunch that answered API queries but refused `LoadProject` and created an "Untitled Project". Headless Resolve lifecycle on NYC is not dependable enough to put inside a loop; one relaunch at the end is.
- **`--replace-blackmagic` refusal path.** Needs a non-Velaria `DeckLinkAPI.framework` in `/Library/Frameworks`, which takes root beyond the sudoers grant (`install.sh`, `xattr`, `pmset`). Untested; the logic is six lines and reads right, but it needs a real Blackmagic Mac or a wider sudo on NYC.
- Headset half (no Quest on NYC).

## Rig notes for next time

- Never leave an `rq.py` attached to Resolve; kill strays before any API call.
- Quit Resolve through the API only when it is idle at a project; otherwise AppleScript quit, and expect a recovery dialog after any kill.
- Resolve appends to `davinci_resolve.log` (rotates at 2 MB, archives into `LogArchive` on some launches); detect new lines by byte offset with a reset when the file shrinks, not by inode.

## 1.1.0-rc1 — hardware-free tests and the build Mac (20 Nov 2026, evening)

Run from the branch tip (`d773621` and after) on the build Mac; nothing installed by sudo (the driver and plugin swap waits for Monday, `mac/install/dev/monday-swap.sh`).

| Suite | Where | Result |
|---|---|---|
| Bridge `pytest mac/bridge/tests` | Linux + build Mac | **51 passed** (output-scaling transactional/Deliver/grace/None cases, folding and labels, driver notice) |
| Driver `tests/tap_test.sh` | Linux + build Mac | passed (header validation, NaN, ring invalidation, insert selection) |
| Driver `build/selftest` against the fresh framework | build Mac | **PASSED: 0 failure(s)** |
| Driver `chain_test`, `server_test`, `area_test` | build Mac | passed (in `build.sh`) |
| Velaria Sound: VST3 validator | build Mac | **47 tests passed, 0 failed**; universal (`lipo -archs`: x86_64 arm64) |
| Velaria Sound `tap_test` (null buffers, pass-through, log worker) | build Mac | all checks passed |
| Headset `hrirtest` | Linux | kernel sane, 1 s decoded in 21 ms |
| `hudpreview` renders of every panel | Linux | regenerated into `quest/host/ref/` (+87 layout) |
| Headset `orbstest` (sound-orb peak tracker) | Linux | 16 checks passed (one source within 0.5°, 150° pair → two, 30° pair → one, all-W and diffuse → none, circling tone lag 4.8°, fade in/out) |
| Title templates `s_titles.py` (6 comps × in/hold/out frames) | Bruce, Resolve | all PASS; right eye 12 px left on the 3840 chart |
| Bridge `pytest` | Linux | **53 passed** (template labels, R G B swatch order) |
| Headset `striptest` (strip pill rules, drop window) | Linux | 13 checks passed (pill within 1.0 s of drops at 3/s, clear within 1.0 s, nothing while paused) |
| Headset `orbsview` (orb shader, Mesa) | Linux | compiles and links under GLES 3; stereo pair rendered, disparity as 2.5 m at 64 mm |
| rc1 APK | Bruce | repackaged 09:23 Sat with **+94**, bridge 1.0.16 and the four title templates (clean start on the Quest, no shader errors) |
| `package.sh 1.1.0-rc1` with `VELARIA_VST3_LICENSED=1` | build Mac | built, 52 MB, `~/VRLink/release/Velaria-1.1.0-rc1/` |
| `check_release.sh` | build Mac | **clean** apart from the legal placeholders ([LEGAL ENTITY NAME], [MAILING ADDRESS], [SUPPORT EMAIL], the [VERIFY] items in THIRD-PARTY-NOTICES) — "do not sell this build" until those are filled |

Not run tonight: `tests/e2e.sh` (needs the UDP/TCP ports the installed driver inside Resolve holds), the NYC install soak (Resolve on NYC not running; no relaunch unattended), the macro regression rig, and anything with the headset on.

**The RC package is not for a customer yet** (the VST 3 licence agreement is not signed; `VELARIA_VST3_LICENSED=1` was set only to exercise the path), and the legal placeholders stand. It is the install candidate for the NYC clean-install test on Monday.
