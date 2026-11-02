#!/bin/bash
# Builds the Velaria View Quest APK without Gradle: NDK clang -> libvrlink.so, aapt2 link, zipalign, apksigner.
# Needs ANDROID_HOME-style layout in $ANDROID (sdk/, ndk/, openxr/ unpacked from the Khronos loader AAR).
set -euo pipefail
cd "$(dirname "$0")"
ANDROID=${ANDROID:-/home/claude/android}
NDK=$ANDROID/ndk
SDK=$ANDROID/sdk
BT=$SDK/build-tools/34.0.0
JAR=$SDK/platforms/android-34/android.jar
OXR=$ANDROID/openxr
API=32
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
CXX="$TC/aarch64-linux-android$API-clang++"
CC="$TC/aarch64-linux-android$API-clang"
GLUE=$NDK/sources/android/native_app_glue
OUT=build
# Version: ./build.sh [version] [build-number]; the number must go up for adb to call it an update
VER=${1:-${VER:-1.0.7}}
BUILD=${2:-${BUILD:-41}}
rm -rf $OUT && mkdir -p $OUT/obj $OUT/apk/lib/arm64-v8a
CFLAGS="-O2 -g -fPIC -Wall -Wno-unused-parameter -DXR_USE_PLATFORM_ANDROID -DXR_USE_GRAPHICS_API_OPENGL_ES -DXR_USE_TIMESPEC -DVELARIA_BUILD=\"$VER+$BUILD\""
$CC $CFLAGS -c $GLUE/android_native_app_glue.c -o $OUT/obj/glue.o
for f in src/*.cpp ../common/timing.cpp; do
  $CXX -std=c++17 $CFLAGS -I../common -Isrc -I$GLUE -I$OXR/prefab/modules/headers/include -c $f -o $OUT/obj/$(basename $f .cpp).o &
done
wait
$CXX -shared -o $OUT/apk/lib/arm64-v8a/libvrlink.so $OUT/obj/*.o \
  -L$OXR/jni/arm64-v8a -lopenxr_loader -landroid -llog -lEGL -lGLESv3 -lmediandk -lnativewindow \
  -u ANativeActivity_onCreate -static-libstdc++ -Wl,--no-undefined -Wl,-z,max-page-size=16384
cp $OXR/jni/arm64-v8a/libopenxr_loader.so $OUT/apk/lib/arm64-v8a/
$BT/aapt2 link -o $OUT/unsigned.apk --manifest AndroidManifest.xml -I $JAR \
  --min-sdk-version $API --target-sdk-version $API --version-code $BUILD --version-name "$VER+$BUILD"
(cd $OUT/apk && zip -q -r ../unsigned.apk lib)
$BT/zipalign -p -f 4 $OUT/unsigned.apk $OUT/aligned.apk
KS=${KEYSTORE:-$HOME/.vrlink-debug.keystore}
[ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass android -alias vrlink -keypass android \
  -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=VR Link Debug,O=Skylight Visual Media,C=CA" >/dev/null 2>&1
$BT/apksigner sign --ks "$KS" --ks-pass pass:android --key-pass pass:android --out $OUT/Velaria.apk $OUT/aligned.apk
$BT/apksigner verify $OUT/Velaria.apk && ls -la $OUT/Velaria.apk
