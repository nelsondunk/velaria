# Velaria — what changed

For alpha testers. Newest first. Headset builds are numbered `version+build`; the View tab in the headset shows the build you are on, next to the Mac package, bridge and Resolve versions.

## Unreleased — 180, stereo and dome output (18 Nov 2026, overnight)

Headset app 1.1.0-dome+62 (installed on Nelson's Quest 18 Nov 20:30, **not yet seen by him**). Macros verified by rendering the labelled charts in the "Velaria Format Tests" project on Bruce; the headset half is UNTESTED.

- **180 family (verified):** *Velaria 180 to 360* and *Velaria 360 to 180* adapters (VR180 side-by-side ↔ the 360 over/under frame the other macros work in; round trip is pixel-identical), *Velaria Orient 180* (tilt and roll on both eyes, same signs as Orient 360 and ffmpeg v360), and the existing Canvas Control card placed on a 180 plate through the adapter (centred card lands at the centre of both eyes; Yaw 60 lands on the 60° line).
- **Velaria Stereo 180 / Stereo 360 (macro verified):** Convergence (−0.05..0.05 of the eye width; +0.02 moved the left eye +20 px and the right −20 px on a 1920-wide eye) and Swap Eyes. In the headset the bars sit on the Placement panel under STEREO, like Trim.
- **Live convergence in the headset (UNTESTED):** while the Convergence bar is dragged the two eyes are yawed apart by the change, so the depth moves under your hand before Resolve re-renders; the offset is dropped 700 ms after release. The direction of the yaw has not been checked on a device: if dragging right makes things look *further* away in the headset and *nearer* once Resolve catches up, the sign is wrong (one line in `main.cpp`).
- **Velaria Dome Output:** equirect 360 (or one eye of a stereo plate) to a fulldome master, zenith at the centre, front at the bottom of the frame, with Theatre Tilt, Horizon, Pan, Master FOV and source-eye controls. As a **DCTL** for the Color page (verified on Bruce against ffmpeg's v360 at tilt 0 and tilt 20) and as a **GPU Fuse** for the Fusion page (same formulas, UNTESTED: Resolve only registers Fuses at launch). Both ship only in a Dome package (`VELARIA_DOME=1 ./package.sh`).
- Known gap: merging Canvas Control's output over the plate *inside one Fusion comp* misplaces the card in both 180 and 360; the shipped way (the card on its own track) is right. Matters only for future macros that merge internally.

## Unreleased — 1.1 work in progress (Dome preview)

**UNTESTED on a headset.** Built on Linux only (13 Nov 2026); nothing below has been seen in a Quest.

- **Dome projection** in the headset (Look ▸ View ▸ Projection: Dome). A domemaster (square, mono, equidistant fisheye, zenith at the centre, bottom of the frame = front of the dome) is shown on a real-sized hemisphere around your seat. Chosen by hand; nothing detects it automatically.
- **Theatre controls** appear under PICTURE with the Dome projection: tilt presets (Level, 10°, 15°, 20°, 25°, 30°) and a 1° trim, Seat (centre, front, rear, left, right: half a radius off centre), Dome diameter (8 to 24 m) and Master field of view (180°, 200°, 220°; the dome shows the inner 180°). Recentre view also re-takes your seat.
- Cards are not picked or outlined in the scene while the Dome projection is on.
- **Fixed on the way:** with Projection set to Fisheye 180° the View tab read "flat" and the lens presets never appeared (the label table stopped at 360°). It now reads "Fisheye 180°" and the presets show.

## Unreleased — coexistence with Blackmagic Desktop Video (opt-in, not for customers yet)

**UNTESTED.** Written on Linux (13 Nov 2026); never compiled for macOS, never run beside Desktop Video or a real card. Design and test plan: `docs/decklink-coexistence.md`, `docs/decklink-coexistence-test-plan.md`.

- `sudo ./install.sh --coexist` sets Blackmagic's driver aside where Velaria's driver can load it underneath, so Resolve's Video I/O should list Velaria (Quest) and the Blackmagic device. The default install and `--replace-blackmagic` behave as before.
- `./install.sh --status` (no sudo) says which driver is active and what is set aside. `send-logs.sh` includes it.
- `--uninstall` no longer puts an old Blackmagic backup inside a newer Blackmagic driver when Desktop Video was reinstalled after Velaria.
- Driver log: lines starting `coexist:` record what was loaded, its version and the devices it reports.

## 1.0.9 — 2 November 2026 (the rename)

Headset app 1.0.9+51. **Kalyx is now Velaria** everywhere: the headset app (*Velaria View*, a new package — install it fresh with `sudo ./install.sh --headset`; your panel layout starts over), the Resolve device (*Velaria (Quest)* in Video and Audio I/O — pick it again after the update), the Fusion library (Effects ▸ **Velaria**: Canvas Control, Orient 360, Trim) and the Mac helpers.

**Upgrading from 1.0.8:** run `sudo ./install.sh` from the new folder. It removes the old helpers and launch agents, keeps your settings file, and moves the Kalyx macros into the Fusion archive. Timelines keep working as they are (each comp carries its own copy of a macro); to switch a comp's instances to the Velaria macros, open it on the Fusion page and run **Workspace ▸ Scripts ▸ Comp ▸ Skylight_UpgradeMacros** — one undo step. The headset reads both old and new instances.

Also in this build: Trim's bars moved to the Placement panel (TRIM), the Placement and Look bars show the clip name, card outlines sit under the panels with proper round corners, scopes are drawn smooth, and the 3D Titles / 3D Cards are no longer in the package (Skylight-internal; chooseable 3D card templates come later).

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
