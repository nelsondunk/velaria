# Support and sending logs

Support is by email: **[SUPPORT EMAIL]**. We answer when we can. There is no phone or chat support.

If Velaria will not install or run on your setup, you can ask for a refund within 14 days of buying it.

## Before you write

1. Check the five fixes on the read-me-first card.
2. Look at the headset's status line. When Resolve is set up wrong, the fix is written there.
3. Restart Resolve, then the headset app.

## What to send

1. **One line on what you saw**, and the time it happened.
2. **A screenshot of the versions line.** In the headset: look down, Look panel, View tab, bottom right. It names the headset app, the Mac package, the bridge and Resolve.
3. **The logs zip**, made as below.

## Make the logs zip

Plug the Quest into the Mac if you can, so the headset's log is included. Then, in Terminal:

```
cd ~/Downloads/Velaria-1.0.x
./send-logs.sh
```

No `sudo` is needed. A file named `velaria-logs-<date>-<time>.zip` appears on your Desktop and Finder shows it. Attach it to your email.

**The script sends nothing.** It only makes the zip. You decide whether to email it.

## What is in the zip

| In the zip | What it is |
|---|---|
| `mac/driver.log`, `bridge.log`, `bridge.out`, `usb-tether.log`, `ndi.log` | Velaria's own logs from `~/Library/Logs/Velaria`. They record connections, timings, versions, and the names of timelines, clips and effects Velaria handled. |
| `mac/config.plist` | Velaria's settings file. |
| `mac/davinci_resolve.tail.log` | The last 2,000 lines of Resolve's own log. It can include project names, clip names and file paths. |
| `mac/versions.txt` | macOS version, Mac type, Velaria and Resolve versions, and whether Velaria's background helpers are running. |
| `quest/app.txt` | The Velaria View version on the headset. |
| `quest/logcat.txt` | The headset's system log. It can include entries from other apps on the headset. |
| `quest/dropbox.txt` | The headset's list of recent app crashes. |

You can open the zip and delete anything before you send it. The `quest` files are there only when the headset is plugged in.

## Where the logs live

If you want to look yourself:

- `~/Library/Logs/Velaria/driver.log`: the video driver inside Resolve.
- `~/Library/Logs/Velaria/bridge.log`: the bridge that lets the headset read and change the timeline.
- `~/Library/Logs/Velaria/usb-tether.log`: the USB helper.
