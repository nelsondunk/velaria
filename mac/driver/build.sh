#!/bin/bash
# Builds DeckLinkAPI.framework (Velaria virtual output) into ./build
set -euo pipefail
cd "$(dirname "$0")"
DEV=$(xcode-select -p)
SDK=$(xcrun --sdk macosx --show-sdk-path)
CXX=$(xcrun -f clang++)
# Build into build.tmp and swap at the end, so a Resolve launch never sees a half-written framework.
B=build.tmp
OUT=$B/DeckLinkAPI.framework
rm -rf $B && mkdir -p $OUT/Versions/A/Resources
SRCS="$(ls src/*.cpp src/*.mm 2>/dev/null || true) ../../common/timing.cpp"
FW="-framework CoreFoundation -framework CoreVideo -framework CoreMedia -framework VideoToolbox -framework IOSurface -framework SystemConfiguration"
$CXX -std=c++17 -O2 -g -arch arm64 -isysroot "$SDK" -mmacosx-version-min=13.0 \
  -fvisibility=hidden -fvisibility-inlines-hidden -Wall -Wno-unused-parameter -Wno-unused-private-field \
  -fobjc-arc -Isdk -Isrc -I../../common \
  -dynamiclib -install_name /Library/Frameworks/DeckLinkAPI.framework/Versions/A/DeckLinkAPI \
  -framework CoreFoundation -framework CoreVideo -framework CoreMedia -framework VideoToolbox \
  -framework IOSurface -framework Foundation -framework SystemConfiguration \
  $SRCS -o $OUT/Versions/A/DeckLinkAPI
sed "s/@BUILD@/$(date +%Y%m%d-%H%M%S)/" Info.plist > $OUT/Versions/A/Resources/Info.plist
ln -s A $OUT/Versions/Current
ln -s Versions/Current/DeckLinkAPI $OUT/DeckLinkAPI
ln -s Versions/Current/Resources $OUT/Resources
codesign -s - -f --timestamp=none $OUT >/dev/null
echo "Built $OUT"
nm -gU $OUT/Versions/A/DeckLinkAPI | grep -c Create
# self-test client
$CXX -std=c++17 -O2 -arch arm64 -isysroot "$SDK" -mmacosx-version-min=13.0 -Isdk tests/selftest.cpp \
  -framework CoreFoundation -o $B/selftest
echo "Built build/selftest"
# benchmark (encoder pipeline on synthetic 8K v210)
$CXX -std=c++17 -O2 -arch arm64 -isysroot "$SDK" -mmacosx-version-min=13.0 -Isdk -Isrc -I../../common \
  tests/bench.cpp src/Encoder.cpp src/ActiveArea.cpp src/Log.cpp $FW -o $B/bench
echo "Built build/bench"
# active-area tracker unit test
$CXX -std=c++17 -O2 -arch arm64 -isysroot "$SDK" -mmacosx-version-min=13.0 -Isdk -Isrc \
  tests/area_test.cpp src/ActiveArea.cpp src/Log.cpp -framework CoreFoundation -o $B/area_test
echo "Built build/area_test"
# test tools: Resolve-like source and reference receiver
$CXX -std=c++17 -O2 -arch arm64 -isysroot "$SDK" -mmacosx-version-min=13.0 -Isdk ../tools/e2e_source.cpp \
  -framework CoreFoundation -o $B/e2e_source
$CXX -std=c++17 -O2 -arch arm64 -isysroot "$SDK" -mmacosx-version-min=13.0 -I../../common ../tools/probe.cpp \
  ../../common/timing.cpp $FW -o $B/probe
echo "Built build/e2e_source build/probe"
# NDI source helper (Premiere / After Effects via NDI Tools' Mercury Transmit plug-in)
$CXX -std=c++17 -O2 -g -arch arm64 -isysroot "$SDK" -mmacosx-version-min=13.0 -Isdk -Isrc -I../../common \
  -Wall -Wno-unused-parameter ../ndi/vrlink_ndi.cpp src/Encoder.cpp src/ActiveArea.cpp src/Scopes.cpp src/Log.cpp src/Config.cpp \
  src/Server.cpp src/IIDNames.cpp ../../common/timing.cpp $FW -framework Foundation -o $B/vrlink_ndi
codesign -s - -f --timestamp=none $B/vrlink_ndi >/dev/null
echo "Built build/vrlink_ndi"
rm -rf build.old; [ -d build ] && mv build build.old; mv $B build
echo "Installed into ./build"
