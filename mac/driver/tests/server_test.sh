#!/bin/bash
# Builds and runs tests/server_test.cpp (the headset server's socket handling). Linux: CoreFoundation stand-in from
# tests/linuxstub, built twice, with address+undefined and with thread sanitizers. Mac: the real headers, once.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/velaria_server_test
if [ "$(uname)" = Darwin ]; then
  xcrun clang++ -std=c++17 -O1 -g -Wall -Wno-unused-parameter -Isdk -Isrc -I../../common tests/server_test.cpp \
    -framework CoreFoundation -o "$OUT"
  "$OUT"
else
  for san in address,undefined thread; do
    ${CXX:-g++} -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-multichar -Wno-four-char-constants -pthread \
      -fsanitize=$san -Itests/linuxstub -Isdk -Isrc -I../../common tests/server_test.cpp -o "$OUT"
    echo "== sanitizer: $san"
    "$OUT"
  done
fi
