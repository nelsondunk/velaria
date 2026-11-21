// Sound orbs (+91): the ambisonic sound map as floating volumetric orbs instead of blobs painted on the sphere.
// This half has no GL in it so it builds on the host (tools/orbstest.cpp): it finds the peaks of the beam-power
// field main.cpp already computes for the flat map, follows them from frame to frame, and fades them in and out.
#pragma once
#include <cstdint>
#include <vector>

namespace vrq {

struct Orb {
    float dir[3];      // unit direction in the ambisonic frame: x forward, y left, z up
    float level = 0;   // 0..1, how far above the field's average the peak stands (smoothed)
    float alpha = 0;   // 0..1 fade (150 ms in and out)
    float phase = 0;   // per-orb offset so the wobble is not in step
    int id = 0;
    int64_t lastSeenNs = 0;
};

class OrbTracker {
public:
    static constexpr int kMaxOrbs = 8;
    // p: beam power per texel of a W x H equirect (row 0 = +90 up, column 0 = -180, + = right of front, as the
    // flat map lays it out). peakHold: the map's slow-decaying normaliser (so orbs and the flat map agree on
    // "loud"). Call every time the field is rebuilt (~45 Hz); a call with field == nullptr only fades.
    void update(const float* p, int w, int h, float peakHold, int64_t nowNs);
    const std::vector<Orb>& orbs() const { return m_orbs; }
    // texel -> direction in the ambisonic frame, same mapping as the flat map
    static void texelDir(float i, float j, int w, int h, float out[3]);

    // tuning (public so the host test can read them)
    static constexpr float kMinContrast = 0.30f;   // (peak - mean) / mean below this is a diffuse field: no orbs
    static constexpr float kMinLevel = 0.35f;      // a local maximum this far below the peak is skirt, not a source
    static constexpr float kMergeDeg = 40.0f;      // two maxima closer than this are one first-order blob
    static constexpr float kMatchDeg = 30.0f;      // a peak this close to a known orb is that orb, moved
    static constexpr float kFadeNs = 150e6f;

private:
    std::vector<Orb> m_orbs;
    int64_t m_lastNs = 0;
    int m_nextId = 1;
};

}  // namespace vrq
