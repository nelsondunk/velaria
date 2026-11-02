#include "ActiveArea.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace vrl {

namespace {

inline uint32_t rd32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

// Is the pixel group at column `x` (in pixels) black?  Group granularity depends on the format.
bool isBlackAt(const uint8_t* row, int x, BMDPixelFormat pf) {
    switch (pf) {
        case bmdFormat10BitYUV: {
            const uint8_t* g = row + (x / 6) * 16;
            uint32_t w0 = rd32(g), w1 = rd32(g + 4), w2 = rd32(g + 8), w3 = rd32(g + 12);
            int y[6] = {(int)((w0 >> 10) & 1023), (int)(w1 & 1023), (int)((w1 >> 20) & 1023),
                        (int)((w2 >> 10) & 1023), (int)(w3 & 1023), (int)((w3 >> 20) & 1023)};
            int c[6] = {(int)(w0 & 1023), (int)((w0 >> 20) & 1023), (int)((w1 >> 10) & 1023),
                        (int)(w2 & 1023), (int)((w2 >> 20) & 1023), (int)((w3 >> 10) & 1023)};
            // Resolve pads with flat video black (64/512/512). Dark picture content almost always carries grain or
            // tint beyond this tolerance; a looser test let dark scenes masquerade as letterboxing.
            for (int i = 0; i < 6; i++)
                if (y[i] > 68 || std::abs(c[i] - 512) > 6) return false;
            return true;
        }
        case bmdFormat8BitYUV: {
            const uint8_t* g = row + (x / 2) * 4;   // Cb Y0 Cr Y1
            return g[1] <= 17 && g[3] <= 17 && std::abs(g[0] - 128) <= 2 && std::abs(g[2] - 128) <= 2;
        }
        case bmdFormat8BitBGRA: {
            const uint8_t* p = row + x * 4;
            return p[0] <= 2 && p[1] <= 2 && p[2] <= 2;
        }
        case bmdFormat8BitARGB: {
            const uint8_t* p = row + x * 4;
            return p[1] <= 2 && p[2] <= 2 && p[3] <= 2;
        }
        default:
            return false;
    }
}

// Timelines come in standard shapes; a dark frame whose bright part happens to sit in the middle (a starfield, a
// spotlight, a logo on black) produces an arbitrary one. Only these shapes may shrink the area.
bool standardAspect(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    static const double kAspects[] = {1.0,        2.0,        0.5,  16.0 / 9, 9.0 / 16, 1.85, 2.0 / 1.85,
                                      2.39,       2.35,       4.0 / 3, 3.0 / 4, 3.0 / 2, 2.0 / 3,
                                      256.0 / 135, 135.0 / 256, 2.2, 4.0};
    double a = (double)w / h;
    for (double s : kAspects)
        if (std::fabs(a / s - 1.0) < 0.015) return true;
    return false;
}

}  // namespace

int cropAlignment(BMDPixelFormat pf) {
    switch (pf) {
        case bmdFormat10BitYUV: return 6;
        case bmdFormat8BitYUV: return 2;
        default: return 2;   // keep chroma siting even for 4:2:0 output
    }
}

CropRect detectActiveArea(const uint8_t* base, int width, int height, long rowBytes, BMDPixelFormat pf) {
    if (!base || width <= 0 || height <= 0) return CropRect();
    const int step = pf == bmdFormat10BitYUV ? 6 : 2;
    int left = width, right = -1, top = height, bottom = -1;
    // Horizontal extents from three rows, vertical extents from three columns.
    for (int k = 1; k <= 3; k++) {
        int yRow = height * k / 4;
        const uint8_t* row = base + (long)yRow * rowBytes;
        for (int x = 0; x + step <= width; x += step)
            if (!isBlackAt(row, x, pf)) {
                left = std::min(left, x);
                break;
            }
        for (int x = ((width - step) / step) * step; x >= 0; x -= step)
            if (!isBlackAt(row, x, pf)) {
                right = std::max(right, x + step - 1);
                break;
            }
        int xCol = ((width * k / 4) / step) * step;
        for (int y = 0; y < height; y++)
            if (!isBlackAt(base + (long)y * rowBytes, xCol, pf)) {
                top = std::min(top, y);
                break;
            }
        for (int y = height - 1; y >= 0; y--)
            if (!isBlackAt(base + (long)y * rowBytes, xCol, pf)) {
                bottom = std::max(bottom, y);
                break;
            }
    }
    if (right < left || bottom < top) return CropRect();
    // Letterboxing from Resolve is centred; make the result symmetric using the smaller border.
    int borderX = std::min(left, width - 1 - right);
    int borderY = std::min(top, height - 1 - bottom);
    CropRect r;
    r.x = borderX;
    r.y = borderY;
    r.w = width - 2 * borderX;
    r.h = height - 2 * borderY;
    return r;
}

