// Unit test for active-area tracking: Resolve's letterbox must be found, but dark picture content (night scenes,
// dips to black, black zenith/nadir caps) must never shrink the area mid-playback and flip the stereo layout.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ActiveArea.h"

using namespace vrl;

static int failures = 0;
#define CHECK(cond, ...)                         \
    do {                                         \
        if (cond) {                              \
            printf("ok:   ");                    \
        } else {                                 \
            printf("FAIL: ");                    \
            failures++;                          \
        }                                        \
        printf(__VA_ARGS__);                     \
        printf("\n");                            \
    } while (0)

static const int W = 7680, H = 4320;
static const long RB = ((W + 47) / 48) * 128;

static inline void put(uint32_t* g, uint32_t Y, uint32_t Cb, uint32_t Cr) {
    g[0] = Cb | (Y << 10) | (Cr << 20);
    g[1] = Y | (Cb << 10) | (Y << 20);
    g[2] = Cr | (Y << 10) | (Cb << 20);
    g[3] = Y | (Cr << 10) | (Y << 20);
}

// raster: exact video black padding; active area (aw x ah, centred) filled by `pix(x, y)` -> Y (chroma neutral+noise)
template <typename F>
static void paint(std::vector<uint8_t>& buf, int aw, int ah, F pix) {
    int ax = ((W - aw) / 2) / 6 * 6, ay = (H - ah) / 2;
    for (int y = 0; y < H; y++) {
        uint32_t* row = (uint32_t*)(buf.data() + (long)y * RB);
        for (int gx = 0; gx < W / 6; gx++) {
            int x = gx * 6;
            if (x < ax || x >= ax + aw || y < ay || y >= ay + ah) {
                put(row + gx * 4, 64, 512, 512);
                continue;
            }
            int Y, C;
            pix(x - ax, y - ay, Y, C);
            put(row + gx * 4, (uint32_t)Y, (uint32_t)C, (uint32_t)C);
        }
    }
}

