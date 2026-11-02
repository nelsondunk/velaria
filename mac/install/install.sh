#!/bin/bash
# VR Link installer: puts the virtual DeckLink driver where DaVinci Resolve looks for Blackmagic hardware,
# installs the USB-tether helper, and (if a Quest is plugged in) installs the headset app.
#
#   sudo ./install.sh            install / update
#   sudo ./install.sh --uninstall
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
FW_SRC="$HERE/DeckLinkAPI.framework"
FW_DST="/Library/Frameworks/DeckLinkAPI.framework"
BACKUP="/Library/Frameworks/DeckLinkAPI.framework.blackmagic-backup"
USER_NAME="${SUDO_USER:-$(id -un)}"
USER_HOME=$(eval echo "~$USER_NAME")
AGENT_ID="ca.skylightproductions.velaria.usb"
AGENT_PLIST="$USER_HOME/Library/LaunchAgents/$AGENT_ID.plist"
HELPER="$USER_HOME/Library/Application Support/Velaria/usb-tether.sh"
BRIDGE_ID="ca.skylightproductions.velaria.bridge"
BRIDGE_PLIST="$USER_HOME/Library/LaunchAgents/$BRIDGE_ID.plist"
BRIDGE="$USER_HOME/Library/Application Support/Velaria/vrlink_bridge.py"
NDI_ID="ca.skylightproductions.velaria.ndi"
NDI_PLIST="$USER_HOME/Library/LaunchAgents/$NDI_ID.plist"
NDI_BIN="$USER_HOME/Library/Application Support/Velaria/vrlink_ndi"

if [ "$(id -u)" != "0" ]; then echo "Run with sudo: sudo $0 $*"; exit 1; fi

# A 1.0.8 install (then called Kalyx / VR Link) used other paths and launchd labels. Clear them so nothing runs twice.
legacy_cleanup() {
  local uid; uid=$(id -u "$USER_NAME")
  for id in ca.skylightproductions.vrlink.usb ca.skylightproductions.vrlink.bridge ca.skylightproductions.vrlink.ndi; do
    sudo -u "$USER_NAME" launchctl bootout "gui/$uid/$id" 2>/dev/null || true
    rm -f "$USER_HOME/Library/LaunchAgents/$id.plist"
  done
  if [ -d "$USER_HOME/Library/Application Support/VRLink" ]; then
    # keep the settings file (headset address, stream settings) from the old layout
    if [ -f "$USER_HOME/Library/Application Support/VRLink/config.plist" ] && ! [ -f "$USER_HOME/Library/Application Support/Velaria/config.plist" ]; then
      sudo -u "$USER_NAME" mkdir -p "$USER_HOME/Library/Application Support/Velaria"
      sudo -u "$USER_NAME" cp "$USER_HOME/Library/Application Support/VRLink/config.plist" "$USER_HOME/Library/Application Support/Velaria/config.plist"
    fi
    rm -rf "$USER_HOME/Library/Application Support/VRLink"
    echo "Removed the 1.0.8 (Kalyx) helper files."
  fi
  rm -rf "$USER_HOME/Library/Logs/VRLink"
}

if [ "${1:-}" = "--uninstall" ]; then
  legacy_cleanup
  if [ -L "$FW_DST" ] || [ -d "$FW_DST" ]; then
    if grep -q 'skylightproductions' "$FW_DST/Resources/Info.plist" 2>/dev/null || [ -L "$FW_DST" ]; then rm -rf "$FW_DST"; echo "Removed Velaria driver."; fi
  fi
  if [ -d "$BACKUP" ]; then mv "$BACKUP" "$FW_DST"; echo "Restored Blackmagic Desktop Video framework."; fi
  sudo -u "$USER_NAME" launchctl bootout "gui/$(id -u "$USER_NAME")/$AGENT_ID" 2>/dev/null || true
  sudo -u "$USER_NAME" launchctl bootout "gui/$(id -u "$USER_NAME")/$BRIDGE_ID" 2>/dev/null || true
  sudo -u "$USER_NAME" launchctl bootout "gui/$(id -u "$USER_NAME")/$NDI_ID" 2>/dev/null || true
  rm -f "$AGENT_PLIST" "$HELPER" "$BRIDGE_PLIST" "$BRIDGE" "$NDI_PLIST" "$NDI_BIN" "$USER_HOME/Library/Application Support/Velaria/VERSION"
  rm -rf "$USER_HOME/Library/Application Support/Velaria/platform-tools"
  echo "Velaria uninstalled. Restart DaVinci Resolve."
  exit 0
