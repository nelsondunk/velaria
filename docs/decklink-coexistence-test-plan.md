# DeckLink coexistence: test plan for the Mac

Written 13 Nov 2026. Nothing below has been run. Design: `docs/decklink-coexistence.md`. Internal document; it does not ship.

**Use the second Mac (NYC), not the build Mac.** A broken DeckLink framework there costs nothing. Quit Resolve before every install, uninstall or Desktop Video installer run.

Keep a notes file open and paste the output of every command marked *save*. After each stage run `./send-logs.sh` and keep the zip.

The one line that tells most of the story, after any Resolve launch:

```
grep coexist: ~/Library/Logs/Velaria/driver.log | tail -40
```

## Stage (a): Desktop Video installed, no hardware

Target: **Fri 27 Nov 2026**, about two hours. Needs: a build of branch `claude/week-nov-9` from the build Mac, packaged with `WITH_TOOLS=1` so `tools/selftest` is in the folder; Blackmagic Desktop Video (current release, 16.5 at the time of writing) downloaded from Blackmagic's support page.

| # | Step | Expected result | Done when |
|---|---|---|---|
| A1 | On the build Mac: `cd mac/driver && ./build.sh` | First ever compile of the new files. `Built build.tmp/DeckLinkAPI.framework`, then `43 checks passed, 0 failed` and `Built build/chain_test`. | The build finishes with no compiler error. Any error text is pasted into the notes; that is a code fix, not a test failure. |
| A2 | Build Mac, no Desktop Video: `./build/selftest $PWD/build/DeckLinkAPI.framework` and `tests/e2e.sh` | Same results as before this branch. The log has `coexist: nothing set aside … Velaria device only`. | Both pass as they did on 1.0.9. This proves the refactor did not change the default path. |
| A3 | NYC: install Desktop Video, restart if asked. Open Resolve, look at Preferences > Video and Audio I/O. Open Desktop Video Setup. | Baseline with no Velaria. Resolve probably lists no device; Desktop Video Setup probably says no device is connected. | Both screens are screenshotted. This is what "back to normal" means later. |
| A0 | NYC, before installing Velaria (*save* all four): `nm -gU /Library/Frameworks/DeckLinkAPI.framework/DeckLinkAPI \| grep -i create` ; `otool -L /Library/Frameworks/DeckLinkAPI.framework/DeckLinkAPI` ; `strings /Library/Frameworks/DeckLinkAPI.framework/DeckLinkAPI \| grep -i "DeckLinkAPI.framework\|/Library/"` ; `codesign -dv --verbose=2 /Library/Frameworks/DeckLinkAPI.framework 2>&1 \| head -12 ; ls -ld /Library/Frameworks/DeckLinkAPI.framework` | A list of every creator Blackmagic exports; the libraries it links; any hard-coded paths. | The export list is compared with the twelve names in `CoexistLoader.cpp`. **Every `Create…` export of theirs that we lack is written down, and a forwarder is added before going further.** Any string naming `/Library/Frameworks/DeckLinkAPI.framework` is noted: it is the warning sign for risk 1. |
| A4 | `sudo ./install.sh` (no flag) | Refuses: "Blackmagic Desktop Video is installed…". | Exit without changes; `./install.sh --status` says "Mode: Blackmagic only. Velaria's driver is not installed." |
| A5 | `sudo ./install.sh --coexist`, then `./install.sh --status`, then `tools/selftest` (no argument: it uses the installed framework), then the grep above | Installer prints "Moved Blackmagic Desktop Video framework (16.x) to /Library/Application Support/Velaria/Blackmagic/…". Status says "Mode: coexistence". The log shows `coexist: loaded Blackmagic's framework … version 16.x`, a `Blackmagic exports:` line, an API version line, and `reports 0 device(s) at load`. Selftest passes as in A2. | All four log lines are present and there is no `E` line containing `coexist`. **If the log says `could not load` or the census line is missing, stop: that is risk 1. Roll back and send the log.** |
| A6 | Open Resolve. Select Velaria (Quest) in Video I/O. Play a 5.6K timeline for five minutes with the headset on. | Velaria is listed, alone (no hardware). Picture in the headset as on 1.0.9. Log shows which creators Resolve asked for and `-> Velaria + Blackmagic`. | Picture is correct; the headset strip shows no dropped-frame warning that 1.0.9 did not show; the notes record the exact creator names Resolve used and whether Resolve asked for a video-conversion or ancillary object (lines ending `-> Blackmagic's`). |
| A7 | Open Desktop Video Setup. | Same as the A3 baseline. It may also list "Velaria (Quest)". | It opens without an error or crash, and the notes say whether Velaria appears in it. |
| A8 | Off switch: quit Resolve; `/usr/libexec/PlistBuddy -c "Add :Coexist bool false" ~/Library/Application\ Support/Velaria/config.plist`; open Resolve; grep. Then `PlistBuddy -c "Delete :Coexist" …` and relaunch. | Log says `coexist: off (Coexist=false in config.plist)`. Velaria still works. After deleting the key, chaining is back. | Both states seen in the log. |
| A9 | Desktop Video update: quit Resolve, run Blackmagic's Desktop Video installer again over the top. Then `./install.sh --status`; `codesign --verify /Library/Frameworks/DeckLinkAPI.framework`; open Resolve. Then quit, `sudo ./install.sh` (no flag), then `sudo ./install.sh --coexist`, status, open Resolve. | After Blackmagic's installer: status says "Mode: Blackmagic only. Desktop Video was installed or updated after Velaria…"; codesign passes; Velaria is gone from Resolve. Plain install refuses and names `--coexist`. `--coexist` prints "kept the older copy at ….previous" and Velaria is back. | Each of those five observations matches. If codesign fails after Blackmagic's installer, note it: their installer merged into our folder. |
| A10 | Quit Resolve; `sudo ./install.sh --uninstall`; status; `codesign --verify /Library/Frameworks/DeckLinkAPI.framework`; `ls "/Library/Application Support/Velaria" 2>&1`; open Resolve and Desktop Video Setup. | "Restored Blackmagic Desktop Video framework". Status: Blackmagic only, nothing set aside. Codesign passes. The Velaria folder under `/Library/Application Support` is gone. Both apps look like A3. | Screens match the A3 screenshots. |
| A10b | Optional: `--coexist` again, then run Blackmagic's **uninstaller**, open Resolve. | Velaria still listed and working; the log shows Blackmagic's framework reporting no devices or a creator returning nothing. | Velaria plays. Then `--uninstall` and reinstall Desktop Video to restore the baseline. |
| A11 | With coexistence installed: launch and quit Resolve five times; look in `~/Library/Logs/DiagnosticReports` for new Resolve reports. | No new crash report. | Zero new reports dated today. |

