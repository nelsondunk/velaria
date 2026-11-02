#!/bin/bash
# End-to-end test of the Mac side: Resolve-like source -> virtual card -> encoder -> TCP -> reference receiver.
# Usage: tests/e2e.sh [mode=8k23] [seconds=15] [activeW=5760] [activeH=2880] [hz=72]
set -u
cd "$(dirname "$0")/.."
MODE=${1:-8k23}; SECS=${2:-15}; AW=${3:-5760}; AH=${4:-2880}; HZ=${5:-72}
export VRLINK_PORT=47900 VRLINK_LOGFILE=/tmp/vrlink_e2e_driver.log
rm -f $VRLINK_LOGFILE
./build/probe 127.0.0.1 $((SECS+4)) $HZ 60 /tmp/vrlink_e2e_snapshot.ppm > /tmp/vrlink_e2e_probe.txt 2>&1 &
PROBE=$!
sleep 0.3
./build/e2e_source "${FRAMEWORK:-$PWD/build/DeckLinkAPI.framework}" $MODE $SECS $AW $AH 0 ${RESTARTS:-0} > /tmp/vrlink_e2e_source.txt 2>&1
wait $PROBE; RC=$?
cat /tmp/vrlink_e2e_source.txt
grep -E "stream [0-9]+:|streamed|behind|ERROR|E \[" $VRLINK_LOGFILE | sed 's/^[0-9:.]* //' | head -8
cat /tmp/vrlink_e2e_probe.txt
exit $RC