fi

[ -d "$FW_SRC" ] || { echo "DeckLinkAPI.framework not found next to this script. Run this from inside the unzipped Velaria folder: cd Velaria-x.y.z && sudo ./install.sh"; exit 1; }
echo "Velaria $(cat "$HERE/VERSION" 2>/dev/null || echo '(unversioned build)') installer"

# The Resolve bridge runs on Apple's python3, which on a clean Mac is only a stub until the Command Line Tools are
# installed (the stub pops a dialog and exits). Say so now instead of leaving a bridge that never starts.
if ! xcode-select -p >/dev/null 2>&1; then
  echo "Apple's Command Line Tools are not installed, and the Resolve bridge needs their python3."
  echo "Run:  xcode-select --install   (one dialog, a few minutes), then run this installer again."
  exit 1
fi

# adb: the system's own if there is one (two different adb builds fight over the server), else the copy a previous
# install placed in Application Support, else the one in this package (copied into place below).
find_adb() {
  ADB=$(command -v adb || true)
  if [ -z "$ADB" ] && [ -x /opt/homebrew/bin/adb ]; then ADB=/opt/homebrew/bin/adb; fi
  if [ -z "$ADB" ] && [ -x "$USER_HOME/Library/Application Support/Velaria/platform-tools/adb" ]; then ADB="$USER_HOME/Library/Application Support/Velaria/platform-tools/adb"; fi
  if [ -z "$ADB" ] && [ -x "$HERE/platform-tools/adb" ]; then ADB="$HERE/platform-tools/adb"; fi
  return 0   # a bare [ ] && chain as the last line made the function "fail" under set -e once adb was found (1.0.8 soak, 31 Oct)
}

# Headset only: install / update the app and leave everything on the Mac alone.
if [ "${1:-}" = "--headset" ]; then
  find_adb
  [ -n "$ADB" ] || { echo "adb not found: this package should contain platform-tools/adb (Google's Android platform tools)."; exit 1; }
  xattr -dr com.apple.quarantine "$(dirname "$ADB")" 2>/dev/null || true
  echo "Waiting for the Quest (plug it in, put it on, allow USB debugging)..."
  for i in $(seq 1 60); do sudo -u "$USER_NAME" "$ADB" get-state >/dev/null 2>&1 && break; sleep 1; done
  sudo -u "$USER_NAME" "$ADB" get-state >/dev/null 2>&1 || { echo "No headset seen in 60 s."; exit 1; }
  sudo -u "$USER_NAME" "$ADB" install -r "$HERE/Velaria.apk" && echo "Installed Velaria View on the headset." && exit 0
  exit 1
fi

if pgrep -qf "DaVinci Resolve.app/Contents/MacOS/Resolve"; then
  echo "Note: DaVinci Resolve is running. Quit and relaunch it after installing."
fi

# 1. Driver. If Blackmagic's real Desktop Video is installed, keep it safe (the two can't be active together) --
#    and only with the owner's say-so: a DeckLink / UltraStudio stops working in Resolve while Velaria is installed.
if [ -d "$FW_DST" ] && ! [ -L "$FW_DST" ] && ! grep -q 'skylightproductions' "$FW_DST/Resources/Info.plist" 2>/dev/null; then
  if [ -d "$BACKUP" ]; then echo "A Blackmagic backup already exists at $BACKUP; refusing to overwrite it."; exit 1; fi
  if [ "${1:-}" != "--replace-blackmagic" ]; then
    echo "Blackmagic Desktop Video is installed. Velaria replaces it while installed (your DeckLink / UltraStudio"
    echo "will not appear in Resolve until you uninstall Velaria, which puts it back)."
    echo "If that is fine:  sudo ./install.sh --replace-blackmagic"
    exit 1
  fi
  mv "$FW_DST" "$BACKUP"
  echo "Moved Blackmagic Desktop Video framework to $BACKUP (restored by --uninstall)."
