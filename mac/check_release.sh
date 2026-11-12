#!/bin/bash
# Checks a staged release folder before it is zipped: nothing internal in it, everything a customer needs in it.
#   ./check_release.sh <release-folder>
# Exit 0 = clean, 1 = something must be fixed. Runs on macOS and Linux (plain grep and find only).
set -u
DIR="${1:-}"
[ -n "$DIR" ] && [ -d "$DIR" ] || { echo "usage: check_release.sh <release-folder>" >&2; exit 2; }
bad=0
fail() { echo "check_release: $*" >&2; bad=1; }

# 1. Files that must be there
for f in install.sh send-logs.sh usb-tether.sh config.plist vrlink_bridge.py VERSION READ-ME-FIRST.md SETUP.md SUPPORT.md \
         UNINSTALL.md KNOWN-LIMITS.md CHANGELOG.md EULA.md PRIVACY.md THIRD-PARTY-NOTICES.md \
         "macros/Velaria Canvas Control.setting" "macros/Velaria Orient 360.setting" "macros/Velaria Trim.setting"; do
  [ -e "$DIR/$f" ] || fail "missing: $f"
done
[ -d "$DIR/DeckLinkAPI.framework" ] || fail "missing: DeckLinkAPI.framework"
[ -f "$DIR/Velaria.apk" ] || fail "missing: Velaria.apk (the headset app)"

# 2. Files that must not be there: the internal 3D cards and titles, internal macros, test and development files
while IFS= read -r f; do fail "internal file in the release: ${f#"$DIR"/}"; done < <(
  find "$DIR" \( -iname '*3D Card*' -o -iname '*3D Title*' -o -iname '*Fact Card*' -o -iname 'Skylight_PIP*' \
    -o -iname '*.comp' -o -iname 'test_*' -o -iname '*soak*' -o -iname '*regression*' -o -iname 'roadmap*' \
    -o -iname '*audit*' -o -iname 'alpha-*' -o -iname 'release-checklist*' -o -iname 'pack.sh' -o -iname '.DS_Store' -o -iname '.git*' \) -print)

# 3. Words that must not appear in any text file a customer can open. Binary files are skipped (grep -I).
#    Internal machine and project names, the internal card names, and paths from the development machines.
WORDS='\bNYC\b|\bBruce\b|KALYX TEST|kalyx-test|Salmon|Hay ?Bale|\bVine\b|/Users/|~/VRLink|/home/claude|/Volumes/'
if out=$(grep -rIEn --exclude-dir=licenses --exclude-dir=platform-tools "$WORDS" "$DIR" 2>/dev/null); then
  fail "internal names or paths in the release:"; echo "$out" | sed "s|^$DIR/||" | cut -c1-160 >&2
fi
# 4. The documents themselves: no old product name outside the changelog's rename note, no alpha wording
if out=$(grep -rIEin --include='*.md' --exclude=CHANGELOG.md --exclude=THIRD-PARTY-NOTICES.md 'kalyx|\balpha\b|\btesters?\b' "$DIR" 2>/dev/null); then
  fail "old name or alpha wording in the documents:"; echo "$out" | sed "s|^$DIR/||" | cut -c1-160 >&2
fi
if [ -f "$DIR/CHANGELOG.md" ] && [ "$(grep -ci 'kalyx' "$DIR/CHANGELOG.md")" -gt 1 ]; then
  fail "CHANGELOG.md names Kalyx on more than one line (only the rename note may)"
fi
# 5. Placeholders that must be filled before anything is sold (reported, and fatal only with STRICT=1)
if out=$(grep -rIEn --include='*.md' '\[(SUPPORT EMAIL|LEGAL ENTITY NAME|MAILING ADDRESS|VERIFY)\]|^\*\*DRAFT|^## Open questions for' "$DIR" 2>/dev/null); then
  echo "check_release: WARNING, unfilled placeholders or DRAFT markers (do not sell this build):" >&2
  echo "$out" | sed "s|^$DIR/||" | cut -c1-110 >&2
  if [ "${STRICT:-0}" = 1 ]; then bad=1; fi
fi
[ "$bad" = 0 ] && echo "check_release: $DIR is clean"
exit "$bad"
