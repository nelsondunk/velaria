// Host check of the sound orbs' peak tracker (src/orbs.cpp, +91) on synthetic first-order fields, built the way
// src/main.cpp builds the flat map's beam-power field.
//   g++ -O2 -std=c++17 -Iquest/src quest/tools/orbstest.cpp quest/src/orbs.cpp -o /tmp/orbstest && /tmp/orbstest
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "orbs.h"

using vrq::OrbTracker;
static const int W = 512, H = 256;
static int fails = 0;

struct Src { float az, el, gain; };   // AmbiX degrees: az + = left, el + = up

static void field(const std::vector<Src>& srcs, float omni, float diffuse, std::vector<float>& p) {
    float C[4][4] = {};
    for (auto& s : srcs) {
        float az = s.az * (float)M_PI / 180, el = s.el * (float)M_PI / 180;
        float g[4] = {1, std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az)};   // W Y Z X
        for (int a = 0; a < 4; a++) for (int b = 0; b < 4; b++) C[a][b] += s.gain * g[a] * g[b];
    }
    C[0][0] += omni;                                     // W-only energy (mono, reverb tail)
    C[0][0] += diffuse; for (int a = 1; a < 4; a++) C[a][a] += diffuse / 3;   // isotropic diffuse field
    p.resize((size_t)W * H);
    for (int j = 0; j < H; j++)
        for (int i = 0; i < W; i++) {
            float d[3];
            OrbTracker::texelDir((float)i, (float)j, W, H, d);
            float gv[4] = {1, d[1], d[2], d[0]};
            float acc = 0;
            for (int a = 0; a < 4; a++) { float r = 0; for (int b = 0; b < 4; b++) r += C[a][b] * gv[b]; acc += gv[a] * r; }
            p[(size_t)j * W + i] = acc > 0 ? acc : 0;
        }
}

static void dirOf(float azDeg, float elDeg, float out[3]) {
    float az = azDeg * (float)M_PI / 180, el = elDeg * (float)M_PI / 180;
    out[0] = std::cos(el) * std::cos(az); out[1] = std::cos(el) * std::sin(az); out[2] = std::sin(el);
}
static float angDeg(const float* a, const float* b) {
    float c = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    return std::acos(c > 1 ? 1 : (c < -1 ? -1 : c)) * 180 / (float)M_PI;
}
static void check(bool ok, const char* what) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

// run the tracker for n ticks at 45 Hz on a fixed field; peakHold follows the field's peak like main.cpp
static float peakHold = 0;
static void run(OrbTracker& t, const std::vector<float>& p, int ticks, int64_t& now) {
    float peak = 0;
    for (float v : p) peak = std::max(peak, v);
    for (int k = 0; k < ticks; k++) {
        now += 22000000LL;
        peakHold = std::max(peak, peakHold * 0.985f);
        t.update(p.data(), W, H, peakHold, now);
    }
}
static int visible(const OrbTracker& t) {
    int n = 0;
    for (auto& o : t.orbs()) if (o.alpha > 0.5f) n++;
    return n;
}