fi
rm -rf "$FW_DST"
ditto "$FW_SRC" "$FW_DST"
chown -R root:wheel "$FW_DST"
xattr -dr com.apple.quarantine "$FW_DST" 2>/dev/null || true
codesign --verify "$FW_DST" && echo "Installed driver: $FW_DST ($(defaults read "$FW_DST/Resources/Info.plist" VRLinkBuild))"

legacy_cleanup

# 2. Default settings file (only if absent)
CFG_DIR="$USER_HOME/Library/Application Support/Velaria"
sudo -u "$USER_NAME" mkdir -p "$CFG_DIR"
if [ ! -f "$CFG_DIR/config.plist" ]; then
  sudo -u "$USER_NAME" cp "$HERE/config.plist" "$CFG_DIR/config.plist"
fi

# 3. Resolve bridge: lets the headset read the timeline and adjust Fusion macro controls (TCP 47812).
sudo -u "$USER_NAME" cp "$HERE/vrlink_bridge.py" "$BRIDGE"
if [ -f "$HERE/VERSION" ]; then sudo -u "$USER_NAME" cp "$HERE/VERSION" "$CFG_DIR/VERSION"; fi   # the bridge reports it to the headset
xattr -d com.apple.quarantine "$CFG_DIR/VERSION" "$CFG_DIR/config.plist" 2>/dev/null || true
xattr -d com.apple.quarantine "$BRIDGE" 2>/dev/null || true   # a downloaded zip leaves every file quarantined
sudo -u "$USER_NAME" mkdir -p "$USER_HOME/Library/LaunchAgents" "$USER_HOME/Library/Logs/Velaria"
cat > "$BRIDGE_PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$BRIDGE_ID</string>
  <key>ProgramArguments</key><array><string>/usr/bin/python3</string><string>$BRIDGE</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>$USER_HOME/Library/Logs/Velaria/bridge.out</string>
  <key>StandardErrorPath</key><string>$USER_HOME/Library/Logs/Velaria/bridge.out</string>
</dict></plist>
PLIST
chown "$USER_NAME" "$BRIDGE_PLIST"
sudo -u "$USER_NAME" launchctl bootout "gui/$(id -u "$USER_NAME")/$BRIDGE_ID" 2>/dev/null || true
sudo -u "$USER_NAME" launchctl bootstrap "gui/$(id -u "$USER_NAME")" "$BRIDGE_PLIST"
echo "Resolve bridge running (headset edit mode)."

# 3b. NDI source: streams Premiere / After Effects (NDI Tools' Mercury Transmit plug-in) or any NDI sender to the
#     headset on TCP 47813. Needs the NDI runtime (NDI Tools); without it the helper exits and launchd retries.
NDI_PRESENT=0
ls /Library/NDI/lib/macOS/libndi.dylib /usr/local/lib/libndi*.dylib /Applications/NDI\ Tools /Applications/NDI*Monitor*.app >/dev/null 2>&1 && NDI_PRESENT=1
if [ -f "$HERE/vrlink_ndi" ] && { [ "$NDI_PRESENT" = 1 ] || [ "${1:-}" = "--ndi" ]; }; then
  sudo -u "$USER_NAME" cp "$HERE/vrlink_ndi" "$NDI_BIN"
  chmod +x "$NDI_BIN"
  xattr -d com.apple.quarantine "$NDI_BIN" 2>/dev/null || true   # launchd would otherwise trip Gatekeeper on every retry
  cat > "$NDI_PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$NDI_ID</string>
  <key>ProgramArguments</key><array><string>$NDI_BIN</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>30</integer>
  <key>StandardOutPath</key><string>$USER_HOME/Library/Logs/Velaria/ndi.out</string>
  <key>StandardErrorPath</key><string>$USER_HOME/Library/Logs/Velaria/ndi.out</string>
