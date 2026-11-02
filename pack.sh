#!/bin/bash
# pack.sh <name> <paths...> : tar paths into a uniquely named archive under outputs; prints staged path
set -e
cd /home/claude/vrlink
name=$1; shift
n=$(date +%s%N | tail -c 7)
out=/mnt/user-data/outputs/vrlink/${name}_${n}.tgz
tar czf "$out" "$@"
echo "$out"
