# Velaria: read me first

Velaria shows your DaVinci Resolve timeline in a Meta Quest headset, live and in step with Resolve's viewer. This card is the short version. The full guide is `SETUP.md` in this folder.

## Before you install

You need all four. If one is missing, Velaria will not work.

1. A Mac with Apple silicon, macOS 13 or newer.
2. DaVinci Resolve **Studio** 19, 20 or 21. The free Resolve will not work.
3. A Meta Quest 3 with **developer mode** on. (Quest 3S, Quest 2 and Quest Pro run the app; Quest 3 is the headset we test on.)
4. Apple's Command Line Tools. If you are not sure you have them, run `xcode-select --install` in Terminal.

## Install in four lines

```
cd ~/Downloads/Velaria-1.0.x
sudo ./install.sh
```

Use your folder's real name in the first line: type `cd `, then drag the folder into the Terminal window. Then restart Resolve and choose **Preferences > Video and Audio I/O > Velaria (Quest)**. Put the headset on, open **Velaria View** (App Library, filter: Unknown sources) and press play in Resolve.

## The five things that go wrong

| What you see | The fix |
|---|---|
| No **Velaria (Quest)** in Resolve's Video and Audio I/O | Resolve was open during the install. Quit Resolve and open it again. |
| The installer stops with *Blackmagic Desktop Video is installed* | Velaria takes that driver's place while it is installed. If that is fine, run `sudo ./install.sh --replace-blackmagic`. Uninstalling Velaria puts Blackmagic's driver back. |
| The installer stops with *Apple's Command Line Tools are not installed* | Run `xcode-select --install`, wait for it to finish, then run the installer again. |
| The headset says **Waiting to connect** | On USB: unplug, put the headset on, plug in again and allow USB debugging. On Wi-Fi: put the Mac and the Quest on the same network (not a guest network). |
| A square 360 timeline looks flat or cut off at the top and bottom | In Resolve: **Timeline Settings > Output scaling > 3840 x 3840** while you monitor. Set it back before you deliver. |

Velaria View is not in the App Library? The headset was not connected during the install. Plug it in and run `sudo ./install.sh --headset`.

## Still stuck

Run `./send-logs.sh` from this folder. It makes a zip on your Desktop and sends nothing by itself. Email the zip to [SUPPORT EMAIL] with one line on what you saw. `SUPPORT.md` says what is in the zip.

## Remove it

```
sudo ./install.sh --uninstall
```

This removes Velaria from the Mac and puts Blackmagic's driver back if it was set aside.
