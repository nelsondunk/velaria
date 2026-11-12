#!/bin/bash
# Dry run of package.sh -> check_release.sh -> install.sh -> send-logs.sh -> uninstall on LINUX, with the Mac-only
# commands (ditto, codesign, launchctl, xattr, defaults, xcode-select, adb...) replaced by stubs. It proves the
# scripts' own logic: what gets staged, what gets installed where, the Blackmagic guard and restore, what is left
# after an uninstall. It proves nothing about macOS itself, the real driver, launchd or a headset.
#
# It writes to /Library/Frameworks and to root's home, so it only runs in a throwaway Linux container:
#   VELARIA_DRYRUN_OK=1 mac/install/tests/dryrun.sh
set -euo pipefail
[ "$(uname)" = Linux ] && [ "${VELARIA_DRYRUN_OK:-}" = 1 ] && [ "$(id -u)" = 0 ] || {
  echo "dryrun.sh: Linux container only, as root, with VELARIA_DRYRUN_OK=1. Never on a Mac." >&2; exit 2; }
REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
W=$(mktemp -d /tmp/velaria-dryrun.XXXX)
trap 'rm -rf "$W" /Library/Frameworks/DeckLinkAPI.framework /Library/Frameworks/DeckLinkAPI.framework.blackmagic-backup' EXIT
mkdir -p "$W/bin" "$W/home/VRLink/xfer" "$W/home/VRLink/vendor/platform-tools"
cp -R "$REPO" "$W/repo" && rm -rf "$W/repo/.git"
stub() { printf '#!/bin/bash\n%s\n' "$2" > "$W/bin/$1"; chmod +x "$W/bin/$1"; }
# shellcheck disable=SC2016
stub ditto 'if [ "$1" = "-c" ]; then src="${*: -2:1}"; out="${*: -1}"; exec zip -qr "$out" "$src"; fi; rm -rf "$2"; cp -R "$1" "$2"'
for c in codesign xattr launchctl pgrep open chown; do stub "$c" 'exit 0'; done
stub xcode-select 'echo /Library/Developer/CommandLineTools'
stub defaults 'echo 20260101-000000'
stub sw_vers 'echo 15.0'
# shellcheck disable=SC2016
stub sudo 'if [ "$1" = "-u" ]; then shift 2; fi; exec "$@"'
# shellcheck disable=SC2016
printf '#!/bin/bash\ncase "$1" in get-state) exit "${ADB_STATE:-1}";; esac\nexit 0\n' > "$W/home/VRLink/vendor/platform-tools/adb"
chmod +x "$W/home/VRLink/vendor/platform-tools/adb"
echo notice > "$W/home/VRLink/vendor/platform-tools/NOTICE.txt"
echo "Pkg.Revision=0.0.0" > "$W/home/VRLink/vendor/platform-tools/source.properties"
printf 'fake' > "$W/home/VRLink/xfer/Velaria.apk"
cat > "$W/repo/mac/driver/build.sh" <<'B'
#!/bin/bash
mkdir -p build/DeckLinkAPI.framework/Resources
echo '<plist>ca.skylightproductions.velaria</plist>' > build/DeckLinkAPI.framework/Resources/Info.plist
for t in vrlink_ndi selftest area_test probe e2e_source bench; do echo fake > build/$t; done
B
export PATH="$W/bin:$PATH"
UH=$(eval echo "~$(id -un)")
FW=/Library/Frameworks/DeckLinkAPI.framework
n=0; ok() { n=$((n + 1)); echo "  ok   $*"; }; die() { echo "  FAIL $*" >&2; exit 1; }
rm -rf "$FW" "$FW.blackmagic-backup" "$UH/Library/Application Support/Velaria" "$UH/Library/LaunchAgents"/ca.skylightproductions.velaria.*
mkdir -p /Library/Frameworks "$UH/Library/Application Support/Blackmagic Design/DaVinci Resolve"

