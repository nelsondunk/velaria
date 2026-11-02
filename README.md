# VR Link — live stereo preview from DaVinci Resolve to Meta Quest

VR Link shows your Resolve timeline in a Quest headset in real time, frame-accurate and in step with Resolve's
own viewer. No capture hardware, no OBS, no screen grabbing.

## How it works

Resolve already knows how to play out to a Blackmagic DeckLink card, frame by frame and with timecode.
VR Link is a **software DeckLink card**: Resolve sees "Kalyx (Quest)" under Video I/O and plays out to it
exactly as it would to real hardware.

1. **Virtual card** (`DeckLinkAPI.framework`, loaded by Resolve): runs the card's playback clock, finds
   the picture inside the output raster, and crops it with no copy. It then encodes the frame as HEVC Main10
   with VideoToolbox (about 20 ms) and streams it, stamped with its exact display time.
2. **Headset app** (`VRLink.apk`): decodes in hardware, maps each frame onto per-eye 180°/360° equirect
   compositor layers, and shows it on the display refresh nearest that display time. The refresh rate is picked
   as a whole multiple of the timeline rate (for example 72 Hz for 23.976/24p), so every frame gets the same
   number of refreshes.

Resolve schedules frames ahead of their display time, so they reach the headset about 200 ms early. The
headset therefore matches Resolve's viewer instead of lagging behind it. Measured: frames appear a median of
0 ms (95th percentile 6 ms) from their display time on the Mac, over Wi-Fi.

## Install (once)

```
cd "VR-Link-1.0.5"
sudo ./install.sh
```

The installer:

- puts the driver in `/Library/Frameworks/DeckLinkAPI.framework`, backing up Blackmagic's own driver if
  it's installed;
- starts a small helper that sets up the USB tunnel whenever the Quest is plugged in;
- installs the headset app if a Quest is connected.

**Uninstall:** `sudo ./install.sh --uninstall` removes everything and restores Blackmagic's driver.

## Use

1. In **Resolve**:
   - Go to Preferences → Video and Audio I/O → *For capture and playback use* → **Kalyx (Quest)**.
     Restart Resolve if it asks.
   - In Project Settings → Master Settings → **Video Monitoring**, set the resolution to:
     - **7680 × 4320 Ultra HD** for 5.6K (5760×2880) timelines: frames go out 1:1 with no scaling;
     - **3840 × 2160** if 4K preview is enough.

     Set the format to the timeline frame rate.
   - **Square (360 top-bottom) timelines:** Resolve centre-crops anything taller than the monitoring raster
     rather than scaling it, so a 5760×5760 timeline reaches the card as a 5760×4320 slice with the poles
     missing and shows up flat. In Timeline Settings (or Project Settings → Image Scaling) set **Output
     scaling** to **3840 × 3840** while monitoring. This is also the render resolution: set it back before
     delivering.
2. On the **Quest**, open *VR Link* (App Library → Unknown Sources). It connects automatically:
   - **USB:** the Quest plugged into the Mac (developer mode on).
   - **Wi-Fi:** the Quest on the same network as the Mac. A Wi-Fi 6E access point is best.
3. Press play in Resolve.

### Premiere Pro, After Effects and anything that speaks NDI

Adobe apps have no Blackmagic output of their own, but the free **NDI Tools** (ndi.video/tools) add an NDI
output to Premiere's and After Effects' Mercury Transmit. VR Link's NDI helper (`vrlink_ndi`, started by the
installer) picks that stream up on the Mac and sends it to the headset through the same encoder and the same
link as Resolve — the headset never touches NDI itself, which is why this works at 5.6K where NDI players on
the headset don't.

1. Install NDI Tools, then in Premiere: Settings → Playback → tick **Enable Mercury Transmit** and the **NDI**
   device (After Effects: Settings → Video Preview). A sequence has to be open and playing or scrubbing for
   frames to flow; a parked Premiere sends nothing and the last frame stays up in the headset.
2. In the headset, View tab → **Source**: *Auto* follows Resolve's card when it is running and NDI otherwise;
   *NDI* or *Resolve* pins one. The helper follows the first Premiere / After Effects source it sees;
   `vrlink_ndi --list` shows what is on the network and `--source <name>` picks one (log:
   `~/Library/Logs/VRLink/ndi.log`).

The timeline deck and the Canvas controls are Resolve-only; with an NDI source the headset is a monitor.

### In the headset

- **Status strip.** A slim bar sits just below the horizon: link (USB or Wi-Fi), layout, size, frame rate,
  bitrate, timecode and a play/pause/stop mark. It follows you lazily when you turn and stays out of the
  picture until you glance down. Red text means something needs attention ("Dropping frames – try the USB cable").
- **Panels.** Look down (about 22°) and four glass panels open around you; look up past the horizon and they
  close. With hand tracking, point at a control and pinch index to thumb; with controllers, point and pull the
  trigger. Grab the bar at the top of any panel to move it: it then stays where you put it. The chevron at the
  right end of the bar rolls a panel up to its title. Positions and rolled state are remembered between
  sessions, relative to where you were when the panels opened.

