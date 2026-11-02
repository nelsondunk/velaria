#pragma once
#include <cstdint>
#include <string>

#include "Common.h"

namespace vrl {

struct CropRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool empty() const { return w <= 0 || h <= 0; }
    bool operator==(const CropRect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
    bool operator!=(const CropRect& o) const { return !(*this == o); }
};

// Finds the picture inside a letterboxed/pillarboxed raster by scanning for exact video black.
// Returns an empty rect when the frame is entirely black (nothing to learn from it).
CropRect detectActiveArea(const uint8_t* base, int width, int height, long rowBytes, BMDPixelFormat pf);

// Horizontal pixel alignment needed to start a crop at x without repacking (v210 packs 6 pixels per 16 bytes).
int cropAlignment(BMDPixelFormat pf);

// Tracks the active area over time with hysteresis so a dark frame doesn't trigger a reconfiguration.
class ActiveAreaTracker {
public:
    // mode: "auto" | "full" | "fit"; layoutAspect is used by "fit" (e.g. 2.0 for SBS 180)
    void configure(const std::string& mode, double fitAspect) {
        m_mode = mode;
        m_fitAspect = fitAspect;
    }
    void reset() {
        m_current = CropRect();
        m_candidate = CropRect();
        m_candidateCount = 0;
        m_confirmed = false;
    }
    // Feed a frame; returns the rect to use (stable).
    // `parked`: a still frame shown while stopped/scrubbing (where output-setting changes show up first).
    CropRect update(const uint8_t* base, int width, int height, long rowBytes, BMDPixelFormat pf, bool analyse,
                    bool parked = false);
    CropRect current() const { return m_current; }

private:
    CropRect snap(CropRect r, int width, int height, BMDPixelFormat pf) const;
    std::string m_mode = "auto";
    double m_fitAspect = 2.0;
    CropRect m_current, m_candidate;
    int m_candidateCount = 0;
    bool m_confirmed = false;   // current rect came from a real (non-black) detection
};

}  // namespace vrl
