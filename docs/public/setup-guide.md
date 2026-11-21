# Velaria setup guide

Mac, DaVinci Resolve Studio and a Meta Quest. Allow about 20 minutes the first time. Most of that is turning on the headset's developer mode.

## What you need

- **A Mac with Apple silicon**, macOS 13 or newer. Intel Macs are not supported.
- **DaVinci Resolve Studio 19, 20 or 21.** The free Resolve has no video output and no Fusion effects on the Edit page, so it will not work.
- **A Meta Quest 3**, charged. Quest 3S, Quest 2 and Quest Pro run the app, but Quest 3 is the headset we test on.
- **A USB-C data cable** (the charging cable that came with the Quest works), or the Mac and the Quest on the same Wi-Fi network. USB gives the steadier picture.
- **A Meta account with developer mode** (free). Step 1 covers it. Meta may take a few days to verify a new developer account.
- **Apple's Command Line Tools.** Run `xcode-select --install` in Terminal if you do not have them. The installer checks and tells you.
- **The Velaria download** (`Velaria-1.0.x.zip`).

**Read this before you install if you own a Blackmagic card.** Resolve sees Velaria as a Blackmagic video device. While Velaria is installed it takes the place of Blackmagic Desktop Video, so a DeckLink or UltraStudio will not appear in Resolve. The installer will not set Blackmagic's driver aside unless you tell it to, and the uninstaller puts it back.

## 1. Headset: developer mode (once)

1. On your phone, open the **Meta Horizon** app. Go to **Menu > Devices**, pick your Quest, then **Headset settings > Developer mode**, and turn it on. If the app asks you to create a developer organization, do that first at developers.meta.com (any name will do) and come back.
2. Plug the Quest into the Mac with the USB-C cable.
3. Put the headset on. A dialog asks **Allow USB debugging?** Tick *Always allow from this computer* and allow it. If you do not see the dialog, unplug the cable and plug it in again with the headset on your head.

## 2. Mac: install Velaria (once)

1. Unzip `Velaria-1.0.x.zip` in Downloads or your home folder. Keep the folder: the installer, the uninstaller and `send-logs.sh` live in it.
2. Open **Terminal** and run the installer from inside the folder. Type `cd `, drag the folder into the Terminal window, press Return, then run the second line:

   ```
   cd ~/Downloads/Velaria-1.0.x
   sudo ./install.sh
   ```

   Enter your Mac password when asked.
3. The installer prints each thing it does: the driver, the Resolve bridge, the USB helper, the Velaria effects for Resolve, the Montserrat fonts, and the headset app if the Quest is plugged in and USB debugging is allowed.
4. If the Quest was not connected, plug it in later and run `sudo ./install.sh --headset` from the same folder.
5. **Restart Resolve** if it was open. Resolve reads its video devices and effects when it starts.

**Velaria is not signed by Apple.** On most Macs the installer runs without a warning. If macOS says *Apple could not verify [file name] is free of malware that may harm your Mac or compromise your privacy* (macOS 15 and newer) or *[file name] cannot be opened because the developer cannot be verified* (macOS 13 and 14), click Done, open **System Settings > Privacy & Security**, scroll to Security and click **Open Anyway**. Then run the installer again.

**If the installer stops with "Blackmagic Desktop Video is installed":** that is the safeguard described above. If you are happy for Velaria to take its place, run `sudo ./install.sh --replace-blackmagic`.

The download carries its own copy of Google's `adb` tool for talking to the headset. If you already have Android platform-tools, yours is used.

## 3. Resolve: point it at the headset (once per Mac)

1. **DaVinci Resolve > Preferences > Video and Audio I/O > For capture and playback use > Velaria (Quest)**. Save, and restart Resolve if it asks.
2. **Project Settings > Master Settings > Video Monitoring**: set the resolution to **7680 x 4320 Ultra HD** and the format to your timeline's frame rate.
3. **Square 360 timelines only** (over/under, for example 5760 x 5760): Resolve crops anything taller than the monitoring picture, so set **Output scaling to 3840 x 3840** in Timeline Settings while you monitor. This is also the render size, so set it back before you deliver.
4. If the panels in the headset later say the Resolve bridge isn't running, check **Preferences > System > General > External scripting using** is set to **Local**.

Pictures up to 5760 pixels wide reach the headset at full size (a 5760 x 2880 VR180 timeline, for example). Wider pictures are scaled down to 5760.

## 4. First picture

1. Put the headset on and open **Velaria View** (App Library, filter: *Unknown sources*).
2. The status line reads **Waiting to connect** and then **USB · Resolve** or **Wi-Fi · Resolve** on its own. There is no pairing and there are no addresses to type. Over Wi-Fi it finds the Mac in a few seconds.
3. Press play in Resolve. The timeline is in the headset, in step with Resolve's viewer. Scrub, park or play, and it follows.
4. **Look down** to open the panels. Look back up past the horizon to close them. Grab a panel's top bar to put it where you like. The layout is remembered.

Sound plays in the headset too: Resolve's monitor mix (Fairlight's Control Room output) comes with the picture. Play and stop are Resolve's own (the space bar); the headset can scrub and step.