| Panel | What it holds |
|---|---|
| **Instance strip** (top) | Which card you are on (‹ › cycles stacked cards), the clip it sits on, the comp and footage it is built from, Copy / Paste / Paste all / Undo / Refresh. The title bar shows *Rendering…* and the measured time for Resolve to deliver the new frame. |
| **Placement** (right) | *Position on sphere:* pan ring (you are the centre, front is up) and tilt arc. *Scale · Distance:* vertical throttles. *Card rotation:* dials for roll, card pitch / yaw / roll — drag a dial up or down, tap it to zero. On a clip whose comp holds a **PanoMap** or **LatLong Patcher** the panel reads *Orientation* and turns the whole sphere: pan, tilt, roll (and the patcher's window angle on a Look tab). |
| **Look** (left) | Tabs: **View** (stream, timing, layout, projection, delay, recentre, panels placed/follow, **level grid**) and the macro's own pages — Stereo, Border, Image, Glow, Output — as bars with ± nudges, switches and a colour wheel. |
| **Timeline deck** (low) | Timecode, `◀ Clip` / `Clip ▶`, `−1` / `+1`, zoom (all / 60 s / 10 s), all video tracks with Resolve's clip colours and markers; pinch on the lanes to jump. |

- **Pick a card in the scene.** Point at a card in the picture and pinch: it becomes the current instance and
  gets an outline. Keep the pinch and move your hand to pan and tilt it; Resolve catches up.
- **Card outline** (View tab) is the thin copper line on the selected or pointed-at card; switch it off when it
  gets in the way of judging the picture.
- **Level grid** (View tab) draws a true-level horizon over the picture — copper line, 10° ticks with left/right
  yaw labels, pitch lines at ±10…60°. A wonky horizon in the footage shows against it; bring it to the line with
  the PanoMap roll and tilt. The grid is in the room, not in the footage, so it never moves with your edits.
- **Drags send one change at a time.** While Resolve is rendering the last value, the control keeps moving in
  the headset and the latest position goes out the moment the new frame lands, so Resolve always renders where
  your hand is now rather than working through a backlog.
- **◆ on a control** sets a keyframe at the playhead (copper diamond = animated). Later changes to an
  animated control land as keys at the playhead, as they would in Resolve.
- **Undo** puts the last change back (a whole paste counts as one). Changes are real project changes: save in
  Resolve as usual.

Controllers keep shortcuts: **A** strip on/off, **B** swap eyes, **menu** panels placed/follow, right stick
delay ±10 ms (click = automatic).

Play/stop can't be driven from the headset (Resolve's scripting has no transport control); use the space bar.
Edits are meant for a parked playhead. On a frame with several stacked Fusion comps Resolve can take a second or
more per change; the instance strip shows the wait.

The headset talks to a small helper on the Mac, the **bridge** (`vrlink_bridge.py`, installed as a LaunchAgent
by `install.sh`, log in `~/Library/Logs/VRLink/bridge.log`). It uses Resolve's scripting API, so Resolve Studio
is required and External Scripting must be allowed (Preferences → System → General → External scripting using:
Local). Control names shown in the headset are the bridge's display names; the macros themselves are unchanged.

Layout and projection are detected automatically: a 2:1 picture is treated as side-by-side 180°, and 1:1 as
top-bottom 360°. Overrides are remembered.

The panel's **Timing** row shows the playout delay (automatic by default: it grows at once when frames arrive
late and walks back down while things are calm), how early frames arrive, decode time and the clock
round-trip. **Frames** counts frames shown, dropped, and shown for an uneven number of refreshes.

## Settings (Mac)

`~/Library/Application Support/VRLink/config.plist`. Restart Resolve after editing. The settings are:

- `Layout`
- `Projection`
- `Crop` (auto/full/fit)
- `MaxEncodeWidth` (default 5760)
- `BitrateMbps` (0 = auto, about 80 Mb/s for 5.6K24, capped at 150)
- `KeyframeIntervalSec`
- `Port`

Logs are in `~/Library/Logs/VRLink/driver.log`. The headset reports its frame statistics there every 10 s.

## Limits (honest)

- **Picture size:** Resolve's largest output raster is 8K (7680×4320 / 8192×4320). That fits 5760×2880
  SBS at 1:1, but 5760×5760 top-bottom 360 is scaled to 4320×4320 (75%). True 5.6K-per-eye (11520×5760)
  exceeds both Resolve's raster and the Quest decoder.
- **Audio:** listen on the Mac. Resolve's audio still plays locally; the headset is picture-only in v1.
- **Other apps that scan for capture cards** (Zoom, OBS, Skype) load the driver too and will list a
  "VR Link (Quest)" device they can't capture from. Harmless, and they never take the headset port: that is
  opened only by the app that actually outputs video.
- **Blackmagic hardware:** while VR Link is installed it replaces Blackmagic's Desktop Video driver, so a
  real DeckLink/UltraStudio won't appear. Run `--uninstall` to switch back.
- **Resolve updates:** Resolve 21.1.1 talks to cards through the DeckLink 10.11-generation interfaces. A
  future Resolve could need a driver update. The log shows any interface it asks for that VR Link doesn't
  provide.
- **Frame rate:** 5.6K60 encodes in about 15 ms per frame on an M4 Max, which leaves headroom. Older or
  base-model Macs may need `MaxEncodeWidth` lowered for 60p.

## Tests

The `tools/` folder contains:

- `selftest` — drives the virtual card like Resolve and checks timing.
- `bench` — crop, convert and encode speed at 5.6K.
- `e2e_source` + `probe` — a full pipeline test. It encodes a frame-number barcode into each frame and
  verifies every frame arrives exactly once, in order, decoded, on time.
- `area_test` — the letterbox detector against dark frames, CG on black and real format changes.
- `bridge-tests/` — `python3 test_bridge.py` runs the Resolve bridge against a fake Resolve: timeline snapshot,
  control naming and sections, set/undo/paste, keyframes at comp time, Fusion-page playhead.
