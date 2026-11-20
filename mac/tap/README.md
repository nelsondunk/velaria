# Velaria Tap (VST3)

An insert for DaVinci Resolve's Fairlight page. Audio passes through untouched; a copy of every channel, with the
host's timeline position, goes to the Velaria driver on this Mac so the headset can hear a bus Fairlight will not
patch to hardware (an Ambisonics bus, after the panner). Strictly N in / N out: a plugin with fewer outputs than the
bus starves everything after it of the missing channels (seen with a 4-in/2-out decoder, 19 Nov 2026).

Status (19 Nov 2026, overnight): skeleton. Sends UDP datagrams (`TapPacket` in `src/tap.h`) to 127.0.0.1:47813 and
writes a level line to `~/Library/Logs/Velaria/tap.log` every two seconds — enough to judge the first insert in
Fairlight without the headset. The driver does not listen on 47813 yet; that is the next step once Fairlight is
seen feeding the plugin four channels and a timeline position.

Build on the Mac (SDK cloned to `~/VRLink/sdk/vst3sdk`, submodules base, pluginterfaces, public.sdk, cmake):

    cmake -S mac/tap -B build/tap -G Ninja -Dvst3sdk_SOURCE_DIR=$HOME/VRLink/sdk/vst3sdk -DCMAKE_BUILD_TYPE=Release
    cmake --build build/tap
    sudo ditto "build/tap/VST3/Release/Velaria Tap.vst3" "/Library/Audio/Plug-Ins/VST3/Velaria Tap.vst3"

Then relaunch Resolve, tick it under Preferences › System › Audio Plugins (VST3), and drag it from Effects › VST
Effects onto the bus. Resolve scans `/Library/Audio/Plug-Ins/VST3` only (its FXScan.xml), not the per-user folder.

Licence: the VST3 SDK is GPLv3 or Steinberg's proprietary VST 3 licence (free, on signing). Velaria is closed
source, so the agreement must be signed before this plugin ships to anyone.
