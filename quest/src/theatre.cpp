#include "theatre.h"

#include <android/asset_manager.h>
#include <android/log.h>

#include <cstdio>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"

#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "VRLink", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VRLink", __VA_ARGS__)

namespace vrq {

bool loadTheatreSeat(AAssetManager* am, int seat, std::vector<uint8_t>& rgba, int& w, int& h) {
    if (!am || seat < 0 || seat > 4) return false;
    char name[64];
    snprintf(name, sizeof name, "theatre/seat%d.jpg", seat);
    AAsset* a = AAssetManager_open(am, name, AASSET_MODE_BUFFER);
    if (!a) {
        LOGW("theatre: no asset %s", name);
        return false;
    }
    const void* buf = AAsset_getBuffer(a);
    int len = (int)AAsset_getLength(a);
    int n = 0;
    unsigned char* px = buf && len > 0 ? stbi_load_from_memory((const unsigned char*)buf, len, &w, &h, &n, 4) : nullptr;
    AAsset_close(a);
    if (!px) {
        LOGW("theatre: %s would not decode", name);
        return false;
    }
    rgba.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    LOGI("theatre: %s %dx%d", name, w, h);
    return true;
}

}  // namespace vrq