</dict></plist>
PLIST
  chown "$USER_NAME" "$NDI_PLIST"
  sudo -u "$USER_NAME" launchctl bootout "gui/$(id -u "$USER_NAME")/$NDI_ID" 2>/dev/null || true
  sudo -u "$USER_NAME" launchctl bootstrap "gui/$(id -u "$USER_NAME")" "$NDI_PLIST"
  echo "NDI source running (Premiere / After Effects: Settings > Playback > Enable Mercury Transmit + NDI)."
elif [ -f "$HERE/vrlink_ndi" ]; then
  echo "Premiere / After Effects source not started: NDI Tools (free, ndi.video/tools) isn't installed. After installing it, run: sudo ./install.sh --ndi"
fi

# 4. USB tether helper: whenever a Quest is plugged in, map its localhost ports to this Mac (adb reverse).
#    adb comes with the package (Google's platform-tools, universal binary, licence in platform-tools/NOTICE.txt);
#    it is placed in Application Support so the helper and send-logs.sh find it after the download folder is gone.
if ! command -v adb >/dev/null 2>&1 && [ ! -x /opt/homebrew/bin/adb ] && [ -x "$HERE/platform-tools/adb" ]; then
  sudo -u "$USER_NAME" mkdir -p "$CFG_DIR/platform-tools"
  sudo -u "$USER_NAME" cp "$HERE/platform-tools/adb" "$HERE/platform-tools/NOTICE.txt" "$HERE/platform-tools/source.properties" "$CFG_DIR/platform-tools/" 2>/dev/null || true
  chmod +x "$CFG_DIR/platform-tools/adb"
  xattr -dr com.apple.quarantine "$CFG_DIR/platform-tools" 2>/dev/null || true
  echo "Installed adb (Android platform tools $(sed -n 's/^Pkg.Revision=//p' "$HERE/platform-tools/source.properties" 2>/dev/null)) for the headset link."
fi
find_adb
if [ -n "$ADB" ]; then
  sudo -u "$USER_NAME" cp "$HERE/usb-tether.sh" "$HELPER"
  chmod +x "$HELPER"
  xattr -d com.apple.quarantine "$HELPER" 2>/dev/null || true
  sudo -u "$USER_NAME" mkdir -p "$USER_HOME/Library/LaunchAgents"
  cat > "$AGENT_PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$AGENT_ID</string>
  <key>ProgramArguments</key><array><string>/bin/bash</string><string>$HELPER</string><string>$ADB</string></array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>$USER_HOME/Library/Logs/Velaria/usb-tether.log</string>
  <key>StandardErrorPath</key><string>$USER_HOME/Library/Logs/Velaria/usb-tether.log</string>
</dict></plist>
EOF
  chown "$USER_NAME" "$AGENT_PLIST"
  sudo -u "$USER_NAME" mkdir -p "$USER_HOME/Library/Logs/Velaria"
  sudo -u "$USER_NAME" launchctl bootout "gui/$(id -u "$USER_NAME")/$AGENT_ID" 2>/dev/null || true
  sudo -u "$USER_NAME" launchctl bootstrap "gui/$(id -u "$USER_NAME")" "$AGENT_PLIST"
  echo "USB tether helper running (adb reverse is set up automatically whenever the Quest is plugged in)."
  # 5. Headset app
  if sudo -u "$USER_NAME" "$ADB" get-state >/dev/null 2>&1 && [ -f "$HERE/Velaria.apk" ]; then
    sudo -u "$USER_NAME" "$ADB" install -r "$HERE/Velaria.apk" && echo "Installed Velaria View on the headset."
  else
    echo "No headset connected: plug it in later and run:  sudo ./install.sh --headset"
  fi
else
  echo "adb not found and none in this package. USB tether and headset install skipped; Wi-Fi still works."
