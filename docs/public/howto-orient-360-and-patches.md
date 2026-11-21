# Orient 360 and patches

Use this page to re-level or re-aim a 360 shot, and to work on paint-out patches, from inside the headset.

## What each tool is

- **Velaria Orient 360** turns a stereo over/under 360 shot: Pan, Tilt and Roll, the same for both eyes. It has one switch, **Stereo 360 (Top / Bottom)**.
- **PanoMap** is Resolve's own tool. Use it for a mono 360 shot. It shows up in the headset in the same place.
- **LatLong Patcher** is Resolve's own tool for lifting a flat patch out of a 360 picture so you can paint out a tripod or a camera support. Velaria does not paint. It lets you see and move each patch window in the headset.

## Re-orient a stereo 360 shot

1. Open the clip on the Fusion page.
2. Add **Velaria Orient 360** from the Effects Library's Macros, between your last patch or paint node and MediaOut. Putting it last means your patches turn with the shot.
3. Put the headset on and switch the strip to **Edit**. Tap **Orient 360**. The Selected panel shows Pan, Tilt and Roll.
4. Open **Settings** and turn on **Level grid**. A level copper horizon appears over the footage. Bring the real horizon to the line.

If you drag Orient 360 from the Edit page onto a clip that already has a Fusion composition, Resolve adds it without connecting it. Connect it on the Fusion page.

## Work on patches

1. Build your paint-outs with LatLong Patcher on the Fusion page as usual.
2. In the headset, switch the strip to **Edit**. Each LatLong Patcher window has a chip, beside the sphere tools.
3. Tap a chip. The Selected panel shows that patch's rotations.
4. Use the **Lock** switch on the right of the strip to lock a patch you are happy with. A locked patch shows a lock on its chip and ignores changes until you unlock it.

On the Edit stage nothing in the picture is pickable, so a stray pinch cannot move a title while you work.

## Quick answers

- **Orient 360 does nothing.** It is not connected. Check on the Fusion page that the shot goes into it and it goes into MediaOut.
- **The eyes come apart when I turn the shot.** The shot is mono, or it is not over/under. Use PanoMap for mono.
- **A patch will not move.** It is locked. Tap the Lock switch on the strip.
