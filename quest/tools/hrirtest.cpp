// Host check of the SH-HRIR decode kernel used in src/audio.cpp (same arithmetic, no AAudio):
//   g++ -O2 -std=c++17 -Iquest/src quest/tools/hrirtest.cpp -o /tmp/hrirtest && /tmp/hrirtest
// Prints left/right RMS for plane waves from the front, left, right, back and up, and the kernel's speed.
#include "hrir_sh1.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
using namespace vrq;
static float hr[4][kHrirTaps];
static void decode(const std::vector<float>& ambi, std::vector<float>& L, std::vector<float>& R) {
    std::vector<float> hist((size_t)4 * 2 * kHrirTaps, 0.0f);
    int pos = 0;
    size_t n = ambi.size() / 4;
    L.assign(n, 0); R.assign(n, 0);
    for (size_t i = 0; i < n; i++) {
        float acc[4];
        for (int c = 0; c < 4; c++) {
            float* hc = hist.data() + (size_t)c * 2 * kHrirTaps;
            hc[pos] = ambi[i * 4 + c]; hc[pos + kHrirTaps] = ambi[i * 4 + c];
            const float* win = hc + pos + 1; float a = 0;
            for (int k = 0; k < kHrirTaps; k++) a += hr[c][k] * win[k];
            acc[c] = a;
        }
        pos = pos + 1 == kHrirTaps ? 0 : pos + 1;
        L[i] = (acc[0] + acc[1] + acc[2] + acc[3]) * 1.6f;
        R[i] = (acc[0] - acc[1] + acc[2] + acc[3]) * 1.6f;
    }
}
static float rms(const std::vector<float>& v) { double s = 0; for (float x : v) s += x * x; return std::sqrt(s / v.size()); }
int main() {
    for (int c = 0; c < 4; c++) for (int k = 0; k < kHrirTaps; k++) hr[c][k] = kHrirSh1[c][kHrirTaps - 1 - k];
    struct { const char* name; float y, z, x; } dirs[] = {{"front", 0, 0, 1}, {"left", 1, 0, 0}, {"right", -1, 0, 0}, {"back", 0, 0, -1}, {"up", 0, 1, 0}};
    const size_t n = 48000;
    for (auto& d : dirs) {
        std::vector<float> a(n * 4), L, R;
        for (size_t i = 0; i < n; i++) {   // 1 kHz tone at 0.5 full scale, SN3D plane wave
            uint32_t h = (uint32_t)i * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
            float s = ((float)(h >> 8) / 16777216.0f * 2 - 1) * 0.6124f;   // white noise, 0.354 rms
            a[i * 4] = s; a[i * 4 + 1] = d.y * s; a[i * 4 + 2] = d.z * s; a[i * 4 + 3] = d.x * s;
        }
        decode(a, L, R);
        printf("%-6s L %.3f  R %.3f  (source 0.354 rms)\n", d.name, rms(L), rms(R));
    }
    std::vector<float> a(n * 4, 0.1f), L, R;
    auto t0 = std::chrono::steady_clock::now();
    decode(a, L, R);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("1 s of audio decoded in %.1f ms on this host\n", ms);
    return 0;
}
