// Scopes measured on the uncompressed frame Resolve (or the NDI sender) hands us: luma waveform, RGB parade,
// vectorscope and histograms, packed into one 8-bit intensity raster (layout in common/protocol.h). Measured here,
// before HEVC, the scopes read the monitoring signal itself, not the compressed copy the headset decodes.
#pragma once
#include <cstdint>
#include <vector>

#include "ActiveArea.h"
#include "DeckLinkAPI.h"

namespace vrl {

struct ScopesResult {
    std::vector<uint8_t> raster;   // kScopesW * kScopesH
    uint32_t samples = 0;
};

// Subsamples the crop so that roughly `targetSamples` pixels are measured. Supports the formats Resolve plays out
// (8/10-bit YUV 4:2:2, 8-bit BGRA/ARGB); returns false for anything else.
bool computeScopes(const uint8_t* base, int width, int height, long rowBytes, BMDPixelFormat pf, const CropRect& crop,
                   ScopesResult& out, int targetSamples = 600000);

}  // namespace vrl