echo "package"
HOME="$W/home" bash "$W/repo/mac/package.sh" 9.9.9 >"$W/pkg.out" 2>&1 || { cat "$W/pkg.out"; die "package.sh failed"; }
R="$W/home/VRLink/release/Velaria-9.9.9"
if [ -f "$R.zip" ]; then ok "zip written"; else die "zip written"; fi
if [ ! -e "$R/README.md" ] && [ ! -d "$R/tools" ]; then ok "no repository README, no test tools"; else die "no repository README, no test tools"; fi
if [ -z "$(find "$R" -name '*3D*')" ]; then ok "no 3D card or title macros"; else die "no 3D card or title macros"; fi
if HOME="$W/home" bash "$W/repo/mac/package.sh" "../x" >/dev/null 2>&1; then die "bad version accepted"; else ok "bad version refused"; fi
cp "$REPO/macros/Velaria 3D Card - Vine.setting" "$R/macros/"
if bash "$W/repo/mac/check_release.sh" "$R" >/dev/null 2>&1; then die "checker missed an internal macro"; else ok "checker catches an internal macro"; fi
rm "$R/macros/Velaria 3D Card - Vine.setting"

echo "install"
cd "$R"
bash ./install.sh >"$W/i1.out" 2>&1 || { cat "$W/i1.out"; die "install failed"; }
if grep -q skylightproductions "$FW/Resources/Info.plist"; then ok "driver in place"; else die "driver in place"; fi
if [ -f "$UH/Library/Application Support/Velaria/vrlink_bridge.py" ] && [ -f "$UH/Library/LaunchAgents/ca.skylightproductions.velaria.bridge.plist" ]; then ok "bridge and its launch agent"; else die "bridge and its launch agent"; fi
if [ -f "$UH/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Templates/Edit/Effects/Velaria/Velaria Trim.setting" ]; then ok "effects on the Edit page"; else die "effects on the Edit page"; fi
if bash ./install.sh >/dev/null 2>&1; then ok "second run (update) succeeds"; else die "second run (update) succeeds"; fi
if ADB_STATE=0 bash ./install.sh --headset >/dev/null 2>&1; then ok "--headset with a headset"; else die "--headset with a headset"; fi
ADB_STATE=1 bash ./install.sh --headset >/dev/null 2>&1 & pid=$!
sleep 2; if kill "$pid" 2>/dev/null; then ok "--headset waits when there is none"; else die "--headset did not wait for a headset"; fi
mkdir -p "$UH/Desktop"
HOME="$UH" bash ./send-logs.sh >/dev/null 2>&1; z=$(find "$UH/Desktop" -name 'velaria-logs-*.zip' 2>/dev/null | head -1)
if [ -n "$z" ] && unzip -l "$z" | grep -q versions.txt; then ok "send-logs writes a zip with versions.txt"; else die "send-logs writes a zip with versions.txt"; fi; rm -f "$z"

echo "uninstall"
bash ./install.sh --uninstall >/dev/null 2>&1
if [ ! -e "$FW" ] && [ ! -e "$UH/Library/Application Support/Velaria/vrlink_bridge.py" ] && [ ! -e "$UH/Library/LaunchAgents/ca.skylightproductions.velaria.bridge.plist" ]; then ok "driver, bridge and agents gone"; else die "driver, bridge and agents gone"; fi
if [ -f "$UH/Library/Application Support/Velaria/config.plist" ]; then ok "settings file kept"; else die "settings file kept"; fi

echo "Blackmagic guard"
mkdir -p "$FW/Resources"; echo '<plist>blackmagic</plist>' > "$FW/Resources/Info.plist"
if bash ./install.sh >/dev/null 2>&1; then die "installed over Blackmagic without the flag"; else ok "refuses without --replace-blackmagic"; fi
if grep -q blackmagic "$FW/Resources/Info.plist"; then ok "Blackmagic driver untouched after the refusal"; else die "Blackmagic driver untouched after the refusal"; fi
if bash ./install.sh --replace-blackmagic >/dev/null 2>&1 && [ -d "$FW.blackmagic-backup" ]; then ok "set aside with the flag"; else die "set aside with the flag"; fi
bash ./install.sh --uninstall >/dev/null 2>&1
if grep -q blackmagic "$FW/Resources/Info.plist" && [ ! -e "$FW.blackmagic-backup" ]; then ok "uninstall restores it"; else die "uninstall restores it"; fi
echo; echo "$n checks passed"