**Hearing an ambisonic mix, head-tracked.** Fairlight cannot send an Ambisonics bus to any output card, so Velaria brings its own insert, **Velaria Sound** (installed with the rest; Resolve finds it on its next launch, and it is listed under **Preferences > System > Audio Plugins** if it needs ticking). On the Fairlight page, set the bus you mix on to **Ambisonics 1st order** and insert **Velaria Sound** on it. That is all: no patching. The headset's Sound chip reads **ambisonic · bus**, the bus's four channels reach the headset in sync with the picture, and turning your head keeps every sound where you panned it. On a stereo bus the same insert sends the stereo mix (chip: **in headset · bus**). The plugin changes nothing in the mix; it only copies it out.

**Seeing the sound.** With an Ambisonics bus live, a **Map** switch appears on the strip's top bar. On, a see-through orb floats a few metres out in whatever direction the field is loudest, up to eight at once, coloured from deep red (quiet) to yellow (loud); an orb follows its sound as it moves, fades when it stops, and two close sounds merge into one shape. First-order ambisonics has a wide beam, so two sounds closer together than about a third of a circle show as one orb between them: the orbs show direction, not spread. A mono or very reverberant bus shows nothing, which is correct.

## 5. Place things from the headset

- In Resolve, open the Edit page and go to **Effects > Velaria**. You get three effects: **Velaria Canvas Control** (a footage window, a flat title or your own 3D scene), **Velaria Orient 360** (re-orient a stereo 360 shot) and **Velaria Trim** (a trim pass on an adjustment clip). Each has its own how-to.
- In the headset, the strip shows three layers: **Overlays**, **Patches** and **Footage**. On Overlays, pinch a window in the picture and move it, or tap its chip on the strip and use the Selected panel.
- When you let go, Resolve renders the change and the strip reports the time: *Resolve updated in 0.4 s*.
- Every change you make in the headset is a real change to the project. Undo is Resolve's undo. Save in Resolve as usual.

## 6. When something goes wrong

**The headset tells you first.** When Resolve is set up wrong, the strip's status line shows a warning with the fix in Resolve's own menu words, for example *Timeline 5760x5760 exceeds the 7680x4320 monitor: Timeline Settings > Output scaling > 3840 x 3840*. Fix it and the line clears on its own.

- **No "Velaria (Quest)" in Video and Audio I/O.** Resolve was open during the install. Quit it and open it again. If it is still missing, run `ls /Library/Frameworks/DeckLinkAPI.framework` in Terminal. If that says *No such file*, run the installer again and keep its output.
- **The headset says Waiting to connect.** On USB, check the cable and that you allowed USB debugging with the headset on. On Wi-Fi, check the Mac and the Quest are on the same network. Guest networks keep devices apart.
- **Picture, but the panels say "The Resolve bridge isn't running on the Mac".** Run `sudo ./install.sh` again from the Velaria folder; it restarts the bridge. Then check the External scripting setting in step 3.
- **Velaria View is not in the App Library.** Set the filter to *Unknown sources*. If it is still missing, plug the Quest in and run `sudo ./install.sh --headset`.
- **Black screen in the headset, or the app will not open.** Quit the app from the Quest menu and open it again. If that fails, restart the headset.
- **Dropped frames on Wi-Fi.** Use the USB cable.
- **Anything else.** Run `./send-logs.sh` and email us the zip. See the Support page.

## 7. Premiere Pro and After Effects (experimental)

The headset's Source setting also lists Premiere / After Effects. This path is experimental in 1.0 and is not covered by support. It is a monitor only: no placement, no timeline panel.

1. Install the free **NDI Tools** from ndi.video/tools.
2. Run `sudo ./install.sh --ndi` from the Velaria folder.
3. In Premiere, open **Settings > Playback**, enable Mercury Transmit and tick the NDI device.
4. In the headset, leave **Source** on Auto.

If the headset says *Premiere / After Effects* when you want Resolve, open Settings and tap **Source** until it reads Auto.

## 8. Settings file

`~/Library/Application Support/Velaria/config.plist` holds the Mac-side settings. Restart Resolve after you change it. You will rarely need to.

| Setting | Default | What it does |
|---|---|---|
| `Layout` | `auto` | `auto`, `sbs`, `tb` or `mono`. Auto treats a 2:1 picture as side by side and a square one as over/under. |
| `Projection` | `auto` | `auto`, `180`, `360` or `flat`. |
| `Crop` | `auto` | `auto` finds the picture inside Resolve's output. `full` and `fit` override it. |
| `MaxEncodeWidth` | `5760` | The widest picture sent to the headset. Lower it if an older Mac struggles at 60 frames per second. |
| `BitrateMbps` | `0` | `0` is automatic. |
| `KeyframeIntervalSec` | `2` | Seconds between full frames in the stream. |
| `Port` | `47810` | The network port for the picture. |

## 9. Uninstall

```
cd ~/Downloads/Velaria-1.0.x
sudo ./install.sh --uninstall
```

The Uninstall page lists exactly what is removed and what stays.
