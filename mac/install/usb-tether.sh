#!/bin/bash
# Keeps `adb reverse tcp:47810 tcp:47810` in place for every connected Quest, so the headset reaches the
# virtual card over USB with no setup. Runs as a per-user LaunchAgent.
ADB="${1:-adb}"
PORTS="47810 47812 47813"   # video from the virtual card, the Resolve bridge, video from the NDI helper
while true; do
  serials=$("$ADB" devices 2>/dev/null | awk 'NR>1 && $2=="device" {print $1}')
  for s in $serials; do
    list=$("$ADB" -s "$s" reverse --list 2>/dev/null)
    for PORT in $PORTS; do
      if ! echo "$list" | grep -q "tcp:$PORT"; then
        if "$ADB" -s "$s" reverse tcp:$PORT tcp:$PORT >/dev/null 2>&1; then
          echo "$(date '+%F %T') tether up for $s on $PORT"
        fi
      fi
    done
  done
  sleep 3
done
