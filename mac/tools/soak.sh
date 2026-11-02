#!/bin/bash
# Kalyx install soak on NYC: N × (quit Resolve → uninstall → install → relaunch Resolve → load KALYX TEST → checks).
# Runs unattended; one line per iteration in soak.tsv, details in soak.log and per-iteration install logs.
N=${1:-10}
cd ~/kalyx-test || exit 1
RQ=~/kalyx-test/rq
A="$HOME/Library/Application Support/VRLink"
FW=/Library/Frameworks/DeckLinkAPI.framework
RLOG="$HOME/Library/Application Support/Blackmagic Design/DaVinci Resolve/logs/davinci_resolve.log"
BOUT="$HOME/Library/Logs/VRLink/bridge.out"
LOG=~/kalyx-test/soak.log
TSV=~/kalyx-test/soak.tsv
UID_=$(id -u)
echo "iter	uninstall_rc	leftovers	install_rc	install_s	quarantined	ndi_loaded	adb	fonts	decklink_s	bridge_s	resolve_pid	notes" > "$TSV"
say() { echo "$(date +%H:%M:%S) $*" | tee -a "$LOG"; }
wait_for() {   # wait_for <seconds> <command...> ; returns elapsed or -1
  local max=$1; shift; local t0=$(date +%s)
  while :; do
    if "$@" >/dev/null 2>&1; then echo $(( $(date +%s) - t0 )); return 0; fi
    [ $(( $(date +%s) - t0 )) -ge "$max" ] && { echo -1; return 1; }
    sleep 1
  done
}
for i in $(seq 1 "$N"); do
  notes=""
  say "=== iteration $i"
  # 0. quit Resolve through its API (never kill), wait for the process to go. Resolve's script server serves one
  #    client: a stale rq.py from an earlier run makes every new call hang, so clear those first.
  pkill -f "rq.py" 2>/dev/null; sleep 1
  if pgrep -x Resolve >/dev/null; then
    (cd "$RQ" && python3 rq.py s_quit.py >> "$LOG" 2>&1 & QP=$!; sleep 60; kill $QP 2>/dev/null) &
    q=$(wait_for 100 bash -c '! pgrep -x Resolve >/dev/null')
    if [ "$q" = "-1" ]; then
      notes="$notes api-quit-failed;"
      osascript -e 'quit app "DaVinci Resolve"' >/dev/null 2>&1
      q2=$(wait_for 60 bash -c '! pgrep -x Resolve >/dev/null')
      [ "$q2" = "-1" ] && { notes="$notes killed-resolve;"; pkill -x Resolve; sleep 5; }
    fi
    say "resolve quit in ${q}s $notes"
  fi
  # 1. uninstall
  sudo ./install.sh --uninstall > "un.$i.log" 2>&1; un_rc=$?
  left=""
  [ -e "$FW" ] && left="${left}framework "
  [ -e "$A/vrlink_bridge.py" ] && left="${left}bridge "
  [ -e "$A/platform-tools" ] && left="${left}platform-tools "
  [ -e "$A/VERSION" ] && left="${left}VERSION "
  launchctl print "gui/$UID_/ca.skylightproductions.vrlink.bridge" >/dev/null 2>&1 && left="${left}bridge-agent "
  launchctl print "gui/$UID_/ca.skylightproductions.vrlink.usb" >/dev/null 2>&1 && left="${left}usb-agent "
  ls "$HOME/Library/LaunchAgents" 2>/dev/null | grep -q vrlink && left="${left}launchagent-plist "
  [ -z "$left" ] && left="none"
  say "uninstall rc=$un_rc leftovers=$left"
  # 2. install
  t0=$(date +%s)
  sudo ./install.sh > "in.$i.log" 2>&1; in_rc=$?
  in_s=$(( $(date +%s) - t0 ))
  quar=$(xattr -lr "$A" "$FW" 2>/dev/null | grep -c com.apple.quarantine)
  ndi=$(launchctl print "gui/$UID_/ca.skylightproductions.vrlink.ndi" >/dev/null 2>&1 && echo loaded || echo no)
  adbv=$("$A/platform-tools/adb" version 2>/dev/null | sed -n 's/^Version //p'); [ -z "$adbv" ] && adbv="missing"
  fonts=$(ls "$HOME/Library/Fonts" 2>/dev/null | grep -c Montserrat)
  say "install rc=$in_rc ${in_s}s quarantined=$quar ndi=$ndi adb=$adbv fonts=$fonts"
  # 3. relaunch Resolve, wait for the driver line, load KALYX TEST, wait for the bridge
  rpos=$(stat -f %z "$RLOG" 2>/dev/null || echo 0)   # Resolve appends to its log (rotates at 2 MB)
  bpos=$(stat -f %z "$BOUT" 2>/dev/null || echo 0)
  t1=$(date +%s)
  open -a "DaVinci Resolve"
  dl=$(wait_for 150 bash -c "p=$rpos; [ \$(stat -f %z \"$RLOG\") -lt \$p ] && p=0; tail -c +\$((p+1)) \"$RLOG\" | grep -q \"Decklink model name: 'Kalyx (Quest)'\"")
  say "decklink line after ${dl}s"
  sleep 20
  (cd "$RQ" && python3 rq.py s_reopen.py >> "$LOG" 2>&1)
  br=$(wait_for 90 bash -c "tail -c +$((bpos+1)) \"$BOUT\" | grep -q 'connected to Resolve'")
  [ "$br" != "-1" ] && br=$(( $(date +%s) - t1 ))
  say "bridge connected after ${br}s (from launch)"
  rp=$(pgrep -x Resolve | head -1)
  echo "$i	$un_rc	$left	$in_rc	$in_s	$quar	$ndi	$adbv	$fonts	$dl	$br	$rp	$notes" >> "$TSV"
done
say "soak done"
