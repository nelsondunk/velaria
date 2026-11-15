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
trap 'rm -rf "$W" /Library/Frameworks/DeckLinkAPI.framework /Library/Frameworks/DeckLinkAPI.framework.blackmagic-backup "/Library/Application Support/Velaria"' EXIT
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
UNDER="/Library/Application Support/Velaria/Blackmagic/DeckLinkAPI.framework"
rm -rf "$FW" "$FW.blackmagic-backup" "/Library/Application Support/Velaria" "$UH/Library/Application Support/Velaria" "$UH/Library/LaunchAgents"/ca.skylightproductions.velaria.*
mkdir -p /Library/Frameworks "$UH/Library/Application Support/Blackmagic Design/DaVinci Resolve"

echo "package"
HOME="$W/home" bash "$W/repo/mac/package.sh" 9.9.9 >"$W/pkg.out" 2>&1 || { cat "$W/pkg.out"; die "package.sh failed"; }
R="$W/home/VRLink/release/Velaria-9.9.9"
if [ -f "$R.zip" ]; then ok "zip written"; else die "zip written"; fi
if [ ! -e "$R/README.md" ] && [ ! -d "$R/tools" ]; then ok "no repository README, no test tools"; else die "no repository README, no test tools"; fi
if [ -z "$(find "$R" -name '*3D*')" ]; then ok "no 3D card or title macros"; else die "no 3D card or title macros"; fi
if HOME="$W/home" bash "$W/repo/mac/package.sh" "../x" >/dev/null 2>&1; then die "bad version accepted"; else ok "bad version refused"; fi
want=$(grep -ohE 'ref/[A-Za-z0-9_-]+\.png' "$REPO"/docs/public/*.md | sort -u | wc -l); got=$(find "$R/docs/ref" -name '*.png' | wc -l)
if [ "$want" -gt 0 ] && [ "$got" -eq "$want" ] && [ ! -e "$R/docs/ref/look-dome.png" ]; then ok "only the $want pictures the documents show"; else die "pictures in the release: $got, documents show $want"; fi
echo '[CONFIRM AT PUBLISH: something still open]' > "$R/docs/zz-placeholder.md"
if bash "$W/repo/mac/check_release.sh" "$R" 2>&1 | grep -q 'zz-placeholder.md'; then ok "checker reports a [CONFIRM ...] placeholder"; else die "checker missed a [CONFIRM ...] placeholder"; fi
rm "$R/docs/zz-placeholder.md"
cp "$REPO/macros/Velaria 3D Card - Vine.setting" "$R/macros/"
if bash "$W/repo/mac/check_release.sh" "$R" >/dev/null 2>&1; then die "checker missed an internal macro"; else ok "checker catches an internal macro"; fi
rm "$R/macros/Velaria 3D Card - Vine.setting"

echo "install"
cd "$R"
bash ./install.sh >"$W/i1.out" 2>&1 || { cat "$W/i1.out"; die "install failed"; }
if grep -q skylightproductions "$FW/Resources/Info.plist"; then ok "driver in place"; else die "driver in place"; fi
if [ -f "$UH/Library/Application Support/Velaria/vrlink_bridge.py" ] && [ -f "$UH/Library/LaunchAgents/ca.skylightproductions.velaria.bridge.plist" ]; then ok "bridge and its launch agent"; else die "bridge and its launch agent"; fi
if grep -q '<string>ca.skylightproductions.velaria.bridge</string>' "$UH/Library/LaunchAgents/ca.skylightproductions.velaria.bridge.plist" && grep -q "<string>$UH/Library/Application Support/Velaria/vrlink_bridge.py</string>" "$UH/Library/LaunchAgents/ca.skylightproductions.velaria.bridge.plist"; then ok "launch agent names the bridge and its path"; else die "launch agent names the bridge and its path"; fi
# shellcheck disable=SC2016
if SUDO_USER='x$(touch /tmp/velaria-dryrun-pwned)' bash ./install.sh >/dev/null 2>&1 || [ -e /tmp/velaria-dryrun-pwned ]; then rm -f /tmp/velaria-dryrun-pwned; die "odd user name refused"; else ok "odd user name refused"; fi
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

# Coexistence (--coexist). Stubs only: this proves where the installer moves things, not that the driver can load
# Blackmagic's framework from there.
echo "coexistence"
bm() { rm -rf "$FW"; mkdir -p "$FW/Resources"; echo "<plist>blackmagic $1</plist>" > "$FW/Resources/Info.plist"; }
is() { grep -q "$2" "$1/Resources/Info.plist" 2>/dev/null; }
if bash ./install.sh --status 2>&1 | grep -q "Mode: Blackmagic only. Velaria's driver is not installed."; then ok "--status before installing"; else die "--status before installing"; fi
if bash ./install.sh --coexist >"$W/c1.out" 2>&1 && is "$FW" skylightproductions && is "$UNDER" "blackmagic"; then ok "--coexist: Velaria in place, Blackmagic set aside for the driver"; else cat "$W/c1.out"; die "--coexist install"; fi
if [ ! -e "$FW.blackmagic-backup" ]; then ok "--coexist does not use the --replace-blackmagic backup path"; else die "--coexist left a backup at the old path"; fi
if bash ./install.sh --status 2>&1 | grep -q "Mode: coexistence"; then ok "--status reports coexistence"; else die "--status reports coexistence"; fi
if bash ./install.sh >"$W/c2.out" 2>&1 && is "$UNDER" blackmagic && grep -q "Keeping coexistence" "$W/c2.out"; then ok "plain update keeps coexistence"; else cat "$W/c2.out"; die "plain update keeps coexistence"; fi
bm v2   # Desktop Video updated: its installer overwrote Velaria's driver
if bash ./install.sh --status 2>&1 | grep -q "run the Velaria installer again"; then ok "--status after a Desktop Video update"; else die "--status after a Desktop Video update"; fi
if bash ./install.sh >/dev/null 2>&1; then die "plain install ran over an updated Desktop Video"; else ok "plain install refuses after a Desktop Video update and names --coexist"; fi
if is "$FW" "blackmagic v2"; then ok "updated Blackmagic driver untouched after the refusal"; else die "updated Blackmagic driver untouched after the refusal"; fi
if bash ./install.sh --replace-blackmagic >/dev/null 2>&1; then die "--replace-blackmagic ran over a coexistence install"; else ok "--replace-blackmagic refuses on a coexistence install"; fi
if bash ./install.sh --coexist >/dev/null 2>&1 && is "$UNDER" "blackmagic v2" && [ -d "$UNDER.previous" ] && ! is "$UNDER.previous" v2; then ok "--coexist again adopts the updated framework and keeps the older one as .previous"; else die "--coexist after a Desktop Video update"; fi
bash ./install.sh --uninstall >/dev/null 2>&1
if is "$FW" "blackmagic v2" && [ ! -e "/Library/Application Support/Velaria" ]; then ok "uninstall restores the current Blackmagic framework and leaves nothing behind"; else die "uninstall after coexistence"; fi
# uninstall when Desktop Video was updated and Velaria never put back: the new one stays, the stale copy goes
bash ./install.sh --coexist >/dev/null 2>&1; bm v3
bash ./install.sh --uninstall >/dev/null 2>&1
if is "$FW" "blackmagic v3" && [ ! -e "$FW/DeckLinkAPI.framework" ] && [ ! -e "$UNDER" ]; then ok "uninstall leaves an updated Desktop Video alone and drops the stale copy"; else die "uninstall over an updated Desktop Video"; fi
# the same situation on the old --replace-blackmagic path used to move the backup inside the new framework
rm -rf "$FW"; bm v1; bash ./install.sh --replace-blackmagic >/dev/null 2>&1; bm v4
bash ./install.sh --uninstall >/dev/null 2>&1
if is "$FW" "blackmagic v4" && [ ! -e "$FW/DeckLinkAPI.framework.blackmagic-backup" ] && [ -d "$FW.blackmagic-backup" ]; then ok "uninstall no longer moves an old backup inside a newer Blackmagic framework"; else die "uninstall with backup and newer framework"; fi
rm -rf "$FW.blackmagic-backup"
# migration from --replace-blackmagic to --coexist
bm v1; bash ./install.sh --replace-blackmagic >/dev/null 2>&1
if bash ./install.sh --coexist >/dev/null 2>&1 && is "$UNDER" "blackmagic v1" && [ ! -e "$FW.blackmagic-backup" ] && is "$FW" skylightproductions; then ok "--coexist takes over a --replace-blackmagic install"; else die "--replace-blackmagic to --coexist"; fi
bash ./install.sh --uninstall >/dev/null 2>&1
if is "$FW" "blackmagic v1"; then ok "and uninstall restores it"; else die "restore after migration"; fi
# no Desktop Video at all
rm -rf "$FW"
if bash ./install.sh --coexist >"$W/c3.out" 2>&1 && is "$FW" skylightproductions && [ ! -e "$UNDER" ] && grep -q "nothing to coexist with" "$W/c3.out"; then ok "--coexist without Desktop Video installs Velaria alone"; else cat "$W/c3.out"; die "--coexist without Desktop Video"; fi
bash ./install.sh --uninstall >/dev/null 2>&1
if [ ! -e "$FW" ]; then ok "clean after the last uninstall"; else die "clean after the last uninstall"; fi
echo; echo "$n checks passed"
