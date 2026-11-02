# Velaria alpha — setup guide

> Name: renamed from Kalyx to Velaria on 31 Oct 2026. Code, macro, driver, folder and Notion/Design names that still say Kalyx are left as they are until the rename lands in the build.


For alpha testers. Mac + DaVinci Resolve Studio + Meta Quest 3. About 20 minutes the first time; most of it is the headset's developer mode. If anything here doesn't match what you see, that is a bug in the guide as much as in the software — tell us.

## What you need

- **Mac** (Apple silicon or Intel, macOS 13 or newer) with **DaVinci Resolve Studio 19 or 20/21**. The free Resolve has no Fusion macros on the Edit page and no video I/O, so it won't work.
- **Meta Quest 3** (Quest 2 and Quest Pro run it; Quest 3 is what we test on), charged, on the same Wi-Fi as the Mac or plugged into it over USB-C. USB is the better experience.
- **A Meta account with developer mode** (free). Steps below.
- The **Velaria alpha zip** we sent you (`VR-Link-1.0.x.zip`).

**One thing to know before you install:** Velaria works by being a *software Blackmagic card*. Resolve plays out to it exactly as it would to a DeckLink or UltraStudio. That means Velaria and Blackmagic's own Desktop Video driver can't be active at the same time in this alpha. If you have a real Blackmagic card, the installer sets its driver aside and the uninstaller puts it back; you can switch by running the uninstaller. Both drivers at once is on the roadmap.

## 1. Headset: developer mode (once)

1. On your phone, open the **Meta Horizon** app → **Menu** → **Devices** → your Quest → **Headset settings** → **Developer mode** → on. If the app asks you to create or join a developer organisation, do that first (developers.meta.com → *Create organisation*, any name) and come back.
2. Plug the Quest into the Mac with a USB-C data cable (the charging cable that came with it works).
3. Put the headset on. A dialog says **Allow USB debugging?** — tick *Always allow from this computer* and allow it. If you don't see it, unplug and plug in again with the headset on your head.

## 2. Mac: install Velaria (once)

1. Unzip `VR-Link-1.0.x.zip` into your home folder or Downloads. Keep the folder; the installer and uninstaller live in it.
2. Open **Terminal** and run the installer from inside the folder:

   ```
   cd ~/Downloads/VR-Link-1.0.x
   sudo ./install.sh
   ```

   Enter your Mac password when asked. (The alpha isn't signed by Apple yet; the installer clears macOS's download quarantine on what it places. If macOS still shows a "cannot be opened because the developer cannot be verified" dialog, choose *Open Anyway* in System Settings ▸ Privacy & Security, then run the installer again.) The installer prints what it does: driver in place, Resolve bridge and USB helper started, Fusion library (the Velaria effects) installed, the title fonts (Montserrat) added to your user fonts, and — if the Quest is plugged in and debugging is allowed — the headset app installed too.
3. If the Quest wasn't connected, plug it in later and run `sudo ./install.sh --headset` from the same folder. (The package carries its own copy of Google's `adb`, so there is nothing else to install; if you already have Android platform-tools, yours is used.)
4. **Restart Resolve** if it was open. It reads the video I/O and the Fusion library at launch.

## 3. Resolve: point it at the headset (once per machine)

1. **DaVinci Resolve → Preferences → Video and Audio I/O → For capture and playback use → Velaria (Quest)**. Save; restart if asked.
2. **Project Settings → Master Settings → Video Monitoring**: set the monitoring resolution to **7680 × 4320 Ultra HD** and the format to your timeline's frame rate. VR180 and anything up to 8K UHD then goes to the headset 1:1.
3. **Square 360 timelines only (over/under, e.g. 5760 × 5760):** Resolve crops anything taller than the monitoring raster, so set **Output scaling to 3840 × 3840** in Timeline Settings while you monitor (and back before you deliver). Full-size square output is on the roadmap.

## 4. First picture

1. Put the headset on and open **Velaria View** (App Library → filter *Unknown sources*).
2. The status line at the top reads **Waiting to connect → USB · Resolve** on its own. No pairing, no IP addresses. Over Wi-Fi it finds the Mac in a few seconds.
3. Press play in Resolve. The timeline is in the headset, in step with Resolve's viewer. Scrub, park, play — it follows.
4. **Look down** to open the panels. Look back up past the horizon to close them. Grab a panel's top bar to put it where you like; the layout is remembered.

