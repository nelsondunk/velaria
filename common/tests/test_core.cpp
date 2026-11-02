// Unit tests for the shared protocol + timing core. Build: see common/tests/run.sh
#include <climits>
#include <cmath>
#include <cstdio>
#include <random>

#include "../protocol.h"
#include "../timing.h"

static int g_fail = 0, g_pass = 0;
#define EXPECT(cond, ...)                                  \
    do {                                                   \
        if (cond) {                                        \
            g_pass++;                                      \
            printf("  ok   " __VA_ARGS__);                 \
        } else {                                           \
            g_fail++;                                      \
            printf("  FAIL " __VA_ARGS__);                 \
        }                                                  \
        printf("\n");                                      \
    } while (0)

using namespace vrltime;
using namespace vrlproto;

static void testProtocolParser() {
    printf("protocol parser\n");
    std::vector<uint8_t> stream;
    std::mt19937 rng(1);
    std::vector<std::vector<uint8_t>> payloads;
    for (int i = 0; i < 200; i++) {
        VideoMsg v{};
        v.frameSeq = (uint64_t)i;
        v.ptsNs = i * 41708333LL;
        std::vector<uint8_t> au((size_t)(rng() % 200000));
        for (auto& b : au) b = (uint8_t)rng();
        appendMessage(stream, MsgType::Video, &v, sizeof v, au.data(), au.size());
        payloads.push_back(au);
    }
    StreamParser p;
    size_t pos = 0, got = 0;
    bool intact = true;
    while (pos < stream.size()) {
        size_t chunk = std::min(stream.size() - pos, (size_t)(1 + rng() % 70000));
        p.feed(stream.data() + pos, chunk);
        pos += chunk;
        StreamParser::Message m;
        while (p.next(m)) {
            VideoMsg v;
            const uint8_t* extra;
            size_t elen;
            if (m.type != MsgType::Video || !readBody(m.payload, v, &extra, &elen) || v.frameSeq != got ||
                elen != payloads[got].size() || (elen && memcmp(extra, payloads[got].data(), elen) != 0))
                intact = false;
            got++;
        }
    }
    EXPECT(got == 200 && intact, "200 fragmented messages reassembled intact (%zu)", got);

    StreamParser bad;
    MsgHeader h{};
    h.length = kMaxPayload + 1;
    h.type = 3;
    bad.feed((const uint8_t*)&h, sizeof h);
    StreamParser::Message m;
    EXPECT(!bad.next(m) && bad.error(), "oversized message rejected");
}

// Simulated network: receiver clock r, sender clock s = r*(1+drift) + off.
struct SimLink {
    double drift;
    int64_t off;
    std::mt19937 rng{7};
    int64_t sender(int64_t r) const { return (int64_t)std::llround((double)r * (1.0 + drift)) + off; }
    int64_t delay(int64_t baseUs, int64_t jitterUs) {
        std::exponential_distribution<double> e(1.0 / (double)jitterUs);
        return (baseUs + (int64_t)e(rng)) * 1000;
    }
};

static void testClockSync() {
    printf("clock sync\n");
    for (double ppm : {0.0, 40.0, -80.0}) {
        SimLink link{ppm * 1e-6, 123456789012LL};
        ClockSync cs;
        double worstAfterSettle = 0;
        int64_t r = 1000000000LL;
        for (int i = 0; i < 4 * 600; i++) {   // 4 pings/s for 10 minutes
            int64_t t1 = r;
            int64_t up = link.delay(800, 3000);      // Wi-Fi-ish: 0.8 ms base + exp(3 ms) jitter
            int64_t down = link.delay(800, 3000);
            int64_t t2 = link.sender(t1 + up);
            int64_t t3 = t2 + 50000;                  // 50 us processing
            int64_t t4 = t1 + up + 50000 + down;
            cs.addSample(t1, t2, t3, t4);
            if (i > 40) {   // >10 s
                int64_t truth = link.sender(t4) - t4;
                double err = std::fabs((double)(cs.offsetAt(t4) - truth)) / 1e6;
                worstAfterSettle = std::max(worstAfterSettle, err);
            }
            r += 250000000LL;
        }
        EXPECT(worstAfterSettle < 2.0, "drift %+.0f ppm: worst offset error after 10 s = %.3f ms (est drift %.1f ppm)",
               ppm, worstAfterSettle, cs.driftPpm());
    }
}

struct CadenceResult {
    CadenceStats stats;
    uint64_t frames;
};

