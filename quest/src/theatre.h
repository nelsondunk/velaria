// Dome theatre backgrounds (+68): a plain planetarium interior rendered in Blender from each of the five seats as a
// top/bottom stereo equirect (quest/tools/theatre_scene.py), shipped in the APK's assets and shown behind the dome
// picture in the Dome projection, so a seat reads as a seat (Nelson, 18 Nov: "right now it doesn't have a lot of
// context"). JPEG, decoded with stb_image.
#pragma once
#include <cstdint>
#include <vector>

struct AAssetManager;

namespace vrq {
// RGBA8, sRGB; returns false when the asset is missing or will not decode
bool loadTheatreSeat(AAssetManager* am, int seat, std::vector<uint8_t>& rgba, int& w, int& h);
}
