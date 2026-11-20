#!/bin/bash
# Builds and runs tests/tap_test.cpp (TapReceiver without a socket). Linux uses the CoreFoundation stand-in.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/velaria_tap_test
if [ "$(uname)" = Darwin ]; then
  xcrun clang++ -std=c++17 -O1 -g -Wall -Wno-unused-parameter -Isdk -Isrc tests/tap_test.cpp src/Tap.cpp -framework CoreFoundation -o "$OUT"
else
  ${CXX:-g++} -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-multichar -Wno-four-char-constants -pthread \
    -Itests/linuxstub -Isdk -Isrc tests/tap_test.cpp src/Tap.cpp -o "$OUT"
fi
"$OUT"
