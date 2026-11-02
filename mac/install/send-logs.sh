#!/bin/bash
# Gathers everything we need to look at a problem into one zip on the Desktop: the Mac-side logs (driver, bridge,
# USB helper, NDI), the installed versions, Resolve's own log tail, and the headset's log if a Quest is plugged in.
#   ./send-logs.sh
set -u
OUT="$HOME/Desktop/kalyx-logs-$(date +%Y%m%d-%H%M).zip"
T=$(mktemp -d /tmp/kalyx-logs.XXXX)
mkdir -p "$T/mac" "$T/quest"
cp "$HOME"/Library/Logs/VRLink/* "$T/mac/" 2>/dev/null
cp "$HOME/Library/Application Support/VRLink/config.plist" "$T/mac/" 2>/dev/null
tail -n 2000 "$HOME/Library/Application Support/Blackmagic Design/DaVinci Resolve/logs/davinci_resolve.log" > "$T/mac/davinci_resolve.tail.log" 2>/dev/null
{
  echo "date: $(date)"
  echo "macOS: $(sw_vers -productVersion) $(uname -m)"
  echo "driver: $(defaults read /Library/Frameworks/DeckLinkAPI.framework/Resources/Info.plist VRLinkBuild 2>/dev/null || echo 'not installed')"
  echo "blackmagic backup: $([ -d /Library/Frameworks/DeckLinkAPI.framework.blackmagic-backup ] && echo present || echo none)"
  echo "bridge: $(grep -m1 -o 'VERSION = .*' "$HOME/Library/Application Support/VRLink/vrlink_bridge.py" 2>/dev/null || echo '?')"
  echo "resolve: $(defaults read '/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/Info.plist' CFBundleShortVersionString 2>/dev/null || echo 'not found')"
  echo "python3: $(python3 --version 2>&1)"
  echo "clt: $(xcode-select -p 2>&1)"
  for a in ca.skylightproductions.vrlink.bridge ca.skylightproductions.vrlink.usb ca.skylightproductions.vrlink.ndi; do
    echo "agent $a: $(launchctl print gui/$(id -u)/$a 2>/dev/null | grep -E 'state =|pid =' | tr -s ' \n' ' ')"
  done
} > "$T/mac/versions.txt"
ADB=$(command -v adb || true); [ -z "$ADB" ] && [ -x /opt/homebrew/bin/adb ] && ADB=/opt/homebrew/bin/adb
[ -z "$ADB" ] && [ -x "$HOME/Library/Application Support/VRLink/platform-tools/adb" ] && ADB="$HOME/Library/Application Support/VRLink/platform-tools/adb"
[ -z "$ADB" ] && [ -x "$(dirname "$0")/platform-tools/adb" ] && ADB="$(dirname "$0")/platform-tools/adb"
if [ -n "$ADB" ] && "$ADB" get-state >/dev/null 2>&1; then
  "$ADB" shell dumpsys package ca.skylightproductions.vrlink 2>/dev/null | grep -E 'versionName|versionCode|lastUpdateTime' > "$T/quest/app.txt"
  "$ADB" logcat -d -v time > "$T/quest/logcat.txt" 2>/dev/null
  "$ADB" shell dumpsys dropbox 2>/dev/null | grep -E 'anr|crash' | tail -20 > "$T/quest/dropbox.txt"
  echo "headset: included" >> "$T/mac/versions.txt"
else
  echo "headset: not connected (plug the Quest in over USB and run this again to include its log)" >> "$T/mac/versions.txt"
fi
(cd "$T" && zip -qr "$OUT" .)
rm -rf "$T"
echo "Wrote $OUT"
open -R "$OUT" 2>/dev/null || true
