#include "theatre.h"

#include <android/asset_manager.h>
#include <android/log.h>

#include <cstdio>
#include <cstring>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"

#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "VRLink", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VRLink", __VA_ARGS__)

namespace vrq {

void TheatreImage::release() {
    if (px) stbi_image_free(px);
    px = nullptr;
    w = h = 0;
}

bool loadTheatreSeat(AAssetManager* am, const char* asset, TheatreImage& out, bool bottomUp) {
    out.release();
    if (!am || !asset) return false;
    char name[96];
    snprintf(name, sizeof name, "theatre/%s.jpg", asset);
    AAsset* a = AAssetManager_open(am, name, AASSET_MODE_BUFFER);
    if (!a) {
        LOGW("theatre: no asset %s", name);
        return false;
    }
    const void* buf = AAsset_getBuffer(a);
    int len = (int)AAsset_getLength(a);
    int n = 0, w = 0, h = 0;
    unsigned char* px = buf && len > 0 ? stbi_load_from_memory((const unsigned char*)buf, len, &w, &h, &n, 4) : nullptr;
    AAsset_close(a);
    if (!px) {
        LOGW("theatre: %s would not decode", name);
        return false;
    }
    if (bottomUp) {   // swap rows in place: one row of scratch instead of a second copy of the picture
        size_t row = (size_t)w * 4;
        std::vector<unsigned char> tmp(row);
        for (int y = 0; y < h / 2; y++) {
            unsigned char* a0 = px + (size_t)y * row;
            unsigned char* b0 = px + (size_t)(h - 1 - y) * row;
            memcpy(tmp.data(), a0, row);
            memcpy(a0, b0, row);
            memcpy(b0, tmp.data(), row);
        }
    }
    out.px = px;
    out.w = w;
    out.h = h;
    LOGI("theatre: %s %dx%d", name, w, h);
    return true;
}

}  // namespace vrq
