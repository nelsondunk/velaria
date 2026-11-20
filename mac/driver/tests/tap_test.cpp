// TapReceiver unit test, no socket: header validation (review finding 2), NaN sanitising, ring invalidation on a
// source change and on a seek (finding 4), and the equal-rank insert flag (finding 9). Builds on Linux or a Mac:
// tests/tap_test.sh
#include "Tap.h"
#include "Common.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

// stand-ins for Log.cpp (Mac only)
static int64_t g_now = 1000000000LL;
namespace vrl {
int64_t nowNs() { return g_now; }
void log(LogLevel, const char* fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr); }
}

using namespace vrl;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static TapHeader hdr(uint16_t ch, uint32_t frames, int64_t t, bool ambi, uint32_t seq) {
    TapHeader h{};
    h.magic = kTapMagic; h.version = 1; h.channels = ch; h.sampleRate = 48000;
    h.flags = 1u | 2u | (ambi ? 4u : 0u); h.projectTime = t; h.frames = frames; h.seq = seq;
    return h;
}
static size_t bytes(const TapHeader& h) { return sizeof h + (size_t)h.frames * h.channels * sizeof(float); }

int main() {
    // ---- validHeader
    {
        TapHeader h = hdr(4, 256, 48000, true, 1);
        CHECK(TapReceiver::validHeader(h, bytes(h)));
        CHECK(!TapReceiver::validHeader(h, bytes(h) - 4));          // short datagram
        CHECK(!TapReceiver::validHeader(h, bytes(h) + 4));          // long datagram
        TapHeader b = h; b.frames = 513;  CHECK(!TapReceiver::validHeader(b, bytes(b)));
        b = h; b.frames = 0;              CHECK(!TapReceiver::validHeader(b, bytes(b)));
        b = h; b.channels = 17;           CHECK(!TapReceiver::validHeader(b, bytes(b)));
        b = h; b.channels = 0;            CHECK(!TapReceiver::validHeader(b, sizeof b));
        b = h; b.projectTime = -5;        CHECK(!TapReceiver::validHeader(b, bytes(b)));
        b = h; b.projectTime = std::numeric_limits<int64_t>::max() - 10; CHECK(!TapReceiver::validHeader(b, bytes(b)));
        b = h; b.flags = 1;               CHECK(TapReceiver::validHeader(b, bytes(b)));   // no project time: allowed, store() ignores it
        b = h; b.sampleRate = 100;        CHECK(!TapReceiver::validHeader(b, bytes(b)));
        b = h; b.version = 2;             CHECK(!TapReceiver::validHeader(b, bytes(b)));
        b = h; b.magic = 1;               CHECK(!TapReceiver::validHeader(b, bytes(b)));
    }

    // ---- ring: write, read back, NaN sanitised
    TapReceiver tap;
    tap.startForTest(48000 * 2);
    const int F = 256;
    std::vector<float> pcm(F * 4);
    for (int i = 0; i < F; i++) for (int c = 0; c < 4; c++) pcm[i * 4 + c] = 0.25f * (c + 1);
    pcm[7 * 4 + 1] = std::numeric_limits<float>::quiet_NaN();
    pcm[8 * 4 + 2] = std::numeric_limits<float>::infinity();
    tap.inject(5000, hdr(4, F, 10000, true, 1), pcm.data(), g_now);
    CHECK(tap.channels() == 4 && tap.ambisonic() && tap.live(g_now));
    std::vector<int16_t> out(F * 4);
    CHECK(tap.read(10000, F, out.data(), 4) == (uint32_t)F);
    CHECK(out[0] == (int16_t)(0.25f * 32767) && out[3] == (int16_t)(1.0f * 32767));
    CHECK(out[7 * 4 + 1] == 0 && out[8 * 4 + 2] == 0);
    CHECK(tap.read(20000, F, out.data(), 4) == 0);   // never written

    // ---- seek: a jump of more than a second forgets the old range
    for (auto& v : pcm) v = std::isfinite(v) ? v : 0.0f;
    tap.inject(5000, hdr(4, F, 10000 + 3 * 48000, true, 2), pcm.data(), g_now);
    CHECK(tap.read(10000, F, out.data(), 4) == 0);                    // old samples gone
    CHECK(tap.read(10000 + 3 * 48000, F, out.data(), 4) == (uint32_t)F);
    int64_t first, end; tap.held(first, end);
    CHECK(first == 10000 + 3 * 48000 && end == first + F);

    // ---- a second, better insert (ambisonic over speakers) takes over and the ring starts empty
    TapReceiver t2;
    t2.startForTest(48000 * 2);
    t2.inject(6000, hdr(2, F, 1000, false, 1), pcm.data(), g_now);          // stereo bus first
    CHECK(t2.channels() == 2 && !t2.ambisonic());
    CHECK(t2.read(1000, F, out.data(), 2) == (uint32_t)F);
    t2.inject(6001, hdr(4, F, 5000, true, 1), pcm.data(), g_now + 1000);   // ambisonic appears
    CHECK(t2.channels() == 4 && t2.ambisonic());
    CHECK(t2.read(1000, F, out.data(), 4) == 0);                             // the stereo insert's samples are not served
    CHECK(t2.read(5000, F, out.data(), 4) == (uint32_t)F);
    // the stereo insert keeps sending and is ignored
    t2.inject(6000, hdr(2, F, 1000 + F, false, 2), pcm.data(), g_now + 2000);
    CHECK(t2.read(1000 + F, F, out.data(), 4) == 0);
    CHECK(!t2.ambiguous());

    // ---- two equal inserts: flagged, first one followed
    TapReceiver t3;
    t3.startForTest(48000 * 2);
    t3.inject(7000, hdr(4, F, 100, true, 1), pcm.data(), g_now);
    t3.inject(7001, hdr(4, F, 100000, true, 1), pcm.data(), g_now + 1000);
    CHECK(t3.ambiguous());
    CHECK(t3.read(100, F, out.data(), 4) == (uint32_t)F);
    CHECK(t3.read(100000, F, out.data(), 4) == 0);
    // the second one goes away (silent 3 s): flag clears on the next packet from the first
    t3.inject(7000, hdr(4, F, 100 + F, true, 2), pcm.data(), g_now + 3000000000LL);
    CHECK(!t3.ambiguous());

    // ---- the chosen insert vanishes: the other takes over, ring cleared
    TapReceiver t4;
    t4.startForTest(48000 * 2);
    t4.inject(8000, hdr(4, F, 100, true, 1), pcm.data(), g_now);
    t4.inject(8001, hdr(2, F, 100, false, 1), pcm.data(), g_now + 1000);
    CHECK(t4.channels() == 4);
    t4.inject(8001, hdr(2, F, 100 + F, false, 2), pcm.data(), g_now + 600000000LL);   // 600 ms later, 4-ch silent
    CHECK(t4.channels() == 2);
    CHECK(t4.read(100, F, out.data(), 2) == 0);           // nothing from the old insert survives
    CHECK(t4.read(100 + F, F, out.data(), 2) == (uint32_t)F);

    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    puts("tap_test: all checks passed");
    return 0;
}
