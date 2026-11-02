#!/bin/bash
# Compares VideoToolbox HEVC Main10 settings on real footage: speed, bitrate and SSIM/PSNR vs the reference.
# usage: enc_quality.sh ref.mov   (3840x3840 reference clip)
export PATH=/opt/homebrew/bin:$PATH
REF=${1:-/tmp/vrq/ref.mov}
cd "$(dirname "$REF")"
run() {   # name bitrate prio_speed
  local name=$1 br=$2 ps=$3
  local t0=$(python3 -c 'import time;print(time.time())')
  ffmpeg -hide_banner -loglevel error -y -i "$REF" -c:v hevc_videotoolbox -profile:v main10 -pix_fmt p010le \
    -realtime 1 -prio_speed $ps -bf 0 -g 60 -b:v $br -maxrate $(( ${br%M} * 3 / 2 ))M -bufsize ${br} -tag:v hvc1 "$name.mp4"
  local t1=$(python3 -c 'import time;print(time.time())')
  local secs=$(python3 -c "print(round($t1-$t0,2))")
  local size=$(stat -f %z "$name.mp4")
  local q=$(ffmpeg -hide_banner -i "$name.mp4" -i "$REF" -lavfi "[0:v]format=yuv420p10le[a];[1:v]format=yuv420p10le[b];[a][b]ssim;[0:v]format=yuv420p10le[c];[1:v]format=yuv420p10le[d];[c][d]psnr" -f null - 2>&1 | grep -oE 'All:[0-9.]+|average:[0-9.]+' | tr '\n' ' ')
  echo "$name: ${secs}s for 120 frames ($(python3 -c "print(round(120/$secs,1))") fps incl. ProRes decode), $(python3 -c "print(round($size*8/4.004/1e6,1))") Mbit/s, $q"
}
run bug_3M 3M 1
run fix_88M_speed 88M 1
run fix_88M_quality 88M 0
run fix_120M_quality 120M 0