If the status says **Premiere / After Effects** instead of Resolve, the Source is set to Adobe from an earlier test: View panel → SOURCE → **Auto**.

## 5. Placing things from the headset

- Edit page → **Effects → Velaria**. Drop **Velaria Canvas Control** on a clip above your plate for a footage window (PIP), a flat title, or your own 3D scene (FBX/OBJ) with real depth; drop **Velaria Trim** on an adjustment clip for a trim pass you can do from the headset. The Inspector has every control; the headset has the ones you want to do by hand. (Ready-made 3D card templates are a later release — the alpha has Canvas Control's 3D Scene input.)
- In the headset, the strip shows **Overlays · Patches · Picture**. On Overlays, pinch a card in the picture and move it, or tap its name and use the Placement panel's ring (pan), arc (tilt), throttles (distance, scale) and dials. Release, and Resolve re-renders: "Resolve updated in 0.4 s".
- Undo is Resolve's undo. Every change you make in the headset is a real change to the comp.
- **Patches** (LatLong Patcher paint-outs) and **Picture** (PanoMap / Velaria Orient 360 re-orientation) live on their own layers and stay out of your way until you switch to them; lock a patch so a stray pinch can't move it.

## 6. Premiere Pro and After Effects (monitor only)

Install the free **NDI Tools** (ndi.video/tools) first, then run `sudo ./install.sh --ndi` from the Velaria folder (the installer only starts the Adobe path when NDI Tools is present). In Premiere or After Effects: **Preferences → Playback → Mercury Transmit → enable NDI**. The headset's Source on Auto finds it. You get the picture, frame-locked; placement controls for Adobe are on the roadmap.

## 7. Known limits in this alpha

- Mac only. Windows is being evaluated (same driver interface; different encoder path).
- Velaria replaces Blackmagic Desktop Video while installed (see the note at the top).
- Square 360 timelines monitor at 3840 × 3840 (set output scaling); VR180 and ≤ 8K UHD are full size.
- Hand tracking is the main input; controllers work for pointing and pressing.
- Apple Vision Pro and Pico are not targets for the alpha.

## 8. When something goes wrong

- **The headset tells you first.** When the Mac side is set up wrong, the strip's status line shows a warning with the fix in Resolve's own menu words: *Timeline 5760x5760 exceeds the 7680x4320 monitor: Timeline Settings > Output scaling > 3840 x 3840*, *Monitoring 1920x1080, timeline 5760x2880: Project Settings > Video Monitoring > 7680 x 4320*, *Monitoring at 24 fps, timeline 29.97…*, or *No picture: DaVinci Resolve > Preferences > Video and Audio I/O > Velaria (Quest)*. The same line is in `~/Library/Logs/VRLink/bridge.out`. Fix it, and the line goes away on its own.

- **No "Velaria (Quest)" in Video I/O:** Resolve was open during install. Quit and relaunch it. Still missing: `ls /Library/Frameworks/DeckLinkAPI.framework` should exist — if not, run the installer again and send us its output.
- **Headset says Waiting to connect:** check the cable and that USB debugging was allowed with the headset on; over Wi-Fi, make sure the Mac and Quest are on the same network (guest networks isolate devices). The USB helper logs to `~/Library/Logs/VRLink/usb-tether.log`.
- **Picture but no panels / "bridge isn't running":** the Resolve bridge is a small background service; `launchctl kickstart -k gui/$(id -u)/ca.skylightproductions.vrlink.bridge` restarts it. Its log is `~/Library/Logs/VRLink/bridge.out`.
- **Black screen in the headset or it won't open:** quit the app from the Quest menu and open it again; if that fails, restart the headset and tell us the time it happened.
- **Send logs:** run `./send-logs.sh` from the install folder. It zips the Mac logs, the versions, and (if the headset is plugged in) the headset's log onto your Desktop; attach that zip. The View tab in the headset also shows every version in play (app, Mac package, bridge, Resolve), so a screenshot of it answers the first three questions we'd ask.

## 9. Uninstall

```
cd ~/Downloads/VR-Link-1.0.x
sudo ./install.sh --uninstall
```

Removes the driver (and restores Blackmagic's if it was set aside), the bridge, the NDI and USB helpers. The Velaria effects stay in your Fusion library so existing projects keep working; delete `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Templates/Edit/Effects/Kalyx` and the `Kalyx *.setting` files in `…/Fusion/Macros` if you want them gone too. On the headset: Settings → Apps → Velaria View → Uninstall.
