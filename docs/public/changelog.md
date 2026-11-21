# What changed

Newest first. The Settings panel in the headset shows the versions you are running.

## 1.1 (release candidate)

- **Sound in the headset.** Velaria plays Resolve's monitoring mix in step with the picture. With the **Velaria Sound** insert on a Fairlight bus, the headset hears that bus instead: a stereo bus as stereo, a first-order Ambisonics bus head-tracked through an HRTF (the same SADIE set YouTube uses). No patching in Fairlight. The headset's sound is a spatial review, not a mix monitor.
- **Sound orbs.** With an Ambisonics bus live, the Map switch floats a see-through orb a few metres out wherever the field is loudest, with its own depth, coloured red to yellow by how loud. Up to eight; near sounds merge into one shape, and an orb follows its sound and fades when it stops.
- **Output scaling, handled.** A square 360 timeline is taller than Resolve's monitor raster; the Mac side now sets Output scaling to 3840 × 3840 while a headset is connected and puts your setting back when it leaves. It stands aside while you render.
- **Real stereo for cards and 3D titles** in Canvas Control, with an Eye Separation control; yaw keeps the depth at any angle. Equirect Width is per eye and should match your timeline width.
- **180 and dome.** Velaria 180 to 360 / 360 to 180 adapters, Orient 180, a dome preview with a rendered theatre and seat positions.
- **Headset:** one panel for the selected card (Placement plus the card's own tabs), a Settings panel that stays closed until you need it, dials that turn, cards named for what they are, a title plugged into a Canvas Control is one card with it, a status line that speaks only when something is wrong.
- **DeckLink coexistence:** Velaria's driver and Blackmagic Desktop Video can be installed together; the installer sets Blackmagic's framework aside and restores it on uninstall.
- Fixes: the headset no longer replays audio from before a seek; a stop silences the sound with the picture; the reticle is visible between panels; a 4-channel speaker bus is not mistaken for Ambisonics.

## 1.0

The first public release.

- **Velaria View** for Meta Quest: your Resolve timeline in the headset, in step with Resolve's viewer, over USB or Wi-Fi.
- 360 and 180, mono and stereo, plus a dual-fisheye 180 preview with lens presets.
- **Velaria Canvas Control**, **Velaria Orient 360** and **Velaria Trim** for Resolve, adjustable from the headset.
- Timeline and scopes panels in the headset.
- The headset's status line names the Resolve setting to fix when monitoring is set up wrong.
- `send-logs.sh` gathers logs into one zip for support.

**A note on the name.** During development Velaria was called Kalyx. If you see that name in an older file or project, it is the same software.
