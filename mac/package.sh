#!/bin/bash
# Assembles the release folder: ~/VRLink/release/VR-Link-<version>/
set -euo pipefail
cd "$(dirname "$0")"
VER=${1:-1.0.0}
OUT=~/VRLink/release/VR-Link-$VER
rm -rf "$OUT" && mkdir -p "$OUT"
(cd driver && ./build.sh >/dev/null)
ditto driver/build/DeckLinkAPI.framework "$OUT/DeckLinkAPI.framework"
cp install/install.sh install/send-logs.sh install/usb-tether.sh install/config.plist bridge/vrlink_bridge.py driver/build/vrlink_ndi "$OUT/"
echo "$VER" > "$OUT/VERSION"
[ -f ~/VRLink/xfer/VRLink.apk ] && cp ~/VRLink/xfer/VRLink.apk "$OUT/VRLink.apk"
cp ../README.md "$OUT/README.md" 2>/dev/null || true
mkdir -p "$OUT/macros"
# the 3D Titles / 3D Cards (Vine, Salmon, Hay Bale) are internal: their model kits are client assets (Nelson, 2 Nov)
for f in ../macros/Kalyx*.setting; do case "$f" in *"3D Title"*|*"3D Card"*) ;; *) cp "$f" "$OUT/macros/";; esac; done
cp ../macros/Skylight_UpgradeMacros.lua "$OUT/macros/"
cp ../docs/alpha-setup-guide.md "$OUT/SETUP.md" 2>/dev/null || true
# adb for testers without the Android SDK: Google's platform-tools (universal), trimmed to adb + licence + revision.
# Refresh with: cd ~/VRLink/vendor && curl -sSLo platform-tools-darwin.zip https://dl.google.com/android/repository/platform-tools-latest-darwin.zip && unzip -oq platform-tools-darwin.zip
if [ -x ~/VRLink/vendor/platform-tools/adb ]; then
  mkdir -p "$OUT/platform-tools"
  cp ~/VRLink/vendor/platform-tools/adb ~/VRLink/vendor/platform-tools/NOTICE.txt ~/VRLink/vendor/platform-tools/source.properties "$OUT/platform-tools/"
fi
cp ../CHANGELOG.md "$OUT/CHANGELOG.md" 2>/dev/null || true
# fonts the 3D Titles use (Montserrat, SIL OFL): installed to the user's font folder by install.sh
mkdir -p "$OUT/fonts" && cp ../fonts/Montserrat-*.ttf ../fonts/OFL-Montserrat.txt "$OUT/fonts/" 2>/dev/null || true
mkdir -p "$OUT/tools"
cp driver/build/selftest driver/build/area_test driver/build/probe driver/build/e2e_source driver/build/bench "$OUT/tools/"
chmod +x "$OUT"/*.sh
(cd ~/VRLink/release && rm -f "VR-Link-$VER.zip" && ditto -c -k --norsrc --keepParent "VR-Link-$VER" "VR-Link-$VER.zip")
echo "Release: $OUT (and VR-Link-$VER.zip)"
