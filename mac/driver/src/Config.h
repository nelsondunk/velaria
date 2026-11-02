#pragma once
#include <string>

namespace vrl {

// Settings live in ~/Library/Application Support/Velaria/config.plist (all keys optional).
struct Config {
    std::string layout = "auto";       // auto | sbs | tb | mono
    std::string projection = "auto";   // auto | 180 | 360 | flat
    std::string crop = "auto";         // auto (detect letterbox) | full (whole raster) | fit (layout aspect)
    int maxEncodeWidth = 5760;         // active picture wider than this is scaled down
    int maxEncodePixels = 5760 * 2880 * 2;   // also caps 360 TB (5760x5760)
    int bitrateMbps = 0;               // 0 = automatic from resolution and frame rate
    int keyframeIntervalSec = 2;
    int port = 47810;
    std::string name;                  // shown in the headset; defaults to the Mac's name
    bool trace = false;                // log-only probe mode (no streaming)
    bool streamWhenNoClient = false;   // encode even when no headset is connected (benchmarks)

    static const Config& get();        // loaded once per process
};

}  // namespace vrl
