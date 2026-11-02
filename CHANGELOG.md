# Kalyx — what changed

For alpha testers. Newest first. Headset builds are numbered `version+build`; the View tab in the headset shows the build you are on, next to the Mac package, bridge and Resolve versions.

## 1.0.8 — 2 November 2026

Headset app 1.0.8+50. Install over 1.0.7 with `sudo ./install.sh`; the headset app updates in place and keeps your panel layout.

**Fixed**

- **Black screen a couple of seconds after the picture appeared, panels gone, nothing to do but reboot.** The headset saves where you put the panels. Reading that file back produced orientations that were a hair off exact, and the Quest's compositor refuses a whole frame when any layer is — so once you had ever placed a panel, every launch failed silently. Poses are now corrected on the way in and again right before every frame goes to the compositor. If a frame is ever refused again, the log says exactly which layer and why.
- **Quit, reopen, stuck on the Quest loading screen until you rebooted the headset.** Android keeps a finished app's process around and starts the next launch inside it; Kalyx View tripped over its own leftovers and quit within a tenth of a second, before drawing anything. The app now ends its process cleanly when you quit, so a relaunch is always a fresh start. Verified: quit → reopen in six seconds, no reboot.
- The hover highlight on wide chips (Copy, Paste, layer names) was oversized and had a thick white line along the top. It now hugs the control with a 4 px margin and a 2 px sheen at any width.

**Added**

- **The headset tells you when Resolve is set up wrong.** The strip's status line shows one warning with the fix in Resolve's own menu words: a square timeline without output scaling, monitoring smaller than the timeline, a frame-rate mismatch, or no picture after the bridge connects. Fix it and the line clears.
- **Kalyx Trim** (Effects ▸ Kalyx): a trim pass for an adjustment clip — exposure, lift, gamma, gain, contrast, saturation, temperature, tint, amount — adjustable from the headset: pick it on the Picture layer and the Placement panel becomes the TRIM panel.
- **Montserrat ships in the package** and the installer adds it to your fonts, so titles set in it export on any Mac.
- **Versions on the View tab.** Bottom right of Look ▸ View: headset build, Mac package, bridge and Resolve. A screenshot of that line answers the first three questions in any bug report.
- **`adb` comes in the box.** The package includes Google's Android platform-tools (`platform-tools/adb`, universal binary, licence in `platform-tools/NOTICE.txt`). You no longer need Homebrew or the Android SDK; if you have `adb` already, yours is used. `sudo ./install.sh --headset` installs or updates the headset app on its own.
- `send-logs.sh` gathers the Mac logs, the versions and the headset log into one zip on your Desktop.
- The Placement and Look bars name the clip you are controlling.
- Scopes are drawn with a smoothing filter instead of nearest-neighbour: lines are lines, not staircases.
- Card outlines in the picture sit under the panels (they used to cut across them) and have proper round corners.
- Test hooks for us: the app reads `cmd.txt` in its files folder for scripted presses, pointing and quitting, so we can reproduce what you report without a hand in the headset.

**Fixed, installer**

- 1.0.8's first installer stopped after placing adb and never installed the Fusion library (caught by a ten-cycle install soak on a second Mac). Fixed before anyone got it.

**Changed**

- The installer clears macOS's download quarantine on everything it places, so there is no "developer cannot be verified" dialog on first run. The Adobe (NDI) path only starts when NDI Tools is installed, or with `--ndi`.
- The installer refuses to run over a Mac that has Blackmagic Desktop Video installed unless you pass `--replace-blackmagic` (it sets the Blackmagic driver aside; `--uninstall` puts it back).

**Known**

- Square 360 timelines (over/under, e.g. 5760 × 5760) still need Output scaling set to 3840 × 3840 while you monitor; the headset tells you when that is the problem.
- No 3D card templates yet: the alpha's 3D path is your own FBX/OBJ scene through Canvas Control's *3D Scene* input. Templates come with beta.
- Nothing on the Quest Store yet: sideload only, developer mode required.

## 1.0.7 — 30 October 2026

- Kalyx View UI v3 (Red Hat Text, line icons, segmented layer well, copper focus hairline, keyframe diamonds, lens presets on the View tab).
- Fusion library for the Edit page: **Kalyx Canvas Control** (footage window, flat graphic, or 3D-only via *Window shows*), **Kalyx 3D Title** and **Kalyx 3D Card** (Vine, Salmon, Hay Bale), **Kalyx Orient 360**.
- Bridge: layer kinds (card / patch / orient), seek from the empty strip, display names for Window aspect and Window shows.
- Installer: Command Line Tools check, `--headset`, `--uninstall`, Fusion library install with dated backups, `send-logs.sh`.