int main() {
    std::vector<uint8_t> buf((size_t)RB * H);
    ActiveAreaTracker t;
    t.configure("auto", 2.0);
    auto bright = [](int x, int y, int& Y, int& C) {
        Y = 300 + (x * 7 + y * 3) % 400;
        C = 500 + (x + y) % 20;
    };
    // 1) first picture: TB 3840x3840 centred -> found immediately
    paint(buf, 3840, 3840, bright);
    CropRect r = t.update(buf.data(), W, H, RB, bmdFormat10BitYUV, true, false);
    CHECK(r.w == 3840 && r.h == 3840 && r.x == 1920 && r.y == 240, "initial area %dx%d+%d+%d (want 3840x3840+1920+240)", r.w,
          r.h, r.x, r.y);

    // 2) dark scene during playback: top and bottom quarter of the picture near black with grain -> no change
    unsigned seed = 1;
    auto darkCaps = [&](int x, int y, int& Y, int& C) {
        if (y < 900 || y >= 3840 - 900) {
            seed = seed * 1103515245u + 12345u;
            Y = 64 + (int)((seed >> 16) % 12);   // grain around black
            C = 512 + (int)((seed >> 8) % 5) - 2;
        } else
            bright(x, y, Y, C);
    };
    paint(buf, 3840, 3840, darkCaps);
    bool changed = false;
    for (int i = 0; i < 40; i++) changed |= !(t.update(buf.data(), W, H, RB, bmdFormat10BitYUV, true, false) == r);
    CHECK(!changed, "grainy dark zenith/nadir over 40 analyses: area unchanged");

    // 3) flat black caps (graphic nadir/zenith patches) during playback: needs ~10 s of identical looks
    auto flatCaps = [&](int x, int y, int& Y, int& C) {
        if (y < 900 || y >= 3840 - 900) {
            Y = 64;
            C = 512;
        } else
            bright(x, y, Y, C);
    };
    paint(buf, 3840, 3840, flatCaps);
    changed = false;
    for (int i = 0; i < 20; i++) changed |= !(t.update(buf.data(), W, H, RB, bmdFormat10BitYUV, true, false) == r);
    CHECK(!changed, "flat black caps for 20 playback analyses (~8 s): area unchanged");
    // a brighter frame in between resets the candidate
    paint(buf, 3840, 3840, bright);
    t.update(buf.data(), W, H, RB, bmdFormat10BitYUV, true, false);

    // 4) user changes output to 2880x2880 while parked -> follows after two parked looks
    paint(buf, 2880, 2880, bright);
    CropRect a = t.update(buf.data(), W, H, RB, bmdFormat10BitYUV, true, true);
    t.update(buf.data(), W, H, RB, bmdFormat10BitYUV, true, true);
    CropRect b = t.update(buf.data(), W, H, RB, bmdFormat10BitYUV, true, true);
    CHECK(a == r && b.w == 2880 && b.h == 2880, "parked shrink to %dx%d after 3 looks (first look kept %dx%d)", b.w, b.h, a.w,
          a.h);

    // 5) back to 3840 during playback -> grows on the first analysed frame
    paint(buf, 3840, 3840, bright);
    CropRect g = t.update(buf.data(), W, H, RB, bmdFormat10BitYUV, true, false);
    CHECK(g == r, "grows back immediately to %dx%d", g.w, g.h);

    // 6) a fully black frame (dip to black) carries no information
    paint(buf, 3840, 3840, [](int, int, int& Y, int& C) {
        Y = 64;
        C = 512;
    });
    CropRect k = r;
    for (int i = 0; i < 30; i++) k = t.update(buf.data(), W, H, RB, bmdFormat10BitYUV, true, false);
    CHECK(k == r, "black frames keep the area");

    // 7) CG starfield on pure black, parked (the 28 Oct bug): bright blob 1176x2728 in the middle of a 3840 square.
    //    Exactly black around it, so it looks like pillarboxing, but it has no timeline shape -> must be ignored.
    auto starfield = [](int x, int y, int& Y, int& C) {
        bool inBlob = x >= (3840 - 1176) / 2 && x < (3840 + 1176) / 2 && y >= (3840 - 2728) / 2 && y < (3840 + 2728) / 2;
        if (inBlob && ((x / 6 + y) % 7 != 0)) {
            Y = 200 + (x + y) % 300;
            C = 540;
        } else {
            Y = 64;
            C = 512;
        }
    };
    paint(buf, 3840, 3840, starfield);
    k = r;
    for (int i = 0; i < 40; i++) k = t.update(buf.data(), W, H, RB, bmdFormat10BitYUV, true, true);
    CHECK(k == r, "parked CG frame on pure black (blob 1176x2728) keeps %dx%d", k.w, k.h);
    for (int i = 0; i < 40; i++) k = t.update(buf.data(), W, H, RB, bmdFormat10BitYUV, true, false);
    CHECK(k == r, "same during playback keeps %dx%d", k.w, k.h);

    // 8) a fresh start on that dark frame shows the full raster, then locks on as soon as a real frame arrives
    ActiveAreaTracker t2;
    t2.configure("auto", 2.0);
    CropRect f = t2.update(buf.data(), W, H, RB, bmdFormat10BitYUV, true, true);
    CHECK(f.w == W && f.h == H, "fresh start on a dark frame: full raster %dx%d", f.w, f.h);
    paint(buf, 3840, 3840, bright);
    f = t2.update(buf.data(), W, H, RB, bmdFormat10BitYUV, false, false);
    CHECK(f == r, "then locks to %dx%d on the first real frame", f.w, f.h);

    // 9) a legitimate timeline change to 16:9 UHD while parked is still followed
    paint(buf, 3840, 2160, bright);
    CropRect u;
    for (int i = 0; i < 3; i++) u = t.update(buf.data(), W, H, RB, bmdFormat10BitYUV, true, true);
    CHECK(u.w == 3840 && u.h == 2160, "parked change to UHD followed: %dx%d", u.w, u.h);

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
