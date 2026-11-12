#!/bin/bash
# Assembles the release folder: ~/VRLink/release/Velaria-<version>/
set -euo pipefail
cd "$(dirname "$0")"
VER=${1:-1.0.0}
# the version becomes part of a path that is removed with rm -rf below: refuse anything that is not a plain version
case "$VER" in ""|*/*|*..*|*" "*) echo "package.sh: bad version '$VER'" >&2; exit 1;; esac
OUT=~/VRLink/release/Velaria-$VER
rm -rf "$OUT" && mkdir -p "$OUT"
(cd driver && ./build.sh >/dev/null)
ditto driver/build/DeckLinkAPI.framework "$OUT/DeckLinkAPI.framework"
cp install/install.sh install/send-logs.sh install/usb-tether.sh install/config.plist bridge/vrlink_bridge.py driver/build/vrlink_ndi "$OUT/"
echo "$VER" > "$OUT/VERSION"
if [ -f ~/VRLink/xfer/Velaria.apk ]; then cp ~/VRLink/xfer/Velaria.apk "$OUT/Velaria.apk"; else echo "WARNING: no Velaria.apk in ~/VRLink/xfer: this release has no headset app" >&2; fi
# Customer documents come from docs/public only (UNTESTED on a Mac as of 12 Nov 2026). The repository README, the
# internal setup guide and the internal changelog name development machines and internal projects and do not ship.
P=../docs/public
cp "$P/READ-ME-FIRST.md" "$OUT/READ-ME-FIRST.md"
cp "$P/setup-guide.md" "$OUT/SETUP.md"
cp "$P/support.md" "$OUT/SUPPORT.md"
cp "$P/uninstall.md" "$OUT/UNINSTALL.md"
cp "$P/known-limits.md" "$OUT/KNOWN-LIMITS.md"
cp "$P/changelog.md" "$OUT/CHANGELOG.md"
mkdir -p "$OUT/docs/ref"
cp "$P"/howto-*.md "$P/headset-reference.md" "$OUT/docs/"
cp ../quest/host/ref/*.png "$OUT/docs/ref/"
mkdir -p "$OUT/macros"
# the 3D Titles / 3D Cards (Vine, Salmon, Hay Bale) are internal: their model kits are client assets (Nelson, 2 Nov)
for f in ../macros/Velaria*.setting; do case "$f" in *"3D Title"*|*"3D Card"*) ;; *) cp "$f" "$OUT/macros/";; esac; done
cp ../macros/Skylight_UpgradeMacros.lua "$OUT/macros/"
# adb for customers without the Android SDK: Google's platform-tools (universal), trimmed to adb + licence + revision.
# Refresh with: cd ~/VRLink/vendor && curl -sSLo platform-tools-darwin.zip https://dl.google.com/android/repository/platform-tools-latest-darwin.zip && unzip -oq platform-tools-darwin.zip
if [ -x ~/VRLink/vendor/platform-tools/adb ]; then
  mkdir -p "$OUT/platform-tools"
  cp ~/VRLink/vendor/platform-tools/adb ~/VRLink/vendor/platform-tools/NOTICE.txt ~/VRLink/vendor/platform-tools/source.properties "$OUT/platform-tools/"
fi
# Legal texts (UNTESTED on a Mac as of 12 Nov 2026): EULA, privacy statement, third-party notices and the licence
# texts they refer to. These must ship: fail the build if any is missing rather than release without them.
for f in EULA.md PRIVACY.md THIRD-PARTY-NOTICES.md; do
  [ -f "../legal/$f" ] || { echo "package.sh: legal/$f is missing; not building a release without it" >&2; exit 1; }
  cp "../legal/$f" "$OUT/$f"
done
mkdir -p "$OUT/licenses" && cp ../legal/licenses/* "$OUT/licenses/"
if grep -l -E '\[(LEGAL ENTITY NAME|SUPPORT EMAIL|MAILING ADDRESS)\]|^\*\*DRAFT' "$OUT"/EULA.md "$OUT"/PRIVACY.md "$OUT"/THIRD-PARTY-NOTICES.md >/dev/null; then
  echo "WARNING: the legal texts still contain DRAFT markers or unfilled [placeholders]. Do not sell this build." >&2
fi
# fonts for titles (Montserrat, SIL OFL): installed to the user's font folder by install.sh
mkdir -p "$OUT/fonts" && cp ../fonts/Montserrat-*.ttf ../fonts/OFL-Montserrat.txt "$OUT/fonts/" 2>/dev/null || true
# The test tools (selftest, bench, probe...) are for development and are left out of a customer release.
# WITH_TOOLS=1 ./package.sh <ver> puts them back for an internal build.
if [ "${WITH_TOOLS:-0}" = 1 ]; then
  mkdir -p "$OUT/tools"
  cp driver/build/selftest driver/build/area_test driver/build/probe driver/build/e2e_source driver/build/bench "$OUT/tools/"
fi
chmod +x "$OUT"/*.sh
# Last gate before the zip: nothing internal in the folder, everything a customer needs in it.
./check_release.sh "$OUT"
(cd ~/VRLink/release && rm -f "Velaria-$VER.zip" && ditto -c -k --norsrc --keepParent "Velaria-$VER" "Velaria-$VER.zip")
echo "Release: $OUT (and Velaria-$VER.zip)"
