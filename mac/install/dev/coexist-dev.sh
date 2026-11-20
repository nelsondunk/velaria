#!/bin/bash
# Development-machine coexistence swap (framework only — no bridge, agents or macros, unlike install.sh).
#
#   sudo ./coexist-dev.sh            after Blackmagic Desktop Video is installed: set its framework aside at the
#                                    path the driver loads it from, and put the branch build in its place
#   sudo ./coexist-dev.sh --restore  put Blackmagic's framework back; Velaria disappears from Resolve
#   ./coexist-dev.sh --status        what is where (no sudo)
#
# Quit DaVinci Resolve before either swap. Written 19 Nov 2026 for the first coexistence run on Bruce; the customer
# path is install.sh --coexist, which does the same framework moves plus everything else.
set -euo pipefail

OWNER="${SUDO_USER:-$(id -un)}"
BUILD="/Users/$OWNER/VRLink/xfer/velaria-branch/mac/driver/build/DeckLinkAPI.framework"
CUR=/Library/Frameworks/DeckLinkAPI.framework
UNDER_DIR="/Library/Application Support/Velaria/Blackmagic"
UNDER="$UNDER_DIR/DeckLinkAPI.framework"

kind() {
  if [ ! -d "$1" ]; then echo absent; return; fi
  if grep -q skylightproductions "$1/Resources/Info.plist" 2>/dev/null; then echo velaria; else echo blackmagic; fi
}
ver() { defaults read "$1/Resources/Info.plist" CFBundleShortVersionString 2>/dev/null || echo "?"; }

status() {
  echo "Active   $CUR: $(kind "$CUR") $(ver "$CUR")"
  echo "Set aside $UNDER: $(kind "$UNDER") $(ver "$UNDER")"
  echo "Build    $BUILD: $(kind "$BUILD") $(ver "$BUILD")"
  if systemextensionsctl list 2>/dev/null | grep -qi blackmagic; then echo "Desktop Video extension: present"; else echo "Desktop Video extension: not listed"; fi
}

case "${1:-}" in
  --status) status; exit 0;;
  --restore)
    [ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
    pgrep -x Resolve >/dev/null && { echo "Quit DaVinci Resolve first."; exit 1; }
    [ "$(kind "$UNDER")" = blackmagic ] || { echo "nothing of Blackmagic's is set aside at $UNDER; nothing to restore"; exit 1; }
    [ "$(kind "$CUR")" = velaria ] && rm -rf "$CUR"
    [ -d "$CUR" ] && { echo "$CUR is not Velaria's; leaving it alone"; exit 1; }
    ditto "$UNDER" "$CUR" && rm -rf "$UNDER"
    echo "Restored Blackmagic's framework ($(ver "$CUR")). Velaria is out of Resolve until the next swap."
    status; exit 0;;
  "")
    [ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
    pgrep -x Resolve >/dev/null && { echo "Quit DaVinci Resolve first."; exit 1; }
    [ -d "$BUILD" ] || { echo "no build at $BUILD"; exit 1; }
    case "$(kind "$CUR")" in
      blackmagic)
        [ -d "$UNDER" ] && { echo "$UNDER already exists; refusing to overwrite it. --restore first, or move it by hand."; exit 1; }
        mkdir -p "$UNDER_DIR"
        ditto "$CUR" "$UNDER" && rm -rf "$CUR"   # copy + delete, not rename: dyld's launch cache follows a renamed file
        chown -R root:wheel "$UNDER"
        echo "Set aside Blackmagic's framework ($(ver "$UNDER")) at $UNDER.";;
      velaria)
        [ "$(kind "$UNDER")" = blackmagic ] || echo "Note: no Blackmagic framework is set aside; this is a plain Velaria refresh."
        rm -rf "$CUR";;
      absent) ;;
    esac
    ditto "$BUILD" "$CUR"
    chown -R root:wheel "$CUR"
    xattr -dr com.apple.quarantine "$CUR" 2>/dev/null || true
    echo "Installed the branch driver ($(ver "$CUR")). Relaunch DaVinci Resolve, then:  grep coexist: ~/Library/Logs/Velaria/driver.log | tail -40"
    status; exit 0;;
  *) echo "usage: sudo $0 [--restore] | $0 --status"; exit 1;;
esac