CropRect ActiveAreaTracker::snap(CropRect r, int width, int height, BMDPixelFormat pf) const {
    int a = cropAlignment(pf);
    r.x = (r.x / a) * a;
    r.w = std::min(width - r.x, ((r.w + 3) / 4) * 4);   // multiple of 4 so SBS/TB halves stay even
    r.w = (r.w / 4) * 4;
    r.y = (r.y / 2) * 2;
    r.h = std::min(height - r.y, ((r.h + 3) / 4) * 4);
    r.h = (r.h / 4) * 4;
    return r;
}

CropRect ActiveAreaTracker::update(const uint8_t* base, int width, int height, long rowBytes, BMDPixelFormat pf,
                                   bool analyse, bool parked) {
    CropRect full{0, 0, width, height};
    if (m_mode == "full") {
        m_current = snap(full, width, height, pf);
        return m_current;
    }
    if (m_mode == "fit") {
        CropRect r;
        if ((double)width / height > m_fitAspect) {
            r.h = height;
            r.w = (int)(height * m_fitAspect);
        } else {
            r.w = width;
            r.h = (int)(width / m_fitAspect);
        }
        r.x = (width - r.w) / 2;
        r.y = (height - r.h) / 2;
        m_current = snap(r, width, height, pf);
        return m_current;
    }
    // auto
    bool fresh = !m_confirmed || m_current.empty() || m_current.x + m_current.w > width ||
                 m_current.y + m_current.h > height;
    if (fresh) analyse = true;
    if (!analyse) return m_current;
    CropRect d = detectActiveArea(base, width, height, rowBytes, pf);
    if (d.empty()) {   // black frame: no information
        if (fresh) m_current = snap(full, width, height, pf);
        return m_current;
    }
    bool plausible = standardAspect(d.w, d.h);
    d = snap(d, width, height, pf);
    if (fresh) {   // first picture after (re)start: trust it immediately if it has a timeline's shape
        if (!plausible) {
            // most likely a dark frame; show the whole raster and look again on the next frame
            m_current = snap(full, width, height, pf);
            return m_current;
        }
        VLOGI("active area %dx%d+%d+%d", d.w, d.h, d.x, d.y);
        m_current = d;
        m_confirmed = true;
        m_candidateCount = 0;
        return m_current;
    }
    if (d == m_current) {
        m_candidateCount = 0;
        return m_current;
    }
    // Grow immediately when the picture definitely extends past the current area on every side we know about.
    // Shrink cautiously, and only to a timeline-shaped area: a smaller area also looks exactly like a dark scene,
    // a dip to black, a CG frame on pure black or black zenith/nadir caps, and flipping layout mid-shot is far worse
    // than encoding some black. Output settings change while parked, so a parked frame may shrink after three
    // identical looks; during playback the same smaller area must persist for ~10 s of analysed frames.
    bool grows = d.w >= m_current.w && d.h >= m_current.h;
    if (!grows && !plausible) {
        m_candidateCount = 0;
        return m_current;
    }
    if (d == m_candidate)
        m_candidateCount++;
    else {
        m_candidate = d;
        m_candidateCount = 1;
    }
    int needed = parked ? 3 : 25;
    if (grows || m_candidateCount >= needed) {
        VLOGI("active area %dx%d+%d+%d -> %dx%d+%d+%d%s", m_current.w, m_current.h, m_current.x, m_current.y, d.w, d.h,
              d.x, d.y, grows ? "" : parked ? " (parked frames)" : " (stable 10 s)");
        m_current = d;
        m_candidateCount = 0;
    }
    return m_current;
}

}  // namespace vrl
