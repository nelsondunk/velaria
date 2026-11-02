#include "Scopes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "protocol.h"

namespace vrl {

namespace {

inline uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

// One pixel's Y, Cb, Cr on a 0..1023 scale (video range: 64..940 luma, 512 = neutral chroma).
struct YCC {
    int y, cb, cr;
};

inline bool readYCC(const uint8_t* row, int x, BMDPixelFormat pf, YCC& o) {
    switch (pf) {
        case bmdFormat10BitYUV: {   // v210: 6 pixels in 16 bytes
            const uint8_t* g = row + (x / 6) * 16;
            uint32_t w0 = rd32(g), w1 = rd32(g + 4), w2 = rd32(g + 8), w3 = rd32(g + 12);
            // components in order: Cb0 Y0 Cr0 | Y1 Cb2 Y2 | Cr2 Y3 Cb4 | Y4 Cr4 Y5
            int c[12] = {(int)(w0 & 1023), (int)((w0 >> 10) & 1023), (int)((w0 >> 20) & 1023),
                         (int)(w1 & 1023), (int)((w1 >> 10) & 1023), (int)((w1 >> 20) & 1023),
                         (int)(w2 & 1023), (int)((w2 >> 10) & 1023), (int)((w2 >> 20) & 1023),
                         (int)(w3 & 1023), (int)((w3 >> 10) & 1023), (int)((w3 >> 20) & 1023)};
            int i = x % 6;
            static const int yIdx[6] = {1, 3, 5, 7, 9, 11};
            static const int cbIdx[6] = {0, 0, 4, 4, 8, 8};
            static const int crIdx[6] = {2, 2, 6, 6, 10, 10};
            o = {c[yIdx[i]], c[cbIdx[i]], c[crIdx[i]]};
            return true;
        }
        case bmdFormat8BitYUV: {   // UYVY: Cb Y0 Cr Y1
            const uint8_t* g = row + (x / 2) * 4;
            o = {(int)g[(x & 1) ? 3 : 1] << 2, (int)g[0] << 2, (int)g[2] << 2};
            return true;
        }
        case bmdFormat8BitBGRA:
        case bmdFormat8BitARGB: {
            const uint8_t* p = row + x * 4;
            int r, g, b;
            if (pf == bmdFormat8BitBGRA) { b = p[0]; g = p[1]; r = p[2]; }
            else { r = p[1]; g = p[2]; b = p[3]; }
            // full-range RGB -> BT.709 video-range YCbCr on the 10-bit scale
            double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
            double yf = 0.2126 * rf + 0.7152 * gf + 0.0722 * bf;
            double cbf = (bf - yf) / 1.8556, crf = (rf - yf) / 1.5748;
            o = {(int)std::lround(64 + yf * 876), (int)std::lround(512 + cbf * 896), (int)std::lround(512 + crf * 896)};
            return true;
        }
        default:
            return false;
    }
}

// BT.709 video-range YCbCr -> RGB, each on the same 0..1023 video scale (64 = 0%, 940 = 100%), unclamped.
inline void toRGB(const YCC& p, double& r, double& g, double& b) {
    double y = p.y, cb = p.cb - 512.0, cr = p.cr - 512.0;
    r = y + 1.5748 * cr;
    g = y - 0.1873 * cb - 0.4681 * cr;
    b = y + 1.8556 * cb;
}

}  // namespace

bool computeScopes(const uint8_t* base, int width, int height, long rowBytes, BMDPixelFormat pf, const CropRect& crop,
                   ScopesResult& out, int targetSamples) {
    using namespace vrlproto;
    if (!base || crop.empty()) return false;
    if (pf != bmdFormat10BitYUV && pf != bmdFormat8BitYUV && pf != bmdFormat8BitBGRA && pf != bmdFormat8BitARGB)
        return false;
    const int W = kScopesW, H = kScopesH;
    // accumulators
    std::vector<uint32_t> wave(256 * 128, 0), parR(128 * 128, 0), parG(128 * 128, 0), parB(128 * 128, 0),
        vec(128 * 128, 0);
    std::vector<uint32_t> hY(256, 0), hR(128, 0), hG(128, 0), hB(128, 0);

    int step = std::max(1, (int)std::ceil(std::sqrt((double)crop.w * crop.h / std::max(1, targetSamples))));
    uint32_t samples = 0;
    auto row128 = [](double v) {   // 0..1023 -> row 127 (black) .. 0 (white), clamped
        int r = 127 - (int)std::floor(v * 128.0 / 1024.0);
        return std::max(0, std::min(127, r));
    };
    for (int y = crop.y; y < crop.y + crop.h; y += step) {
        const uint8_t* row = base + (long)y * rowBytes;
        for (int x = crop.x; x < crop.x + crop.w; x += step) {
            YCC p;
            if (!readYCC(row, x, pf, p)) return false;
            samples++;
            double fx = (double)(x - crop.x) / crop.w;   // 0..1 across the picture
            int c256 = std::min(255, (int)(fx * 256)), c128 = std::min(127, (int)(fx * 128));
            wave[row128(p.y) * 256 + c256]++;
            double r, g, b;
            toRGB(p, r, g, b);
            parR[row128(r) * 128 + c128]++;
            parG[row128(g) * 128 + c128]++;
            parB[row128(b) * 128 + c128]++;
            int vx = std::max(0, std::min(127, p.cb / 8)), vy = std::max(0, std::min(127, 127 - p.cr / 8));
            vec[vy * 128 + vx]++;
            hY[std::max(0, std::min(255, p.y / 4))]++;
            hR[std::max(0, std::min(127, (int)r / 8))]++;
            hG[std::max(0, std::min(127, (int)g / 8))]++;
            hB[std::max(0, std::min(127, (int)b / 8))]++;
        }
    }
    if (samples == 0) return false;

    out.raster.assign((size_t)W * H, 0);
    out.samples = samples;
    uint8_t* R = out.raster.data();
    // Waveform / parade intensity: a column holds (samples / columns) pixels spread over up to 128 rows; a cell
    // reads full-bright when it holds ~1/12 of the column, so ordinary textures stay translucent and flat areas
    // (bars, blacks) burn in. sqrt keeps faint traces visible.
    auto put = [&](const std::vector<uint32_t>& acc, int cols, int ox, int oy, double colSamples) {
        double cap = std::max(1.0, colSamples / 12.0);
        for (int yy = 0; yy < 128; yy++)
            for (int xx = 0; xx < cols; xx++) {
                uint32_t n = acc[yy * cols + xx];
                if (!n) continue;
                double v = std::sqrt(std::min(1.0, n / cap));
                R[(oy + yy) * W + ox + xx] = (uint8_t)std::lround(40 + 215 * v);
            }
    };
    put(wave, 256, 0, 0, (double)samples / 256);
    put(parR, 128, 256, 0, (double)samples / 128);
    put(parG, 128, 384, 0, (double)samples / 128);
    put(parB, 128, 512, 0, (double)samples / 128);
    // Vectorscope: most of a frame sits near neutral; scale so a 1/3000 share of the picture is full-bright.
    {
        double cap = std::max(1.0, samples / 3000.0);
        for (int yy = 0; yy < 128; yy++)
            for (int xx = 0; xx < 128; xx++) {
                uint32_t n = vec[yy * 128 + xx];
                if (!n) continue;
                double v = std::sqrt(std::min(1.0, n / cap));
                R[yy * W + 640 + xx] = (uint8_t)std::lround(40 + 215 * v);
            }
    }
    // Histograms: bars filled to height, tallest bin = 64 rows (log scale so a spike doesn't flatten the rest).
    auto hist = [&](const std::vector<uint32_t>& h, int bins, int ox) {
        uint32_t mx = 1;
        for (uint32_t v : h) mx = std::max(mx, v);
        double lm = std::log1p((double)mx);
        for (int i = 0; i < bins; i++) {
            if (!h[i]) continue;
            int hgt = std::max(1, (int)std::lround(64.0 * std::log1p((double)h[i]) / lm));
            for (int yy = 0; yy < hgt; yy++) R[(128 + 63 - yy) * W + ox + i] = 200;
        }
    };
    hist(hY, 256, 0);
    hist(hR, 128, 256);
    hist(hG, 128, 384);
    hist(hB, 128, 512);
    return true;
}

}  // namespace vrl
