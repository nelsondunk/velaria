# Known limits

What Velaria 1.0 does not do, stated plainly.

## What it runs on

- **Mac with Apple silicon only**, macOS 13 or newer. No Intel Macs. No Windows.
- **DaVinci Resolve Studio only**, 19 to 21. The free Resolve will not work. A future Resolve version may need a Velaria update.
- **Meta Quest only.** Quest 3 is tested. Quest 3S, Quest 2 and Quest Pro run the app. Apple Vision Pro and Pico are not supported.
- **Developer mode is required.** Velaria View is not in the Meta Horizon Store. The installer loads it onto the headset over USB.
- **Not signed by Apple.** macOS may ask you to approve it in System Settings the first time. The setup guide has the exact words.

## Blackmagic hardware

Velaria takes the place of Blackmagic Desktop Video while it is installed. A DeckLink or UltraStudio will not appear in Resolve until you uninstall Velaria, which puts Blackmagic's driver back. You can have one or the other, not both at once.

## Picture

- **Square 360 timelines** (over/under) monitor at 3840 x 3840. Set Output scaling in Resolve while you monitor and set it back before you deliver.
- **Pictures wider than 5760 pixels** are scaled down to 5760 for the headset.
- **The headset picture is compressed.** It is for judging framing, depth, comfort and placement. It is not a pixel-exact or colour-accurate reference. The scopes read the uncompressed signal on the Mac.
- **Fisheye 180° is a preview**, not a calibrated lens conversion.
- **60 frames per second at 5760 pixels wide** needs a fast Mac. On an older or base-model Mac, lower `MaxEncodeWidth` in the settings file.

## Sound and transport

- **Frame rates.** 24, 25 and 30 fps are what Velaria has been run on. 60 fps works by design (the stream is scaled to about 3360 × 3360 per frame to stay inside the encoder's budget) but is not yet verified end to end. 50 fps has no matching headset refresh rate and will judder; a 50p timeline is best reviewed with a 25p copy. Apple Immersive's 90 fps is out of scope.
- **Sound is the monitor mix**, or the bus **Velaria Sound** is inserted on. First-order ambisonics only (what the headsets' own players decode); higher orders are heard as first order. The headset decodes it binaurally (a generic HRTF, SADIE II), so the image is not identical to Fairlight's own Binaural monitor, which uses a different HRTF set. The sound orbs are first-order beams too: sounds closer together than about 130° merge into one orb between them.
- **Audio delay follows video delay.** The headset adds the delay the mix needs to stay in step with the picture; expect a short settling period after play starts.
- **Play and stop are Resolve's.** The headset can scrub, step and jump between clips, but it cannot start or stop playback.

## Editing from the headset

- The headset changes the controls of Velaria effects, LatLong Patcher windows and PanoMap on the clips under the playhead. It does not add, move or delete anything on the timeline.
- Changes are meant for a parked playhead. On a frame with several stacked Fusion compositions, Resolve can take a second or more for each change. The strip shows the wait.
- **No ready-made 3D title templates.** Canvas Control takes your own 3D scene.

## Network

- **On Wi-Fi the picture travels unencrypted on your local network**, and there is no pairing step. Someone on the same network who has Velaria View could watch your timeline. This is true whenever Resolve is playing out to Velaria, even if your own headset is on USB. Use a network you trust.
- Velaria sends nothing to us or to anyone else over the internet. See the privacy statement in the download.

## Other apps

Apps that look for capture cards, such as Zoom and OBS, may list a "Velaria (Quest)" device. They cannot capture from it and it does no harm.

## Premiere Pro and After Effects

Monitoring from Adobe apps through NDI is in the installer as an experiment (`--ndi`). It is a monitor only and is not covered by support.

## Support

Email only, answered when we can. If Velaria will not install on your setup, ask for a refund within 14 days.

## What we are working on

No dates and no promises. Do not buy Velaria for something on this list.

- Running beside Blackmagic Desktop Video, so both appear in Resolve.
- A dome preview for fulldome and planetarium work.
