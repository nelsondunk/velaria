# Velaria — what changed

For alpha testers. Newest first. Headset builds are numbered `version+build`; the View tab in the headset shows the build you are on, next to the Mac package, bridge and Resolve versions.

## +88–+90, bridge 1.0.15, 1.1.0-rc1 packaged, driver and plugin swapped in (20 Nov 2026, 19:00)

- Review leftovers closed: universal plugin (arm64 + x86_64), partly covered tap blocks only when due, headset audio start backoff, bridge None-safe restore and driver notice without the aside condition, tap at 48 kHz only, tap.log rotation, "Two Velaria Sound inserts – remove one" on the strip (+88).
- **+90:** the sound map's **Map** switch sits on the strip's top bar while an ambisonic bus plays (its only switch was the View tab, which became the closed-by-default Settings panel in +87).
- **Swapped in on Bruce at 19:00 (Resolve closed, Nelson's password):** the evening's driver and Velaria Sound 0.1.0.2; Resolve relaunched, `tap: listening`, plugin metering the bus from its worker thread. Headset +90 installed. `~/VRLink/release/Velaria-1.1.0-rc1/` holds the same driver, plugin and +90 APK; `check_release.sh` clean apart from the legal placeholders.

## Headset +86/+87 and bridge 1.0.14: the UI pass, first half (20 Nov 2026, evening)

From Nelson's headset walkthrough at 16:30 (spec: project doc "Headset UI pass spec"). Installed on the Quest and Bruce.

- **One card, named for what it is (bridge 1.0.14 + +86).** A macro plugged into Canvas Control's 3D Scene (a title) is folded into that card: its controls appear under the card with the macro's own pages as tabs (Text, Plate, Lighting), its R/G/B inputs become one swatch (Fusion's ControlGroup), names are tidied ("Slab Width" on the Slab page → "Width"; "Key Light Yaw (deg, + = from the left)" → "Key light yaw"), 0/1 inputs are switches. Cards are called *Title · Editorial*, *Footage window*, *Graphic* or *3D scene*; the V-track tag only appears when cards sit on several tracks. A window that shows nothing loses its Image / Border / Glow tabs. The Picture layer is now **Footage**.
- **Selected panel (+87).** The Placement panel is now *Selected*: Placement first, then every tab of the picked card (Frame, Glow, Stereo, Text, Plate, Lighting…). The old Look panel is **Settings** (View, and Dome when the projection is dome), closed by default — nothing about the headset's own setup is on screen until you open it from the strip.
- **Dials turn (+87).** Roll / card yaw / card pitch / card roll follow the angle you sweep around the dial, relative to where you grabbed; bigger dials in two rows; the tilt arc is a true half-ring with square ends; a circle's hover highlight is a short arc, not a straight line; the keyframe diamond's hit box is smaller so its highlight stays off the dial.
- **Less text (+87).** The standing instructions ("Pinch a dot to move", "Tap a dial to zero it", "Pinch the card in the picture…") are gone; notices and Locked remain. The strip's status line shows a problem only while there is one (dropping frames, a delay over 250 ms, a Resolve setup issue); the numbers are on Settings.
- **Strip hover words (+87)** draw over everything, so Timeline / Settings / Scopes are no longer hidden behind the Hide / Copy / Undo chips.
- **Outline and reticle (+87).** The card outline follows the card's real extent (window fill × aspect, or a 3D title's plate width × height) instead of a 16:9 guess; the reticle sits where the ray meets the card's plane and eases between distances, so it no longer skips when crossing a card's edge.

Not yet from the spec: a separate dock (the strip's top bar is the dock for now), the Footage layer rename in the docs.

## Review fixes, bridge 1.0.13, Canvas Control stereo rig (20 Nov 2026, evening)

Built from the two fresh-eyes reviews (ChatGPT and a second Claude) plus Nelson's first real session with a 3D title in Canvas Control. Headset **+84/+85** and **bridge 1.0.13** are installed; the **driver and plugin are built and tested on Bruce but not swapped in** (needs Resolve closed and an admin password — Monday). Canvas Control is installed in the menus but **not yet rendered in Resolve** (see the last item).

- **Velaria Sound 0.1.0.2:** a null channel buffer on an inactive bus (allowed by VST3) is treated as silence / skipped instead of dereferenced — Resolve could crash when bus activation changed; the log write moved off the processing thread (ring + worker); `tap_test` host check added. Validator 47/47.
- **Driver tap:** every datagram is checked before it touches the ring (frames ≤ 512, project time in range, exact length, finite samples — a bad packet from any local process could index outside the ring inside Resolve); the ring forgets everything on a source change or a seek (a stale slot could satisfy a read for the new range); only a bus the plugin reports as **Ambisonics** takes the four-channel path — a Quad or 5.1 bus fed the stereo receivers from L/R instead of being decoded as W Y Z X; a late ambisonic block falls back to **silence**, not Decklink's stereo mix dressed as W/Y/Z/X; two inserts of equal rank are flagged in the log; onAudio serialised; a stop flushes the tap queue and drops the timecode anchor. `tests/tap_test.sh`.
- **Headset +85:** a block the sender marks discontinuous (seek, stop/resume, gap) resets the FIFO and the HRIR history outright — before, up to a second of the previous passage could play first; an AAudio error is flagged in the callback and the stream closed/reopened on the main thread (closing inside the callback is forbidden) with the network queue and HRIR history dropped; sound is flushed on a playback stop, the way frames past the stop point already were.
- **Headset +84:** the reticle stays visible off the panels while the UI is up (on the hovered card at its distance, else 1.5 m along the ray). Before, the ray was invisible between panels and cards were hard to find and grab.
- **Bridge 1.0.12/1.0.13:** output scaling is re-verified every tick (Resolve re-asserts "match timeline resolution" when the Timeline Settings dialog is saved — on 20 Nov the headset got the full 5760 square while the bridge's record said scaled); a partial apply is rolled back; a refused restore keeps its record and retries; the match checkbox counts as a hand change; on the **Deliver page or while rendering** the original scaling is restored and set again afterwards; a restore waits 5 s after the last headset leaves so reconnects do not flap the timeline. 48 tests.
- **Canvas Control (macro, verified on Bruce 20 Nov 17:55):** cards and 3D scenes now have real stereo. Both eyes were rendered from one mono camera (Eye Separation 0, renderers on Mono); the only depth cue was the whole-image convergence shift. Now: parallel stereo cameras with an **Eye Separation** control (0.065 m default), Left/Right renderers, and **Yaw applied as a wrapped shift of the equirect after the render** so the stereo baseline stays square to the card at any yaw (a card rendered at 90° from a fixed left–right pair has no parallax). Also: *Stereo footage* is a checkbox instead of a 0/1 slider, Equirect Width defaults to 3840 (set it to your timeline width), border width defaults to 0. Verified with the Editorial title on 13 Chart 360 TB: title in both eyes on the 60° line, right eye shifted 12 px left on a 3840-wide render (13 expected at 3 m), composited over the plate through a Merge. Finding on the way: **Fusion's Renderer3D with a SphericalCamera3D outputs 2W × W for a Width of W** (512 → 1024×512, whatever Height says), which is why the old default of 2880 produced exactly 5760×2880 eyes on Nelson's 5760 timelines and why a 3840 timeline looked wrong; the macro now halves the width internally and the control means what it says. The crash earlier in the evening was a probe pulling 11520-wide eyes through `GetValue`, not the macro.

## Headset +83: sound map on the sphere (20 Nov 2026, afternoon)

- **Sound map** (Nelson: "wouldn't an ambisonic heatmap be so cool as a scope in headset… an overlay over the whole sphere"). With an ambisonic bus live, a **Map** chip appears beside the Sound chip on the View tab. On, the headset paints where the first-order field is loudest over the picture: the 4×4 covariance of the world-frame W Y Z X (120 ms smoothing, computed in the audio callback) is steered by a cardioid beam per direction on a 512×256 equirect, normalised against the mean (an all-W field paints nothing) and a slowly decaying peak, sharpened for display (peak position untouched), graphite→copper→near-white at 72 % alpha, bilinear in the compositor. First order makes a ~90° blob, so it shows direction, not spread. Host check (`quest/tools/soundmaptest.cpp`): front/left/right/back/up/45° sources peak within half a texel of their direction. Rebuilt at ~45 Hz; costs about a millisecond every other frame.
- **Fix:** the ambisonic decode now takes the head pose relative to the picture's recentre yaw, so the sound field turns with the picture after Recentre view (before, it stayed at room front).
- Setting `soundMap` persists; the desktop preview (`hudpreview`) has a `look-sound` render.

## Head-tracked binaural decode, installer and bridge fixes (20 Nov 2026, later)

- **Headset +81: the ambisonic bus is decoded through an HRTF.** The first-order field is still rotated by the inverse head pose, but each component now goes through Resonance Audio's SADIE II spherical-harmonic HRIR (256 taps, 48 kHz) instead of a pair of cardioids: left ear = W+Y+Z+X filtered, right ear = W−Y+Z+X. Sounds sit outside the head and front/back differ by spectrum, not just level. Kernel checked on the host (`quest/tools/hrirtest.cpp`): a front source lands at −0.8 dB, a lateral one at +3 / −7.7 dB between the ears; 1 s of audio decodes in ~20 ms. Not yet heard by Nelson.
- **Installer ships Velaria Sound** into `/Library/Audio/Plug-Ins/VST3` (and removes it on `--uninstall`); `package.sh` includes the plugin only with `VELARIA_VST3_LICENSED=1`, i.e. once the Steinberg VST 3 agreement is signed. `--coexist` is no longer marked UNTESTED.
- **Bridge 1.0.11:** a line on the status strip when Desktop Video has been installed over Velaria's driver ("run sudo ./install.sh --coexist, then restart Resolve"); output-scaling records now name their own project and are restored when that project is next open (20 Nov: a timeline in *Strange Worlds Teaser copy* was scaled while the headset was connected, then the record pointed at the wrong project and the log repeated "not found" every two seconds).
- Customer docs: setup guide and known limits now describe sound in the headset and the Velaria Sound recipe.

## Unreleased — Velaria Sound: the mix reaches the headset from a Fairlight bus (20 Nov 2026)

Headset **1.1.0-dome+80**, driver rebuilt the same morning, plugin `VelariaSound.vst3` 0.1. **Confirmed by Nelson, 20 Nov 09:18–09:48:** ambisonic bus → headset, head-tracked, no patching; stereo bus → headset; insert survives a Resolve restart.

- **Velaria Sound** (VST3; "Velaria Tap" for one night): an insert for any Fairlight bus or track. Audio passes through untouched; a copy of every channel goes to the Velaria driver with the timeline position, and the driver feeds the headset from it — **a stereo bus for "Sound: in headset", a first-order Ambisonics bus for "Sound: ambisonic"** — aligned to the picture by timecode. Patch Input/Output is no longer part of the setup. The Sound chip shows "· bus" when the plugin is the source.
- Clock work it took (+76–+79): Fairlight processes a bus near its play time while Resolve hands the Decklink audio out up to 300 ms early, so the driver holds each block until the plugin's audio for it exists; the headset's presentation delay now follows the **worst** audio arrival in its window (sound cannot afford the 2% a picture can); a block that overlaps the FIFO by a few milliseconds is trimmed and crossfaded in rather than restarting the FIFO (the restart was a hole at every delay step; easing a sample at a time buzzed at 480 Hz). Stale blocks from a stop or seek are discarded, never sent.
- Plugin state: a small versioned record, because Fairlight dropped an insert whose saved state was empty.
- Not yet: the installer does not copy the plugin (hand `ditto` into `/Library/Audio/Plug-Ins/VST3`); the Steinberg VST 3 licence agreement must be signed before the plugin reaches a customer; the decode is still the first-order cardioid pair (Resonance Audio HRTF next).

## Unreleased — Velaria Sound and DeckLink coexistence (19 Nov 2026, late)

- **Velaria Sound** (`mac/sound/`, VST3): an insert for a Fairlight bus that passes audio through untouched and sends a copy of every channel, with the timeline position, to the Velaria driver. Resolve 21 accepts it on an **Ambisonics 1st order bus** and feeds it the four raw channels after the panner plus `projectTimeSamples` (checked on Bruce 19 Nov 22:32). The driver (`src/Tap.*`) listens on 127.0.0.1:47813 and, when the plugin is running, builds the headset's four-channel ambisonic message from the bus aligned by timecode instead of Decklink 1–4 — so an ambisonic mix is heard head-tracked in the headset with **no Patch Input/Output**, panned sources included. Falls back to Decklink 1–4 when the plugin stops. UNTESTED end to end in the headset (driver built, not installed); the licence agreement for the VST3 SDK has to be signed before this ships.
- **DeckLink coexistence confirmed on hardware** (Bruce, DeckLink Mini Monitor 4K, Desktop Video 16.5, macOS 26.6): with Blackmagic's framework set aside by `--coexist`, Resolve lists *Velaria (Quest)* and the card; switching between them works with a Resolve restart per switch. Installer now sets aside and restores by copy-and-delete rather than rename (macOS's launch cache follows a renamed framework). Three Blackmagic-side findings documented in `docs/decklink-coexistence-test-plan.md`.

## Unreleased — Output scaling handled by the bridge (19 Nov 2026, overnight)

Bridge 1.0.10 (installed on Bruce 19 Nov night; UNTESTED with a headset — tests only). A square 360 timeline is taller than Resolve's 7680×4320 monitor raster, so until now the headset showed a cropped picture until you set **Timeline Settings › Output scaling › 3840 × 3840** by hand — and a forgotten setting shipped a 3840² export. Now, while a headset is connected, the bridge sets that scaling itself (square → 3840 × 3840; anything else keeps its aspect inside the raster) and puts the original back when the last headset disconnects, or when you switch to another timeline. The headset's status line says *Output scaling set to 3840 x 3840 for the headset … put back when it disconnects* while it is in force. The originals are kept in `~/Library/Application Support/Velaria/output-scaling.json`, so a bridge that is killed mid-session restores them the next time it starts. A value you change by hand while it is in force is left as you set it.

## Unreleased — 180, stereo and dome output (18 Nov 2026, overnight)

Headset app 1.1.0-dome+62 (installed on Nelson's Quest 18 Nov 20:30, **not yet seen by him**). Macros verified by rendering the labelled charts in the "Velaria Format Tests" project on Bruce; the headset half is UNTESTED.

- **180 family (verified):** *Velaria 180 to 360* and *Velaria 360 to 180* adapters (VR180 side-by-side ↔ the 360 over/under frame the other macros work in; round trip is pixel-identical), *Velaria Orient 180* (tilt and roll on both eyes, same signs as Orient 360 and ffmpeg v360), and the existing Canvas Control card placed on a 180 plate through the adapter (centred card lands at the centre of both eyes; Yaw 60 lands on the 60° line).
- **Velaria Stereo 180 / Stereo 360 (macro verified):** Convergence (−0.05..0.05 of the eye width; +0.02 moved the left eye +20 px and the right −20 px on a 1920-wide eye) and Swap Eyes. In the headset the bars sit on the Placement panel under STEREO, like Trim.
- **Live convergence in the headset (UNTESTED):** while the Convergence bar is dragged the two eyes are yawed apart by the change, so the depth moves under your hand before Resolve re-renders; the offset is dropped 700 ms after release. The direction of the yaw has not been checked on a device: if dragging right makes things look *further* away in the headset and *nearer* once Resolve catches up, the sign is wrong (one line in `main.cpp`).
- **Velaria Dome Output:** equirect 360 (or one eye of a stereo plate) to a fulldome master, zenith at the centre, front at the bottom of the frame, with Theatre Tilt, Horizon, Pan, Master FOV and source-eye controls. As a **DCTL** for the Color page (verified on Bruce against ffmpeg's v360 at tilt 0 and tilt 20) and as a **GPU Fuse** for the Fusion page (same formulas, UNTESTED: Resolve only registers Fuses at launch). Both ship only in a Dome package (`VELARIA_DOME=1 ./package.sh`).
- Known gap: merging Canvas Control's output over the plate *inside one Fusion comp* misplaces the card in both 180 and 360; the shipped way (the card on its own track) is right. Matters only for future macros that merge internally.

Later the same evening, with Nelson present (headset +63 → +66, all UNTESTED on the device):

- **Hide / Show** (strip, eye icon in the icon cluster): disables the picked card's clip in Resolve (Enable Clip, D) so the plate shows through; "Show n" brings back everything hidden on the layer; hidden names dim; hidden cards are neither outlined nor pickable. A D pressed in Resolve reaches the headset within two seconds.
- **Review notes** (Timeline panel, new row): ‹ Marker · Drop note · Marker ›. Drop note adds a Cyan marker at the playhead whose note says where you were looking, in Canvas Control's Yaw / Pitch, and which card was picked; on a marker the chip removes it and the row shows the marker's name and note. **Still** grabs a gallery still of the current frame.
- **Sound in the headset:** Resolve's monitoring mix (first two channels, 48 kHz) plays through the Quest on the same clock and delay as the picture. "Sound: in headset / waiting / off" sits beside Source on the View tab. Needs the new Mac driver installed (it forwards the audio Resolve already sends the device); until then the chip reads "waiting".
- **Sharper UI:** every UI quad asks the compositor for supersampling and sharpening (`XR_FB_composition_layer_settings`), Meta's fix for text and thin rules shimmering.
- Not built, on purpose: a red/green "rendered" stripe. The API only says whether caching is switched on for a clip, not whether it is complete.

Thu 19 Nov morning, after Nelson's first pass in the headset (+67, +68; driver rebuilt, awaiting install):

- **Sound after a pause** (driver): the first play worked and every later one was refused as "buffer full"; the audio bookkeeping now resets on stop and on a fresh preroll. Needs the rebuilt framework installed (`sudo ~/VRLink/xfer/install-branch-driver.sh`).
- **Chips:** edge highlights and inset shadows follow the rounded corners; chips inside a well (tabs, the layer control) cast no shadow. Both were what read as "a line hanging off the rounded square" and "a square box behind the tabs".
- **Review notes:** Drop note opens six presets, each its own marker colour: Editing, Sound, Colour, Graphics, Immersive, Love this. The marker's name is the word; the gaze numbers are gone. Marker triangles on the Timeline lanes take their colour.
- **Grab still** writes `<the clip's media folder>/Velaria Stills/<timeline> <timecode>.jpg` at the timeline's output size and stays on the current page (the gallery route flipped to Color).
- **Panels: follow me** now glides with you when you turn, like the strip (they used to wait for the next look-down).
- **Setup warnings** (1080 monitoring and the like) also appear as a toast, not only on the strip's status line.
- "Layout" is labelled **Stereo layout**.
- **Dome:** the seat sits below the rim (**Rim +1.8 m**, a chip in place of Master FOV; 0 / 1.2 / 1.8 / 2.5 m), and a **rendered planetarium interior** (seats, cove light, aisle lights, rear console) shows behind the dome picture, one stereo render per seat, so the seat presets read as places in a room. Setting `domeTheatre 0` turns it off. The Dome Output DCTL and Fuse feather the circle's edge by a pixel.
- **Sync test clip:** `Sync test/Velaria sync flash beep 30fps.mov` on Baxter and timeline **15 Sync flash beep** in the test project: a one-frame white flash and a 1 kHz beep start on every :00 frame, with a frame counter and a sweeping second hand.

Thu 19 Nov, after the second pass (+69; driver with the pause fix installed, **sync test: flash and beep land together**, sound survives a pause):

- **Dome theatre v2.** A modern dome theatre: seven raked rows in the rear half of the room, everyone facing the front, side-aisle stairs with step lights, a rail ahead of the first row, open floor and a lectern in front; nobody sits near the front wall. Seats are real places in it: **sweet spot** (row 3), **front row**, **rear row**, **left** and **right** (ends of row 4). Rendered at 6144 × 3072 per eye (three times +68's pixels per degree).
- **Depth:** the dome, the drum wall and the cove light are at infinity, and so is the dome picture: the headset now warps the domemaster per pixel from the seat's eye position (GPU) onto an infinite sphere, so the picture's rim and the top of the wall come from the same geometry and never shift against each other. Floor, tiers, seats and rail carry the stereo, at a 90 mm eye spacing so the room reads big. The trade: leaning no longer moves you inside the dome (there is no parallax at infinity); turning your head is unchanged.
- **Fewer chips:** Dome size and Rim are gone (the seat table carries the eye position, and at infinity the dome's real size changes nothing); a **Theatre** chip shows or hides the room. The dome row is now trim − · Tilt · trim + · Seat · Theatre.
- Fixed from +68: the left and right seats were mirrored.
- The 6144-pixel theatre decodes on a worker thread (a couple of seconds on the Quest; the picture keeps running) and lives in a single-image swapchain (151 MB, not three of them).

Thu 19 Nov, after the third pass (+70, pending Nelson's choice of venues from the preview stills):

- **Venues.** The dome room comes in three sizes (Small 12 m, Medium 18 m, Large 26 m) and four screen tilts (Level, 15° hybrid, 25° cinema, 30° IMAX dome), each a rendered room with its own raked seating (tilted rooms rake steeper, the chairs recline further, the screen wall runs down to the floor ahead of the audience) and its own seat table (`quest/src/dome_seats.h`, generated by `theatre_scene.py --tables`). The tilt presets *are* the rendered rooms; the 1° trim moves the picture inside the nearest room.
- **Dome tab** on the Look panel (appears with the Dome projection and opens automatically): Venue · Screen tilt · Seat · Theatre on/off · Master field of view. The View tab is back to the projection chip alone.
- **Room light:** cove off; one soft cool source from the dome above-front standing in for the show, plus a faint dome glow, step lights and exit signs. Deep shadows.
- **Eye spacing** back to 64 mm (90 mm read cross-eyed on the neighbour chair). **Default seat** is row 5 of 7 in the medium room (0.6 of the block).
- Theatre renders are named `<size><tilt>_seat<n>.jpg`; a missing venue render falls back to the picture on black.
- **Fade** (+70): a seat, venue or tilt-preset change fades the room and the picture to black together (¼ s), swaps while black, and fades up (⅓ s) once the new room is decoded and the picture re-warped (`XR_KHR_composition_layer_color_scale_bias`; a hard cut where the runtime lacks it). The 1° trim stays live.
- **Tuning with Nelson in the headset (+71, +72):** eye spacing 64 mm; the chairs either side of the viewer are left empty (the neighbour's near armrest at 0.6 m was the eye-crossing object) and the viewer's own chair is not drawn; the room went four stops darker in all — "just barely able to see floors and walls"; step lights and exit signs are the brightest things in it.
- **Seam at the back** (+73): both 360 layers now carry a one-texel wraparound margin, with the layer rectangle set inside it, so the compositor's edge sample at 180° lands on real content instead of drawing a hairline.
- **Halation:** a bloom pass in the Blender compositor on the renders (exit signs, step lights, lectern screen).

Thu 19 Nov afternoon (+74, driver rebuilt and installed 14:3x): **ambisonic sound, head-tracked, first order.**

- Resolve side: a **Quad** track into a **Quad** bus, patched Bus 1-0…1-3 → Decklink 1–4 in Fairlight ▸ Patch Input/Output (un-patch Control Room from 1–2 first). Fairlight's own Ambisonics bus format refuses to patch ("source beds are not supported in Dolby Atmos"), and the Adaptive format is not offered; Quad passes the four AmbiX channels (ACN W Y Z X) untouched, which the driver's new per-channel meter confirmed. BMD16 is the format for third order later.
- Driver: the hello carries `kCapAudio4`; such a headset gets the first four channels, others the stereo pair as before. A per-channel RMS meter line every two seconds in `driver.log`.
- Headset: the **Sound** chip cycles *in headset* (stereo) → *ambisonic* → *off*. In ambisonic mode the field is rotated by the inverse head pose each render callback and decoded with a cardioid pair at ±45° (gains glide across the callback as the head turns). Left/right and head-tracking are right; front/back is a level cue only and elevation is flattened until the HRTF step (Resonance Audio, third order).
- Test material on Baxter, `Sync test/`: `Velaria ambisonic test 1st order.wav` (tones by direction) and `… spoken.wav` (each direction announced by a voice placed where it says, then its tone; `macros/regression-rig/mkambix.py`, `mkambix_voice.py`). Confirmed by Nelson in the headset: "ambisonics and head tracking is working", "works great".

+75 (15:43): **all twelve venues** in the app (Small / Medium / Large × Level / 15° / 25° / 30°, five seats each, 6144² top/bottom; 50 MB of renders, 27 MB APK). Rendered on Bruce 13:09–15:36 at the final look.

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