**Stage (a) passes when** A1, A2, A5, A6, A7, A9 and A10 pass and A0's export comparison leaves nothing unforwarded. That proves loading, forwarding and the installer. It does not prove a device works through the shim.

## Stage (b): with the UltraStudio

Target: **Fri 11 Dec 2026**, about three hours, after the device arrives. Needs: the UltraStudio, an HDMI display and cable, Desktop Video matching the device.

| # | Step | Expected result | Done when |
|---|---|---|---|
| B0 | Velaria uninstalled. Plug in the UltraStudio. Open Desktop Video Setup, then Resolve; select the UltraStudio in Video I/O; play 23.976 and 29.97 timelines with audio. | The device works the normal way. | Picture and sound on the HDMI display. The device's exact name in both apps is written down. This is the baseline for everything below. |
| B1 | Quit Resolve; `sudo ./install.sh --coexist`; open Resolve; grep. | Log census: `Blackmagic device #1 at load: <model> / <name>` and `reports 1 device(s)`. | The name in the log matches B0. |
| B2 | Resolve > Preferences > Video and Audio I/O. | **Both** "Velaria (Quest)" and the UltraStudio are in the list. | Screenshot. Note the order, and which one Resolve had selected when it opened. |
| B3 | Select the UltraStudio. Play both timelines for five minutes each. | Same as B0. | Picture and sound match B0; no error dialog; Resolve's dropped-frame indicator no worse than B0. |
| B4 | Switch to Velaria (Quest) in the same menu; play; switch back. Three round trips. | Each switch works with Resolve's own menu. | Three round trips with a picture on the right device each time. Note whether Resolve asks for a restart; that is Resolve's behaviour, but the docs need to say it. |
| B5 | Velaria selected and playing: unplug the UltraStudio; wait ten seconds; plug it back in. | Velaria keeps playing. The UltraStudio leaves and rejoins Resolve's list. | The list is correct after each change, with no Resolve restart. (This is the notification pass-through.) |
| B6 | UltraStudio selected and playing: unplug it. | Whatever Resolve did at baseline when a card vanishes; no crash. | No crash report; after replugging, playback can be started again. |
| B7 | Open Desktop Video Setup with the device plugged in. Change one harmless setting and change it back. | The device is listed and the setting sticks. | Works as in B0. Note whether "Velaria (Quest)" also shows; if it does and confuses the app, that is the cue to hide Velaria from non-Resolve hosts. **Do not run a firmware update with Velaria installed until this step has passed.** |
| B8 | Any other Blackmagic-aware app on the Mac (Media Express, Premiere with the Blackmagic plug-in, OBS). | Opens and sees the UltraStudio. | Each app tried is listed with pass or what went wrong. |
| B9 | Sleep the Mac for a minute with Resolve open, wake it, play on each device. | Both still work. | Picture on both. |
| B10 | Repeat A9 (Blackmagic's installer over the top, then `--coexist`) with the device plugged in. | As A9, and the UltraStudio works at every point. | Device works before, between and after. |
| B11 | One hour of playback on the UltraStudio, then one hour on Velaria, same session. | No drift, no drops beyond baseline, no growth in Resolve's memory beyond baseline. | Both hours complete. |
| B12 | If an older Desktop Video release that supports the device can be downloaded: install it and repeat B1 to B4. | Same results. The log's version lines show the older release. | Passes on two Desktop Video versions, or the notes say no older version was available. |
| B13 | `sudo ./install.sh --uninstall`; repeat B0. | Baseline restored. | Matches B0. |

**Coexistence can become the default, and the "either/or" wording can come off the page, when** B1 to B7, B10 and B13 pass on the second Mac and B2 to B4 pass once more on the build Mac. Until then `--coexist` stays opt-in and unadvertised.

## If Resolve stops seeing devices: rollback

Work down the list; stop at the first step that restores the A3 or B0 baseline. Quit Resolve before each.

1. **Save the evidence first:** `./send-logs.sh` (it includes `install.sh --status`).
2. **Uninstall:** `sudo ./install.sh --uninstall`, then `./install.sh --status`. Expected: "Active driver: Blackmagic Desktop Video …", nothing set aside. Open Resolve.
3. **By hand, if the installer is missing or fails.** First see what is where:
   ```
   grep -c skylightproductions /Library/Frameworks/DeckLinkAPI.framework/Resources/Info.plist
   ls -d "/Library/Application Support/Velaria/Blackmagic/"* /Library/Frameworks/DeckLinkAPI.framework.blackmagic-backup 2>/dev/null
   ```
   If the first command prints `1`, the active framework is Velaria's. Remove it and move Blackmagic's back:
   ```
   sudo rm -rf /Library/Frameworks/DeckLinkAPI.framework
   sudo mv "/Library/Application Support/Velaria/Blackmagic/DeckLinkAPI.framework" /Library/Frameworks/
   ```
   (Use the `.blackmagic-backup` path in the `mv` instead if that is the one that exists.) If the first command prints `0` or an error, do **not** delete anything: go to step 4.
4. **Reinstall Desktop Video** from Blackmagic's installer. It writes a fresh framework over whatever is there. Then `sudo ./install.sh --uninstall` to clear Velaria's helpers and any stale set-aside copy.
5. If Velaria itself is what stopped working while the card is fine, the quick isolation is the off switch from A8 (`Coexist` = false): Velaria-only, no reinstall. That tells us whether chaining is the cause.

Nothing in coexistence touches Blackmagic's driver extension, its settings or Resolve's preferences. The only thing moved is the one framework folder, so steps 2 to 4 are a complete undo.

## First run on hardware — 19 Nov 2026 (Bruce, DeckLink Mini Monitor 4K, Desktop Video 16.5, macOS 26.6.2)

Undecided; the deciding test is scheduled for the morning of 20 Nov. What the evening established (details in the project doc *Week Nov 18 - DeckLink coexistence first run*):

1. Desktop Video 16.5's installer never requested activation of the card driver extension (`com.blackmagic-design.BlackmagicIO.DExt`) on this macOS; it had to be requested by hand (`"…/Desktop Video Setup.app/Contents/MacOS/Blackmagic Desktop Video Driver Extension.app/Contents/MacOS/Blackmagic Desktop Video Driver Extension" --timeout 10 --retries 1 --extensions 1 activate`, run from the `/Applications` copy) and approved under System Settings › General › Login Items & Extensions › Driver Extensions. Blackmagic's agent later deactivated all its extensions on its own (19:11) and the sequence had to be repeated.
2. A framework swapped by **rename** is not what a relaunched Resolve loads: dyld's launch closure followed the moved Velaria framework (`lsof` showed `DeckLinkAPI-Velaria.framework` mapped in a fresh Resolve while Blackmagic's sat at the public path). Swaps must copy and delete, or be followed by a reboot. `--restore`/`--uninstall` rename Blackmagic's framework back: fix or document.
3. `tools/enum` (new): Blackmagic's framework loaded by a plain process lists 0 devices even at its public path with the driver bound and makes no IOKit/XPC connection; only Resolve's enumeration counts as evidence.
4. Blackmagic 16.5 exports `_0002` screen-preview helpers, `CreateMetalScreenPreviewHelper_0001/_0002`, `CreateVideoConversionInstance_0002/_0003`, `CreateVideoFrameAncillaryPacketsInstance_0002` and `_v15_2`, none forwarded yet; the device-listing creators are covered. No `/Library/Frameworks` string in their binary.
5. `coexist: loaded Blackmagic's framework … 16.5`, the exports line and the API version line all appeared in `driver.log` inside Resolve (A5's first three conditions); the census said 0 devices, but the card had no driver at that moment, so A5/B1 are not yet answered.
