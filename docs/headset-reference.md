# Kalyx View — headset reference

Every panel as the headset draws it (these pictures come from the app's own renderer, so they are the current UI, pixel for pixel). Hand tracking: pinch to press, pinch-and-hold to drag. Controllers: trigger to press. **Look down** to open the panels, look back up past the horizon to close them; grab a panel's top bar to move it, and it stays where you put it.

## The strip

Always on screen when the panels are open. Its job is to answer two questions at a glance: which layer am I on, and which thing will my hand move?

![Strip, Overlays layer, a card picked](ref/strip-overlays.png)

- **Overlays · Patches · Picture** — the layer control, with a count of what is at this frame. Overlays are titles and footage windows (pickable in the picture). Patches are LatLong Patcher windows. Picture is the whole sphere (PanoMap, Kalyx Orient 360) and the **Kalyx Trim** pass (its bars take over the Placement panel).
- The four icons on the right show and hide the **Placement**, **Look**, **Timeline** and **Scopes** panels (raised = shown). The chevron rolls the strip up to its bar.
- Row two: one chip per thing on this layer; the copper one is what your hand moves. Only ever one. Arrows page through them; a lock on a name means it is locked.
- **Copy · Paste · Undo** act on the picked chip. Undo is Resolve's undo.
- Below: timecode · track · clip · what the overlay is built from, then the ice status line: link (USB / Wi-Fi), software, and the last round trip ("Resolve updated in 0.4 s" or "Rendering… 1.2 s").

![Strip, Patches layer, a locked patch picked](ref/strip-patches.png)

On Patches nothing in the picture is pickable; tap a name instead. **Locked** on the right is the switch for the picked patch; locked patches show a lock on their name and the panels refuse changes until you unlock.

![Strip, nothing on this layer](ref/strip-empty.png)

An empty layer says why and offers the nearest overlay on either side, with its timecode: one pinch to jump there.

![Strip while Resolve renders](ref/strip-rendering.png)

![Strip with a setup warning](ref/strip-setup-warning.png)

When Resolve's monitoring, output scaling or frame rate is wrong, or no picture arrives, the status line says so with the fix in Resolve's own menu words. Fix it and the line clears on its own.

![Strip on the Picture layer, the Trim pass picked](ref/strip-trim.png)

## Placement

![Placement panel for a card](ref/placement-card.png)

- **Ring (Pan)**: you are the centre, front is up (the white notch). Drag the dot around; the copper arc shows how far from front you are. Ticks every 15°.
- **Arc (Tilt)**: side view, up is up; the white line is the horizon.
- **Throttles (Distance, Scale)**: vertical, up = nearer / bigger, log scale with the real units printed beside the travel.
- **Dials (Roll, Card yaw / pitch / roll)**: drag up or down to turn; tap the centre to zero. The value sits under the dial.
- The diamond beside a control is its keyframe: outline = not animated, white = animated, copper = a key on this frame.
- Or skip the panel: pinch a card in the picture and drag it; Pan and Tilt follow your hand.

![Placement panel for a patch window](ref/placement-patch.png)

For a patch or the whole picture the same panel shows the tool's rotations; a locked patch shows LOCKED and ignores drags.

## Look

![Look panel, Frame tab](ref/look-frame.png)

Tabs are the macro's own pages (Frame, Glow, Stereo…). Bars have nudge chips for exact steps; a 0/1 control is a switch; colours get a wheel with a brightness bar and a swatch.

![Look panel, View tab](ref/look-view.png)

![Placement panel as TRIM](ref/placement-trim.png)

A **Kalyx Trim** on an adjustment clip (Picture layer) turns the Placement panel into **TRIM**: exposure, lift, gamma, gain, contrast, saturation, temperature, tint and amount, as bars (the last rows page with the chevron). The Look panel keeps its View tab.

The **View** tab is the app's own settings: stream and playback status; **Source** (Auto / Resolve / Premiere · After Effects); **Picture** (layout, projection, swap eyes, delay); **Overlays** (status bar, level grid, card outline); **Panels** (recentre, placed / follow, scopes on/off). Diagnostics at the bottom, and the versions line: headset build · Mac package · bridge (when it differs) · Resolve. Read that line out, or screenshot it, when you report anything.

![View tab with the fisheye lens presets](ref/look-fisheye.png)

With the fisheye projection the lens presets appear: Canon RF 5.2 mm (190°), Canon RF-S 3.9 mm (144°), 180°, 200°, 220°, and a ±5° trim.

## Timeline

![Timeline deck](ref/deck-timeline.png)

Clip lanes per track, the playhead in red, markers above. Scrub by dragging in the lanes; **‹ Clip** / **Clip ›** jump to clip edges; **−1 / +1** step a frame; the chip on the right zooms to All / 60 s / 10 s.

## Scopes

![Scopes, waveform and vectorscope](ref/scopes-waveform.png)

Measured on the Mac from the uncompressed frame Resolve (or the NDI sender) plays out, before it is encoded for the headset, so they read the monitoring signal itself. Waveform + vectorscope, RGB parade, or histograms. Lines at 0 / 25 / 50 / 75 / 100 are video range (64…940 on the 10-bit scale); the vectorscope carries the 75 % colour-bar targets and the skin-tone line.

## Level grid

Turn it on from the View tab (Overlays ▸ Level grid): a world-level copper horizon with yaw ticks and pitch lines over the footage. Use it with the Picture layer to re-level a shot: bring the real horizon to the line.

## Driving the app without hands (for bug reports and tests)

The app reads a text file once every half second and acts on it, so a scripted session can press, point and quit exactly where a tester did. From a Mac with the headset on USB:

```
adb shell "echo 'press Copy' > /sdcard/Android/data/ca.skylightproductions.vrlink/files/cmd.txt"
```

Commands, one per line: `press <Button> [arg]` (button names as in the source: Copy, Paste, Undo, Tab, Layer, Lock, Jump, FovPreset…), `point <panel> <u> <v> [press]` (aim a fake hand from your head at panel texels u, v: 0 = strip, 1 = placement, 2 = look, 3 = timeline, 4 = scopes; `point off` clears it), `slider <row> <0..1>`, `throttle <row> <0..1>`, `ring <row> <degrees>`, `scrub <0..1>`, `tab <n>`, `open` / `close` (the panels), `roll <panel>`, `status` (one line to the log), `quit` (the Quest-menu quit path). The headset has to be on a head: the Quest shows a Guardian dialog on a desk and blocks launches behind it.
