// Dome theatre backgrounds (+68, +69): a modern dome theatre rendered in Blender from each of the five seats as a
// top/bottom stereo equirect (quest/tools/theatre_scene.py), shipped in the APK's assets and shown behind the dome
// picture in the Dome projection, so a seat reads as a seat. The dome and the drum wall are rendered at infinity,
// the seats carry the stereo (Nelson, 19 Nov). JPEG, decoded with stb_image; 6144 x 6144, so the decode runs on a
// worker thread and the render thread only uploads.
#pragma once
#include <cstdint>

struct AAssetManager;

namespace vrq {
struct TheatreImage {
    unsigned char* px = nullptr;   // RGBA8, sRGB
    int w = 0, h = 0;
    TheatreImage() = default;
    TheatreImage(const TheatreImage&) = delete;
    TheatreImage& operator=(const TheatreImage&) = delete;
    ~TheatreImage() { release(); }
    void release();
};
// Decodes theatre/<name>.jpg (e.g. "M15_seat2": venue size M, tilt 15, seat 2) into `out`; with bottomUp the rows
// are flipped into GL order (so the upload needs no second copy). Returns false, with out.px null, when the asset
// is missing or will not decode.
bool loadTheatreSeat(AAssetManager* am, const char* name, TheatreImage& out, bool bottomUp);
}
