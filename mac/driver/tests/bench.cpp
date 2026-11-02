// Phase-0 benchmark on the real machine: crop+convert (v210 8K raster -> 10-bit 4:2:0) and HEVC Main10 encode.
// Usage: bench [encodeW] [encodeH] [fps] [frames] [bitrateMbps]
#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurface.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include "ActiveArea.h"
#include "Common.h"
#include "Encoder.h"

using namespace vrl;

static void fillV210(uint8_t* base, int w, int h, long rb, int frame, int activeX, int activeY, int activeW, int activeH) {
    // Black raster with a moving gradient picture in the active area.
    for (int y = 0; y < h; y++) {
        uint32_t* row = (uint32_t*)(base + (long)y * rb);
        for (int g = 0; g < w / 6; g++) {
            int x = g * 6;
            bool in = x >= activeX && x < activeX + activeW && y >= activeY && y < activeY + activeH;
            uint32_t Y = in ? (uint32_t)(64 + ((x + frame * 8 + y / 3) % 876)) : 64;
            uint32_t Cb = in ? (uint32_t)(512 + ((y / 8) % 200) - 100) : 512;
            uint32_t Cr = in ? (uint32_t)(512 + ((x / 16 + frame) % 200) - 100) : 512;
            row[g * 4 + 0] = Cb | (Y << 10) | (Cr << 20);
            row[g * 4 + 1] = Y | (Cb << 10) | (Y << 20);
            row[g * 4 + 2] = Cr | (Y << 10) | (Cb << 20);
            row[g * 4 + 3] = Y | (Cr << 10) | (Y << 20);
        }
    }
}

int main(int argc, char** argv) {
    int encW = argc > 1 ? atoi(argv[1]) : 5760;
    int encH = argc > 2 ? atoi(argv[2]) : 2880;
    double fps = argc > 3 ? atof(argv[3]) : 30;
    int frames = argc > 4 ? atoi(argv[4]) : 120;
    int mbps = argc > 5 ? atoi(argv[5]) : 120;
    const int W = 7680, H = 4320;
    const long rb = ((W + 47) / 48) * 128;
    int ax = (W - encW) / 2 / 6 * 6, ay = (H - encH) / 2;
    if (encW > W || encH > H) { ax = 0; ay = 0; }
    const int kRing = 4;
    std::vector<uint8_t*> rasters;
    for (int i = 0; i < kRing; i++) {
        uint8_t* p = nullptr;
        posix_memalign((void**)&p, 4096, (size_t)rb * H);
        fillV210(p, W, H, rb, i * 7, ax, ay, std::min(encW, W), std::min(encH, H));
        rasters.push_back(p);
    }
    printf("raster %dx%d v210 (%.1f MB), active %dx%d+%d+%d, encode %dx%d @ %.3f fps %d Mbit/s\n", W, H,
           rb * H / 1e6, encW, encH, ax, ay, encW, encH, fps, mbps);

    // Active area detection
    int64_t t0 = nowNs();
    CropRect r = detectActiveArea(rasters[0], W, H, rb, bmdFormat10BitYUV);
    printf("detectActiveArea: %dx%d+%d+%d in %.2f ms\n", r.w, r.h, r.x, r.y, (nowNs() - t0) / 1e6);

    FrameConverter conv;
    CropRect crop{ax, ay, std::min(encW, W), std::min(encH, H)};
    ColourInfo colour;
    std::vector<double> convMs;
    std::vector<CVPixelBufferRef> converted;
    for (int i = 0; i < frames; i++) {
        int64_t s = nowNs();
        CVPixelBufferRef pb = conv.convert(rasters[i % kRing], rb, bmdFormat10BitYUV, crop, encW, encH, colour);
        convMs.push_back((nowNs() - s) / 1e6);
        if (!pb) {
            printf("convert FAILED\n");
            return 1;
        }
        if (i < 24) converted.push_back(pb);
        else CVPixelBufferRelease(pb);
    }
    std::sort(convMs.begin(), convMs.end());
    printf("convert (zero-copy crop, VTPixelTransfer v210->x420): p50 %.2f ms  p95 %.2f ms  max %.2f ms\n",
           convMs[convMs.size() / 2], convMs[convMs.size() * 95 / 100], convMs.back());

    // Encode throughput + per-frame latency, fed in real time and flat out.
    for (int mode = 0; mode < 2; mode++) {
        HevcEncoder enc;
        std::mutex m;
        std::vector<double> lat;
        size_t bytes = 0;
        int keys = 0;
        std::atomic<int> done{0};
        bool ok = enc.open(encW, encH, fps, mbps, (int)(fps * 2), colour, [&](EncodedPacket&& p) {
            std::lock_guard<std::mutex> lk(m);
            lat.push_back(p.encodeNs / 1e6);
            bytes += p.annexB.size();
            keys += p.keyframe;
            done++;
        });
        if (!ok) {
            printf("encoder open FAILED\n");
            return 1;
        }
        int64_t start = nowNs();
        int64_t period = (int64_t)(1e9 / fps);
        for (int i = 0; i < frames; i++) {
            if (mode == 0) {   // real time pacing
                int64_t due = start + i * period;
                while (nowNs() < due) usleep(500);
            }
            enc.encode(converted[i % converted.size()], (uint64_t)i + 1, i == 0, nullptr);
        }
        enc.flush();
        double secs = (nowNs() - start) / 1e9;
        std::sort(lat.begin(), lat.end());
        printf("%s: %d frames in %.2f s (%.1f fps)  per-frame latency p50 %.1f ms p95 %.1f ms max %.1f ms  "
               "avg %.1f Mbit/s  keyframes %d  paramsets %zu B\n",
               mode == 0 ? "encode real-time" : "encode flat-out", (int)done, secs, done / secs,
               lat[lat.size() / 2], lat[lat.size() * 95 / 100], lat.back(), bytes * 8 / (frames / fps) / 1e6, keys,
               enc.parameterSets().size());
    }
    for (auto pb : converted) CVPixelBufferRelease(pb);
    for (auto p : rasters) free(p);
    return 0;
}
