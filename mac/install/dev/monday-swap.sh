#!/bin/bash
# Monday 23 Nov 2026: put the weekend's driver and plugin builds into place on Bruce. Run with Resolve CLOSED:
#   sudo ~/VRLink/xfer/velaria-branch/mac/install/dev/monday-swap.sh
# Then open Resolve. What it does: coexist-dev.sh (driver framework swap, keeps Blackmagic's aside copy as it is),
# replaces /Library/Audio/Plug-Ins/VST3/VelariaSound.vst3 with build/tap's 0.1.0.2, clears quarantine, prints versions.
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
OWNER="${SUDO_USER:-$(stat -f %Su /dev/console 2>/dev/null || id -un)}"
B="/Users/$OWNER/VRLink/xfer/velaria-branch"
if pgrep -x "DaVinci Resolve" >/dev/null; then echo "Quit DaVinci Resolve first."; exit 1; fi
echo "== driver"
"$B/mac/install/dev/coexist-dev.sh"
echo "== plugin"
SRC="$B/build/tap/VST3/Release/VelariaSound.vst3"
DST="/Library/Audio/Plug-Ins/VST3/VelariaSound.vst3"
[ -d "$SRC" ] || { echo "plugin build missing at $SRC"; exit 1; }
rm -rf "$DST.prev"; [ -d "$DST" ] && mv "$DST" "$DST.prev"
ditto "$SRC" "$DST"
xattr -dr com.apple.quarantine "$DST" 2>/dev/null || true
chown -R root:wheel "$DST"
echo "plugin: $(defaults read "$DST/Contents/Info.plist" CFBundleVersion 2>/dev/null || echo '?') at $DST (previous kept as $DST.prev)"
echo "== status"
"$B/mac/install/dev/coexist-dev.sh" --status || true
echo "Done. Open Resolve; on the Fairlight page the insert should show 'Velaria Sound'; the Velaria log says 'tap: listening'."