// Simulate playback of `seconds` of video at fps on a display at hz. Frames arrive `lead` ns before due (with
// jitter) and decode instantly. Returns scheduler stats.
static CadenceResult simulateCadence(double fps, double hz, double seconds, int64_t jitterNs, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int64_t> jit(0, std::max<int64_t>(1, jitterNs));
    FrameScheduler s;
    int64_t refresh = (int64_t)std::llround(1e9 / hz);
    s.setRefreshPeriod(refresh);
    int64_t frameDur = (int64_t)std::llround(1e9 / fps);
    const int64_t playout = 80000000;   // 80 ms playout delay
    uint64_t total = (uint64_t)(seconds * fps);
    std::vector<ScheduledItem> ready;
    uint64_t nextSeq = 1;
    int64_t t0 = 5000000000LL + 3333333;   // arbitrary phase between video and display
    uint64_t onScreen = 0;
    int64_t onScreenDur = 0;
    for (int64_t display = 5000000000LL; nextSeq <= total || !ready.empty(); display += refresh) {
        // arrivals up to now: frame seq k scanned out on sender at t0 + (k-1)*dur, arrives at that + jitter
        while (nextSeq <= total) {
            int64_t scanout = t0 + (int64_t)(nextSeq - 1) * frameDur;
            int64_t arrival = scanout + jit(rng);
            if (arrival > display) break;
            ready.push_back({nextSeq, scanout + playout, frameDur, false});
            nextSeq++;
        }
        int64_t pendingDue = nextSeq <= total ? t0 + (int64_t)(nextSeq - 1) * frameDur + playout : INT64_MAX;
        int idx = s.pick(ready, display, pendingDue);
        if (idx >= 0) {
            onScreen = ready[idx].seq;
            onScreenDur = ready[idx].durationNs;
            ready.erase(ready.begin(), ready.begin() + idx + 1);
        }
        if (onScreen) s.onRefresh(onScreen, onScreenDur, true);
        if (display > t0 + (int64_t)(seconds * 1e9) + 1000000000LL) break;
    }
    s.finish();
    return {s.stats(), total};
}

static void testCadence() {
    printf("cadence (frame shown for a whole, constant number of refreshes)\n");
    auto r = simulateCadence(24.0, 72.0, 60, 30000000, 1);
    EXPECT(r.stats.cadenceErrors == 0 && r.stats.framesSkipped == 0 && r.stats.holds == 0,
           "24p @ 72 Hz, 30 ms network jitter: shown=%llu skipped=%llu cadenceErr=%llu holds=%llu",
           (unsigned long long)r.stats.framesShown, (unsigned long long)r.stats.framesSkipped,
           (unsigned long long)r.stats.cadenceErrors, (unsigned long long)r.stats.holds);
    r = simulateCadence(30.0, 90.0, 60, 30000000, 2);
    EXPECT(r.stats.cadenceErrors == 0 && r.stats.framesSkipped == 0, "30p @ 90 Hz: cadenceErr=%llu skipped=%llu",
           (unsigned long long)r.stats.cadenceErrors, (unsigned long long)r.stats.framesSkipped);
    r = simulateCadence(60.0, 120.0, 60, 20000000, 3);
    EXPECT(r.stats.cadenceErrors == 0 && r.stats.framesSkipped == 0, "60p @ 120 Hz: cadenceErr=%llu skipped=%llu",
           (unsigned long long)r.stats.cadenceErrors, (unsigned long long)r.stats.framesSkipped);
    r = simulateCadence(24000.0 / 1001.0, 72.0, 120, 30000000, 4);
    // 23.976 on an integer 72 Hz panel runs 0.1% fast: one frame gets a 4th refresh every ~13.9 s (~8.6 in 120 s).
    EXPECT(r.stats.cadenceErrors >= 7 && r.stats.cadenceErrors <= 10 && r.stats.framesSkipped == 0,
           "23.976p @ 72 Hz for 120 s: %llu single-refresh holds (expected ~8.6), skipped=%llu",
           (unsigned long long)r.stats.cadenceErrors, (unsigned long long)r.stats.framesSkipped);
    r = simulateCadence(24.0, 72.0, 30, 120000000, 5);   // jitter beyond playout delay
    EXPECT(r.stats.holds > 0, "jitter beyond playout delay is detected as holds (%llu)", (unsigned long long)r.stats.holds);
    auto h = simulateCadence(24.0, 72.0, 10, 1000000, 6).stats.refreshHistogram;
    EXPECT(h[3] > 200 && h[2] == 0 && h[4] == 0, "24p @ 72 Hz histogram: every frame exactly 3 refreshes (%llu)",
           (unsigned long long)h[3]);
}

static void testStaleFrameDoesNotBlock() {
    printf("robustness\n");
    FrameScheduler s;
    s.setRefreshPeriod(13888889);
    std::vector<ScheduledItem> ready = {{10, 9000000000LL, 41708333, false},   // stale: due 9 s from now
                                        {11, 1000000000LL, 41708333, false},
                                        {12, 1041708333LL, 41708333, false}};
    int idx = s.pick(ready, 1045000000LL, INT64_MAX);
    EXPECT(idx == 2, "a frame with a far-future due time does not block due frames behind it (picked %d)", idx);
}

static void testRefreshPicker() {
    printf("refresh-rate selection\n");
    std::vector<float> quest = {72, 80, 90, 120};
    std::vector<float> extended = {72, 75, 80, 90, 96, 100, 120, 144, 150, 180};
    struct Case {
        double fps;
        const std::vector<float>* list;
        float want;
    } cases[] = {{24000.0 / 1001, &quest, 72}, {24, &quest, 72}, {30000.0 / 1001, &quest, 90}, {30, &quest, 90},
                 {60, &quest, 120},          {50, &extended, 100}, {25, &extended, 100}, {48, &extended, 96},
                 {60000.0 / 1001, &quest, 120}};
    for (auto& c : cases) {
        double err;
        float got = pickRefreshRate(*c.list, c.fps, &err);
        EXPECT(got == c.want, "%.3f fps -> %.0f Hz (want %.0f, err %.4f)", c.fps, got, c.want, err);
    }
}

int main() {
    testProtocolParser();
    testClockSync();
    testCadence();
    testRefreshPicker();
    testStaleFrameDoesNotBlock();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