int main() {
    std::vector<float> p;
    int64_t now = 0;
    {
        // one source: exactly one orb, on it
        const float cases[][2] = {{0, 0}, {90, 0}, {-90, 0}, {180, 0}, {45, 0}, {0, 70}, {-30, -40}};
        for (auto& c : cases) {
            OrbTracker t; peakHold = 0;
            field({{c[0], c[1], 1}}, 0, 0, p);
            run(t, p, 20, now);
            float d[3]; dirOf(c[0], c[1], d);
            bool one = visible(t) == 1 && t.orbs().size() == 1;
            float err = one ? angDeg(t.orbs()[0].dir, d) : 999;
            char buf[128]; snprintf(buf, sizeof buf, "one source az %+4.0f el %+3.0f -> %zu orb(s), %.1f deg off", c[0], c[1], t.orbs().size(), err);
            check(one && err < 1.5f, buf);
        }
    }
    {
        // two sources 150 degrees apart: two orbs. (A first-order cardioid beam is (1 + cos)^2, about 90 degrees
        // wide, so two equal sources closer than ~130 degrees merge into one peak between them: that is the
        // format's limit, not the tracker's, and it is why the panel says the map shows direction, not spread.)
        OrbTracker t; peakHold = 0;
        field({{75, 0, 1}, {-75, 0, 0.8f}}, 0, 0, p);
        run(t, p, 20, now);
        check(visible(t) == 2, "two sources 150 deg apart -> two orbs");
        // 30 degrees apart: first order cannot separate them; one orb, between
        OrbTracker t2; peakHold = 0;
        field({{15, 0, 1}, {-15, 0, 1}}, 0, 0, p);
        run(t2, p, 20, now);
        float f[3]; dirOf(0, 0, f);
        check(visible(t2) == 1 && angDeg(t2.orbs()[0].dir, f) < 6, "two sources 30 deg apart -> one orb between them");
    }
    {
        // all-W (mono / reverb): nothing. Diffuse: nothing. Source under heavy reverb: still one orb.
        OrbTracker t; peakHold = 0;
        field({}, 1, 0, p); run(t, p, 20, now);
        check(t.orbs().empty(), "all-W field -> no orbs");
        OrbTracker t2; peakHold = 0;
        field({}, 0, 1, p); run(t2, p, 20, now);
        check(t2.orbs().empty(), "diffuse field -> no orbs");
        OrbTracker t3; peakHold = 0;
        field({{60, 10, 1}}, 0, 1.5f, p); run(t3, p, 20, now);
        float d[3]; dirOf(60, 10, d);
        check(visible(t3) == 1 && angDeg(t3.orbs()[0].dir, d) < 8, "source in a diffuse field -> one orb on the source");
    }
    {
        // circling tone: one orb, same id throughout, within 12 degrees of the source as it moves 2 deg per tick
        OrbTracker t; peakHold = 0;
        field({{0, 0, 1}}, 0, 0, p); run(t, p, 10, now);
        unsigned id = t.orbs().empty() ? 0 : t.orbs()[0].id;
        float worst = 0; bool sameId = id > 0, always1 = true;
        for (int k = 1; k <= 180; k++) {
            float az = k * 2.0f;
            field({{az, 0, 1}}, 0, 0, p); run(t, p, 1, now);
            if (visible(t) != 1 || t.orbs().size() != 1) always1 = false;
            if (t.orbs().empty() || t.orbs()[0].id != id) sameId = false;
            if (!t.orbs().empty()) { float d[3]; dirOf(az, 0, d); worst = std::max(worst, angDeg(t.orbs()[0].dir, d)); }
        }
        char buf[128]; snprintf(buf, sizeof buf, "circling tone -> one orb, one id, lag at worst %.1f deg", worst);
        check(always1 && sameId && worst < 12, buf);
    }
    {
        // the source stops: the orb is gone within 200 ms; a new one fades in over ~150 ms, not at once
        OrbTracker t; peakHold = 0;
        field({{0, 0, 1}}, 0, 0, p); run(t, p, 20, now);
        field({}, 1, 0, p);
        now += 22000000LL; t.update(p.data(), W, H, peakHold, now);
        bool fading = !t.orbs().empty() && t.orbs()[0].alpha < 1;
        run(t, p, 9, now);   // 10 ticks = 220 ms
        check(fading && t.orbs().empty(), "source stops -> orb fades and is gone within 220 ms");
        OrbTracker t2; peakHold = 0;
        field({{0, 0, 1}}, 0, 0, p);
        now += 22000000LL; t2.update(p.data(), W, H, peakHold, now);
        now += 22000000LL; t2.update(p.data(), W, H, peakHold, now);
        check(t2.orbs().size() == 1 && t2.orbs()[0].alpha > 0 && t2.orbs()[0].alpha < 0.5f, "new source -> orb fades in, not on at once");
    }
    {
        // two sources walk toward each other from 150 apart. First order gives one peak between them once they are
        // closer than ~130, so one orb loses its peak; it must drift into the surviving orb as it fades, so on
        // screen the pair mushes together instead of one blinking out at a distance.
        OrbTracker t; peakHold = 0;
        field({{75, 0, 1}, {-75, 0, 1}}, 0, 0, p); run(t, p, 20, now);
        bool two = visible(t) == 2;
        float closestFading = 999;
        int maxOrbs = 0;
        for (int k = 1; k <= 40; k++) {
            float half = 75 - 3.0f * k;   // 3 deg per tick each: closing at 270 deg/s, fast
            if (half < 5) half = 5;
            field({{half, 0, 1}, {-half, 0, 1}}, 0, 0, p); run(t, p, 1, now);
            maxOrbs = std::max(maxOrbs, (int)t.orbs().size());
            for (auto& a : t.orbs()) {
                if (a.alpha >= 0.99f) continue;   // a fading one
                for (auto& b : t.orbs()) if (&a != &b && b.alpha > a.alpha) closestFading = std::min(closestFading, angDeg(a.dir, b.dir));
            }
        }
        char buf[160]; snprintf(buf, sizeof buf, "closing pair -> fading orb gets within %.0f deg of the live one (bridging range ~20); ends as %zu orb(s)", closestFading, t.orbs().size());
        check(two && closestFading < 20 && visible(t) == 1, buf);
    }
    printf("%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
