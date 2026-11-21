#include "orbs.h"

#include <algorithm>
#include <cmath>

namespace vrq {

namespace {
constexpr float kPi = 3.14159265358979f;
float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void normalize3(float* v) {
    float n = std::sqrt(dot3(v, v));
    if (n > 1e-9f) { v[0] /= n; v[1] /= n; v[2] /= n; }
}
float angleDeg(const float* a, const float* b) {
    float c = dot3(a, b);
    c = c > 1 ? 1 : (c < -1 ? -1 : c);
    return std::acos(c) * 180.0f / kPi;
}
struct Peak {
    float dir[3];
    float level;
};
}  // namespace

void OrbTracker::texelDir(float i, float j, int w, int h, float out[3]) {
    float el = (kPi / 2) - (j + 0.5f) / h * kPi;             // row 0 = +90
    float yawRight = ((i + 0.5f) / w - 0.5f) * 2 * kPi;       // + = right of front
    float az = -yawRight;                                     // AmbiX: + = left
    out[0] = std::cos(el) * std::cos(az);                     // x forward
    out[1] = std::cos(el) * std::sin(az);                     // y left
    out[2] = std::sin(el);                                    // z up
}

void OrbTracker::update(const float* p, int w, int h, float peakHold, int64_t nowNs) {
    float dt = m_lastNs ? (float)(nowNs - m_lastNs) : 0.0f;
    m_lastNs = nowNs;
    float step = dt > 0 ? std::min(dt / kFadeNs, 1.0f) : 0.0f;

    std::vector<Peak> peaks;
    if (p && w >= 16 && h >= 8) {
        double mean = 0;
        float peak = 0;
        for (size_t k = 0; k < (size_t)w * h; k++) {
            mean += p[k];
            if (p[k] > peak) peak = p[k];
        }
        mean /= (double)w * h;
        float contrast = (peak - (float)mean) / std::max((float)mean, 1e-9f);
        float range = std::max(std::max(peakHold, peak) - (float)mean, 1e-9f);
        if (contrast >= kMinContrast) {
            // first-order beams are ~90 degrees wide, so the field is smooth: look for maxima on a coarse grid
            const int cs = std::max(1, w / 64);   // 8 texels on the 512 x 256 map
            const int cw = w / cs, chh = h / cs;
            auto at = [&](int ci, int cj) {
                ci = (ci % cw + cw) % cw;   // wraps in yaw
                cj = cj < 0 ? 0 : (cj >= chh ? chh - 1 : cj);
                return p[(size_t)(cj * cs + cs / 2) * w + (size_t)(ci * cs + cs / 2)];
            };
            for (int cj = 0; cj < chh; cj++)
                for (int ci = 0; ci < cw; ci++) {
                    float v = at(ci, cj);
                    float t = (v - (float)mean) / range;
                    if (t < kMinLevel) continue;
                    bool top = true;
                    for (int dj = -1; dj <= 1 && top; dj++)
                        for (int di = -1; di <= 1; di++) {
                            if (!di && !dj) continue;
                            if (at(ci + di, cj + dj) > v) { top = false; break; }
                        }
                    if (!top) continue;
                    // refine to the texel: the coarse cell is 5.6 degrees wide on the 512 map
                    int bi = ci * cs + cs / 2, bj = cj * cs + cs / 2;
                    float bv = v;
                    for (int jj = std::max(0, bj - cs); jj <= std::min(h - 1, bj + cs); jj++)
                        for (int ii = bi - cs; ii <= bi + cs; ii++) {
                            float vv = p[(size_t)jj * w + (size_t)((ii % w + w) % w)];
                            if (vv > bv) { bv = vv; bi = (ii % w + w) % w; bj = jj; }
                        }
                    Peak pk;
                    texelDir((float)bi, (float)bj, w, h, pk.dir);
                    pk.level = t > 1 ? 1 : t;
                    peaks.push_back(pk);
                }
            // one blob can top out on several coarse cells (plateaus at the poles especially): keep the strongest
            std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.level > b.level; });
            std::vector<Peak> kept;
            for (const Peak& pk : peaks) {
                bool dup = false;
                for (const Peak& k : kept)
                    if (angleDeg(pk.dir, k.dir) < kMergeDeg) { dup = true; break; }
                if (!dup) kept.push_back(pk);
                if ((int)kept.size() >= kMaxOrbs) break;
            }
            peaks.swap(kept);
        }
    }

    // match peaks to orbs: nearest within kMatchDeg, each orb taken once
    std::vector<bool> orbHit(m_orbs.size(), false), peakUsed(peaks.size(), false);
    for (size_t pi = 0; pi < peaks.size(); pi++) {
        int best = -1;
        float bestA = kMatchDeg;
        for (size_t oi = 0; oi < m_orbs.size(); oi++) {
            if (orbHit[oi]) continue;
            float a = angleDeg(peaks[pi].dir, m_orbs[oi].dir);
            if (a < bestA) { bestA = a; best = (int)oi; }
        }
        if (best >= 0) {
            Orb& o = m_orbs[best];
            const float kDir = 0.30f, kLev = 0.25f;   // ~50 ms to settle at 45 Hz; follows a circling tone cleanly
            for (int c = 0; c < 3; c++) o.dir[c] += (peaks[pi].dir[c] - o.dir[c]) * kDir;
            normalize3(o.dir);
            o.level += (peaks[pi].level - o.level) * kLev;
            o.lastSeenNs = nowNs;
            orbHit[best] = true;
            peakUsed[pi] = true;
        }
    }
    for (size_t oi = 0; oi < m_orbs.size(); oi++) {
        Orb& o = m_orbs[oi];
        if (orbHit[oi]) o.alpha = std::min(1.0f, o.alpha + step);
        else o.alpha -= step;
    }
    m_orbs.erase(std::remove_if(m_orbs.begin(), m_orbs.end(), [](const Orb& o) { return o.alpha <= 0; }), m_orbs.end());
    for (size_t pi = 0; pi < peaks.size(); pi++) {
        if (peakUsed[pi] || (int)m_orbs.size() >= kMaxOrbs) continue;
        Orb o;
        for (int c = 0; c < 3; c++) o.dir[c] = peaks[pi].dir[c];
        o.level = peaks[pi].level;
        o.alpha = 0.0f;
        o.id = m_nextId++;
        o.phase = (float)((o.id * 2654435761u) % 1000) / 1000.0f * 6.2831853f;
        o.lastSeenNs = nowNs;
        m_orbs.push_back(o);
    }
}

}  // namespace vrq