fi
# 5b. Fonts the Velaria 3D Titles are set in (Montserrat, SIL Open Font License, licence beside them). Resolve reads
#     fonts at launch; a missing font silently falls back to another face, so this is not optional.
if [ -d "$HERE/fonts" ]; then
  sudo -u "$USER_NAME" mkdir -p "$USER_HOME/Library/Fonts"
  n=0
  for f in "$HERE"/fonts/*.ttf; do
    [ -f "$f" ] || continue
    if [ ! -f "$USER_HOME/Library/Fonts/$(basename "$f")" ]; then sudo -u "$USER_NAME" cp "$f" "$USER_HOME/Library/Fonts/" && n=$((n+1)); fi
  done
  xattr -dr com.apple.quarantine "$USER_HOME/Library/Fonts" 2>/dev/null || true
  [ "$n" -gt 0 ] && echo "Installed $n font file(s) for the Velaria 3D Titles (Montserrat)."
fi

# 6. Fusion library: the Velaria macros (Canvas Control, 3D Titles, 3D Cards, Orient 360) for the Fusion page and the
#    Edit page's Effects > Velaria folder, plus the upgrade script (Fusion > Scripts > Comp). Anything already there is
#    backed up beside the library with today's date. Resolve reads the library at launch.
FUSION="$USER_HOME/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion"
if [ -d "$HERE/macros" ] && [ -d "$(dirname "$FUSION")" ]; then
  STAMP=$(date +%Y-%m-%d-%H%M)
  BK="$(dirname "$FUSION")/Fusion-archive/$STAMP"
  for d in Macros Templates/Edit/Effects/Velaria Scripts/Comp; do sudo -u "$USER_NAME" mkdir -p "$FUSION/$d"; done
  for f in "$HERE"/macros/Velaria*.setting; do
    n=$(basename "$f")
    for d in Macros Templates/Edit/Effects/Velaria; do
      case "$n" in "Velaria 3D Title"*) [ "$d" = Macros ] || continue;; esac   # titles output a 3D scene: Fusion page only
      if [ -f "$FUSION/$d/$n" ] && ! cmp -s "$f" "$FUSION/$d/$n"; then
        sudo -u "$USER_NAME" mkdir -p "$BK/$d" && sudo -u "$USER_NAME" cp "$FUSION/$d/$n" "$BK/$d/$n"
      fi
      sudo -u "$USER_NAME" cp "$f" "$FUSION/$d/$n"
      xattr -d com.apple.quarantine "$FUSION/$d/$n" 2>/dev/null || true
    done
  done
  [ -f "$HERE/macros/Skylight_UpgradeMacros.lua" ] && sudo -u "$USER_NAME" cp "$HERE/macros/Skylight_UpgradeMacros.lua" "$FUSION/Scripts/Comp/"
  # the 1.0.8 library (Kalyx names) moves to the archive: comps keep their own copy of each macro, so nothing on a
  # timeline breaks; Workspace > Scripts > Comp > Skylight_UpgradeMacros swaps old instances for the Velaria ones
  moved=0
  for f in "$FUSION"/Macros/Kalyx*.setting; do
    [ -f "$f" ] || continue
    sudo -u "$USER_NAME" mkdir -p "$BK/Macros" && sudo -u "$USER_NAME" mv "$f" "$BK/Macros/" && moved=$((moved + 1))
  done
  if [ -d "$FUSION/Templates/Edit/Effects/Kalyx" ]; then
    sudo -u "$USER_NAME" mkdir -p "$BK/Templates/Edit/Effects" && sudo -u "$USER_NAME" mv "$FUSION/Templates/Edit/Effects/Kalyx" "$BK/Templates/Edit/Effects/" && moved=$((moved + 1))
  fi
  if [ "$moved" -gt 0 ]; then echo "Moved the 1.0.8 Kalyx macros to $BK (timelines keep working; run Skylight_UpgradeMacros on each comp to switch them over)."; fi
  echo "Fusion library: Velaria macros installed (Effects > Velaria on the Edit page, Macros on the Fusion page)."
fi
echo
echo "Done. Next: restart DaVinci Resolve, then Preferences > Video and Audio I/O > select 'Velaria (Quest)'."
