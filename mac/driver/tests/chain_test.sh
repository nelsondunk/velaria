#!/bin/bash
# Builds and runs tests/chain_test.cpp. On Linux it uses the CoreFoundation stand-in in tests/linuxstub; on a Mac it
# uses the real headers. Either way it tests the merge logic only, against a fake Blackmagic framework.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=${TMPDIR:-/tmp}/velaria_chain_test
if [ "$(uname)" = Darwin ]; then
  xcrun clang++ -std=c++17 -O1 -g -Wall -Wno-unused-parameter -Isdk -Isrc tests/chain_test.cpp src/Chain.cpp -framework CoreFoundation -o "$OUT"
else
  ${CXX:-g++} -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-multichar -Wno-four-char-constants -pthread \
    -Itests/linuxstub -Isdk -Isrc tests/chain_test.cpp src/Chain.cpp -o "$OUT"
fi
"$OUT"
