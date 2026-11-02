#include "hud.h"
#include "edit.h"

#include <pthread.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vrq {

using font_ui::Font;
using namespace pal;

// ------------------------------------------------------------------------------------------------ colour

namespace {

float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

struct EncodeLut {
    uint8_t v[4097];
    EncodeLut() {
        for (int i = 0; i <= 4096; i++) {
            float l = i / 4096.0f;
            float s = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
            v[i] = (uint8_t)std::lround(std::min(1.0f, std::max(0.0f, s)) * 255.0f);
        }
    }
};
const EncodeLut& lut() {
    static EncodeLut l;
    return l;
}


uint32_t nextCodepoint(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    uint32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra && i < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return cp;
}

int glyphIndex(uint32_t cp) {
    const uint32_t* b = font_ui::kCodepoints;
    const uint32_t* e = b + font_ui::kGlyphCount;
    const uint32_t* it = std::lower_bound(b, e, cp);
    if (it == e || *it != cp) return '?' - 32;
    return (int)(it - b);
}

float sdRoundRect(float px, float py, float cx, float cy, float hw, float hh, float r) {
    float qx = std::fabs(px - cx) - (hw - r), qy = std::fabs(py - cy) - (hh - r);
    float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

}  // namespace

std::string timecodeString(uint32_t bcd) {
    if (bcd == 0xFFFFFFFF) return "--:--:--:--";
    char b[16];
    snprintf(b, sizeof b, "%02x:%02x:%02x:%02x", (bcd >> 24) & 0x3f, (bcd >> 16) & 0x7f, (bcd >> 8) & 0x7f, bcd & 0x3f);
    return b;
}

// ------------------------------------------------------------------------------------------------ canvas

void Canvas::reset(int w, int h) {
    m_w = w;
    m_h = h;
    m_px.assign((size_t)w * h * 4, 0.0f);
}

template <typename Cov>
void Canvas::shade(int x0, int y0, int x1, int y1, Colour c, Cov cov) {
    float lr = srgbToLinear(c.r), lg = srgbToLinear(c.g), lb = srgbToLinear(c.b);
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(m_w, x1);
    y1 = std::min(m_h, y1);
    for (int y = y0; y < y1; y++) {
        float* row = &m_px[(size_t)y * m_w * 4];
        for (int x = x0; x < x1; x++) {
            float a = cov(x + 0.5f, y + 0.5f) * c.a;
            if (a <= 0.0f) continue;
            float* p = row + x * 4;
            float k = 1.0f - a;
            p[0] = lr * a + p[0] * k;
            p[1] = lg * a + p[1] * k;
            p[2] = lb * a + p[2] * k;
            p[3] = a + p[3] * k;
        }
    }
}

void Canvas::fillRoundRect(float x, float y, float w, float h, float r, Colour c) {
    float cx = x + w / 2, cy = y + h / 2;
    r = std::min(r, std::min(w, h) / 2);
    shade((int)std::floor(x), (int)std::floor(y), (int)std::ceil(x + w), (int)std::ceil(y + h), c, [&](float px, float py) {
        return std::min(1.0f, std::max(0.0f, 0.5f - sdRoundRect(px, py, cx, cy, w / 2, h / 2, r)));
    });
}

void Canvas::strokeRoundRect(float x, float y, float w, float h, float r, float t, Colour c) {
    float cx = x + w / 2, cy = y + h / 2;
    r = std::min(r, std::min(w, h) / 2);
    shade((int)std::floor(x) - 1, (int)std::floor(y) - 1, (int)std::ceil(x + w) + 1, (int)std::ceil(y + h) + 1, c,
          [&](float px, float py) {
              float d = sdRoundRect(px, py, cx, cy, w / 2, h / 2, r);
              return std::min(1.0f, std::max(0.0f, 0.5f - (std::fabs(d + t / 2) - t / 2)));
          });
}

void Canvas::shadeRect(int x0, int y0, int x1, int y1, const std::function<Colour(float, float)>& fn) {
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(m_w, x1);
    y1 = std::min(m_h, y1);
    for (int y = y0; y < y1; y++) {
        float* row = &m_px[(size_t)y * m_w * 4];
        for (int x = x0; x < x1; x++) {
            Colour c = fn(x + 0.5f, y + 0.5f);
            if (c.a <= 0.0f) continue;
            float* p = row + x * 4;
            float a = std::min(1.0f, c.a), k = 1.0f - a;
            p[0] = srgbToLinear(c.r) * a + p[0] * k;
            p[1] = srgbToLinear(c.g) * a + p[1] * k;
            p[2] = srgbToLinear(c.b) * a + p[2] * k;
            p[3] = a + p[3] * k;
        }
    }
}

void Canvas::fillCircle(float cx, float cy, float r, Colour c) {
    shade((int)(cx - r - 1), (int)(cy - r - 1), (int)(cx + r + 2), (int)(cy + r + 2), c, [&](float px, float py) {
        float d = std::hypot(px - cx, py - cy) - r;
        return std::min(1.0f, std::max(0.0f, 0.5f - d));
    });
}

void Canvas::strokeCircle(float cx, float cy, float r, float t, Colour c) {
    shade((int)(cx - r - 2), (int)(cy - r - 2), (int)(cx + r + 3), (int)(cy + r + 3), c, [&](float px, float py) {
        float d = std::hypot(px - cx, py - cy) - r;
        return std::min(1.0f, std::max(0.0f, 0.5f - (std::fabs(d + t / 2) - t / 2)));
    });
}

void Canvas::fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Colour c) {
    auto edge = [](float ax, float ay, float bx, float by, float px, float py) {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    };
    float area = edge(x0, y0, x1, y1, x2, y2);
    if (std::fabs(area) < 1e-3f) return;
    float s = area > 0 ? 1.0f : -1.0f;
    int bx0 = (int)std::floor(std::min({x0, x1, x2})), by0 = (int)std::floor(std::min({y0, y1, y2}));
    int bx1 = (int)std::ceil(std::max({x0, x1, x2})) + 1, by1 = (int)std::ceil(std::max({y0, y1, y2})) + 1;
    shade(bx0, by0, bx1, by1, c, [&](float px, float py) {
        int inside = 0;   // 4x4 supersampling
        for (int j = 0; j < 4; j++)
            for (int i = 0; i < 4; i++) {
                float sx = px - 0.5f + (i + 0.5f) / 4, sy = py - 0.5f + (j + 0.5f) / 4;
                if (s * edge(x0, y0, x1, y1, sx, sy) >= 0 && s * edge(x1, y1, x2, y2, sx, sy) >= 0 &&
                    s * edge(x2, y2, x0, y0, sx, sy) >= 0)
                    inside++;
            }
        return inside / 16.0f;
    });
}


void Canvas::strokeArc(float cx, float cy, float r, float t, float a0, float a1, Colour c) {
    shade((int)(cx - r - t), (int)(cy - r - t), (int)(cx + r + t + 1), (int)(cy + r + t + 1), c, [&](float px, float py) {
        float dx = px - cx, dy = cy - py;   // y up
        float d = std::hypot(dx, dy) - r;
        float ring = std::min(1.0f, std::max(0.0f, 0.5f - (std::fabs(d) - t / 2)));
        if (ring <= 0) return 0.0f;
        float a = std::atan2(dy, dx);
        // bring a into [a0, a0 + 2pi)
        while (a < a0) a += 2 * (float)M_PI;
        while (a >= a0 + 2 * (float)M_PI) a -= 2 * (float)M_PI;
        if (a > a1) {
            // soft caps: distance to the nearest end point
            float ex0 = cx + std::cos(a0) * r, ey0 = cy - std::sin(a0) * r;
            float ex1 = cx + std::cos(a1) * r, ey1 = cy - std::sin(a1) * r;
            float dc = std::min(std::hypot(px - ex0, py - ey0), std::hypot(px - ex1, py - ey1));
            return std::min(1.0f, std::max(0.0f, t / 2 + 0.5f - dc));
        }
        return ring;
    });
}

// ---- material

void Canvas::glass(float r) {
    float w = (float)m_w, h = (float)m_h;
    // faint copper halo just inside the edge, then the glass body
    fillRoundRect(0, 0, w, h, r, Colour{kAccent.r, kAccent.g, kAccent.b, 0.05f});
    fillRoundRect(1, 1, w - 2, h - 2, r - 1, kPanelBg);
    // inner vignette: darker toward the bottom
    shadeRect(0, (int)(h * 0.55f), m_w, m_h, [&](float, float py) {
        float t = (py - h * 0.55f) / (h * 0.45f);
        return Colour{0, 0, 0, 0.10f * t * t};
    });
    // rim light (top) and base line (bottom)
    strokeRoundRect(0.5f, 0.5f, w - 1, h - 1, r, 1.5f, kRim);
    fillRoundRect(r, 1, w - 2 * r, 1.5f, 0.75f, Colour{1, 1, 1, 0.22f});
    fillRoundRect(r, h - 2.5f, w - 2 * r, 1.5f, 0.75f, Colour{0, 0, 0, 0.6f});
}

void Canvas::glowRoundRect(float x, float y, float w, float h, float r, Colour c, float spread) {
    const int steps = 9;
    for (int i = steps; i >= 1; i--) {
        float e = spread * i / steps;
        Colour cc = c;
        cc.a = c.a * (1.0f - (float)i / (steps + 1)) * 0.35f;
        fillRoundRect(x - e, y - e, w + 2 * e, h + 2 * e, r + e, cc);
    }
}

void Canvas::glowCircle(float cx, float cy, float r, Colour c, float spread) {
    const int steps = 9;
    for (int i = steps; i >= 1; i--) {
        float e = spread * i / steps;
        Colour cc = c;
        cc.a = c.a * (1.0f - (float)i / (steps + 1)) * 0.35f;
        fillCircle(cx, cy, r + e, cc);
    }
}

void Canvas::clearRoundRect(float x, float y, float w, float h, float r) {
    float cx = x + w / 2, cy = y + h / 2;
    int x0 = std::max(0, (int)x - 1), y0 = std::max(0, (int)y - 1), x1 = std::min(m_w, (int)(x + w) + 2), y1 = std::min(m_h, (int)(y + h) + 2);
    for (int py = y0; py < y1; py++) {
        float* row = &m_px[(size_t)py * m_w * 4];
        for (int px = x0; px < x1; px++) {
            float d = sdRoundRect(px + 0.5f, py + 0.5f, cx, cy, w / 2, h / 2, r);
            float cov = std::min(1.0f, std::max(0.0f, 0.5f - d));
            if (cov <= 0) continue;
            float* p = row + px * 4;
            for (int k = 0; k < 4; k++) p[k] *= 1.0f - cov;
        }
    }
}

void Canvas::well(float x, float y, float w, float h, float r) {
    fillRoundRect(x, y, w, h, r, kWell);
    // inner shadow along the top edge, a faint light along the bottom
    fillRoundRect(x + 1, y + 1, w - 2, std::min(h / 2, 6.0f), std::min(r, 3.0f), Colour{0, 0, 0, 0.28f});
    fillRoundRect(x + 1, y + 1, w - 2, std::min(h / 2, 2.5f), std::min(r, 1.5f), Colour{0, 0, 0, 0.35f});
    fillRoundRect(x + r, y + h - 1.5f, w - 2 * r, 1, 0.5f, Colour{1, 1, 1, 0.07f});
}

void Canvas::bevel(float x, float y, float w, float h, float r, bool hover, bool pressed, bool active) {
    chipBody(x, y, w, h, r, active ? ChipState::On : pressed ? ChipState::Pressed : hover ? ChipState::Hover : ChipState::Rest);
}

void Canvas::knob(float cx, float cy, float r, bool live) {
    // v3: a white dome with a dark ring at rest; a copper ring only while held. No glow.
    shadow(cx - r, cy - r, 2 * r, 2 * r, r, 6, 8, 0.6f);
    if (live) fillCircle(cx, cy, r + 4, kAccent);
    else fillCircle(cx, cy, r + 3, kInkOnAccent);
    fillRoundRectGrad(cx - r, cy - r, 2 * r, 2 * r, r, {{0, Colour{1, 1, 1, 1}}, {1, hex(0xcfd3d8)}});
}

// ---- UI v3 primitives

void Canvas::fillRoundRectGrad(float x, float y, float w, float h, float r, const std::vector<GradStop>& stops, bool vertical) {
    if (stops.empty()) return;
    float cx = x + w / 2, cy = y + h / 2;
    r = std::min(r, std::min(w, h) / 2);
    // linear-light interpolation between sRGB stops looks like the CSS gradient on a real display
    struct LinStop { float t, r, g, b, a; };
    std::vector<LinStop> ls;
    for (auto& st : stops) ls.push_back({st.t, srgbToLinear(st.c.r), srgbToLinear(st.c.g), srgbToLinear(st.c.b), st.c.a});
    auto at = [&](float t, float& lr, float& lg, float& lb, float& la) {
        if (t <= ls.front().t) { lr = ls.front().r; lg = ls.front().g; lb = ls.front().b; la = ls.front().a; return; }
        for (size_t i = 1; i < ls.size(); i++) {
            if (t <= ls[i].t) {
                float u = (t - ls[i - 1].t) / std::max(1e-5f, ls[i].t - ls[i - 1].t);
                lr = ls[i - 1].r + (ls[i].r - ls[i - 1].r) * u;
                lg = ls[i - 1].g + (ls[i].g - ls[i - 1].g) * u;
                lb = ls[i - 1].b + (ls[i].b - ls[i - 1].b) * u;
                la = ls[i - 1].a + (ls[i].a - ls[i - 1].a) * u;
                return;
            }
        }
        lr = ls.back().r; lg = ls.back().g; lb = ls.back().b; la = ls.back().a;
    };
    int x0 = std::max(0, (int)std::floor(x)), y0 = std::max(0, (int)std::floor(y));
    int x1 = std::min(m_w, (int)std::ceil(x + w)), y1 = std::min(m_h, (int)std::ceil(y + h));
    for (int py = y0; py < y1; py++) {
        float* row = &m_px[(size_t)py * m_w * 4];
        float lr = 0, lg = 0, lb = 0, la = 1;
        if (vertical) at((py + 0.5f - y) / h, lr, lg, lb, la);
        for (int px = x0; px < x1; px++) {
            if (!vertical) at((px + 0.5f - x) / w, lr, lg, lb, la);
            float cov = std::min(1.0f, std::max(0.0f, 0.5f - sdRoundRect(px + 0.5f, py + 0.5f, cx, cy, w / 2, h / 2, r)));
            float a = cov * la;
            if (a <= 0) continue;
            float* p = row + px * 4;
            float k = 1.0f - a;
            p[0] = lr * a + p[0] * k;
            p[1] = lg * a + p[1] * k;
            p[2] = lb * a + p[2] * k;
            p[3] = a + p[3] * k;
        }
    }
}

void Canvas::shadow(float x, float y, float w, float h, float r, float dy, float blur, float alpha) {
    // a stack of expanding, fading rects reads as a blurred shadow at headset resolution
    const int steps = 6;
    for (int i = steps; i >= 1; i--) {
        float e = blur * i / steps;
        float a = alpha * (1.0f - (float)(i - 1) / steps) / steps * 1.6f;
        fillRoundRect(x - e, y + dy - e, w + 2 * e, h + 2 * e, r + e, Colour{0, 0, 0, a});
    }
}

void Canvas::copper(float x, float y, float w, float h, float r, bool polished) {
    static const std::vector<GradStop> satin = {{0.0f, hex(0xf4cfa8)}, {0.42f, hex(0xe3a676)}, {0.62f, hex(0xcd8a5b)}, {1.0f, hex(0xe2a878)}};
    static const std::vector<GradStop> polish = {{0.0f, hex(0xfbe3c8)}, {0.20f, hex(0xeab389)}, {0.44f, hex(0xcf8d5e)},
                                                 {0.56f, hex(0xb97448)}, {0.76f, hex(0xd99c6c)}, {1.0f, hex(0xf3c79d)}};
    fillRoundRectGrad(x, y, w, h, r, polished ? polish : satin, true);
    float rr = std::min(r, std::min(w, h) / 2);
    fillRoundRect(x + rr / 2, y, w - rr, 1, 0.5f, Colour{1, 1, 1, polished ? 0.7f : 0.6f});
    fillRoundRect(x + rr / 2, y + h - 1, w - rr, 1, 0.5f, Colour{kCopperEdgeDark.r, kCopperEdgeDark.g, kCopperEdgeDark.b, polished ? 0.6f : 0.5f});
}

void Canvas::chipBody(float x, float y, float w, float h, float r, ChipState st) {
    switch (st) {
        case ChipState::Flat:
            return;
        case ChipState::Off:
            fillRoundRect(x, y, w, h, r, hex(0x1f242c));
            return;
        case ChipState::Pressed:
            fillRoundRect(x, y, w, h, r, hex(0x20252d));
            fillRoundRect(x + 1, y + 1, w - 2, 5, std::min(r, 3.0f), Colour{0, 0, 0, 0.30f});
            fillRoundRect(x + 1, y + 1, w - 2, 2.5f, std::min(r, 1.5f), Colour{0, 0, 0, 0.40f});
            return;
        case ChipState::On:
            shadow(x, y, w, h, r, 6, 10, 0.45f);
            copper(x, y, w, h, r, false);
            return;
        case ChipState::Hover:
            shadow(x, y, w, h, r, 10, 14, 0.50f);
            fillRoundRectGrad(x, y, w, h, r, {{0, hex(0x434b59)}, {1, hex(0x2c323c)}});
            fillRoundRect(x + r / 2, y, w - r, 1, 0.5f, Colour{1, 1, 1, 0.32f});
            fillRoundRect(x + r / 2, y + h - 2, w - r, 2, 1, Colour{0, 0, 0, 0.35f});
            return;
        case ChipState::Rest:
        default:
            shadow(x, y, w, h, r, 6, 10, 0.45f);
            fillRoundRectGrad(x, y, w, h, r, {{0, hex(0x363d49)}, {1, hex(0x262c35)}});
            fillRoundRect(x + r / 2, y, w - r, 1, 0.5f, Colour{1, 1, 1, 0.14f});
            fillRoundRect(x + r / 2, y + h - 2, w - r, 2, 1, Colour{0, 0, 0, 0.35f});
            return;
    }
}

Colour Canvas::chipInk(ChipState st) {
    switch (st) {
        case ChipState::On: return kInkOnAccent;
        case ChipState::Off: return Colour{kText.r, kText.g, kText.b, 0.40f};
        case ChipState::Pressed: return Colour{kText.r, kText.g, kText.b, 0.85f};
        case ChipState::Flat: return kDim;
        default: return kText;
    }
}

void Canvas::wellV3(float x, float y, float w, float h, float r) {
    fillRoundRect(x, y, w, h, r, Colour{0, 0, 0, 0.45f});
    float rr = std::min(r, std::min(w, h) / 2);
    // inset 0 3px 10px #000000bf: a few bands from the top edge
    fillRoundRect(x + 1, y + 1, w - 2, std::min(h - 2, 10.0f), std::min(rr, 5.0f), Colour{0, 0, 0, 0.18f});
    fillRoundRect(x + 1, y + 1, w - 2, std::min(h - 2, 6.0f), std::min(rr, 3.0f), Colour{0, 0, 0, 0.22f});
    fillRoundRect(x + 1, y + 1, w - 2, std::min(h - 2, 3.0f), std::min(rr, 1.5f), Colour{0, 0, 0, 0.30f});
    fillRoundRect(x + rr, y + h - 1, w - 2 * rr, 1, 0.5f, Colour{1, 1, 1, 0.07f});
}

void Canvas::glassV3(float r, bool focused, float h) {
    float w = (float)m_w;
    if (h < 0) h = (float)m_h;
    float a = focused ? 0.92f : 0.85f;
    fillRoundRectGrad(0, 0, w, h, r, {{0, hex(0x262c35, a)}, {1, hex(0x1d222a, a)}});
    // lit top edge, dark base line
    fillRoundRect(r, 0, w - 2 * r, 1, 0.5f, Colour{1, 1, 1, focused ? 0.16f : 0.12f});
    fillRoundRect(r, h - 1, w - 2 * r, 1, 0.5f, Colour{0, 0, 0, 0.6f});
    strokeRoundRect(0.5f, 0.5f, w - 1, h - 1, r, 1, Colour{1, 1, 1, focused ? 0.06f : 0.09f});
    if (focused) {
        // the copper hairline: "your hand is in this panel"
        fillRoundRectGrad(28, 0, w - 56, 2, 1, {{0, hex(0xe0a97a, 0)}, {0.5f, hex(0xf0bc8c, 1)}, {1, hex(0xe0a97a, 0)}}, false);
    }
}

void Canvas::icon(icons_ui::Icon id, float cx, float cy, int size, Colour c) {
    int si = 0;
    for (int i = 1; i < icons_ui::kSizeCount; i++)
        if (std::abs(icons_ui::kSizes[i] - size) < std::abs(icons_ui::kSizes[si] - size)) si = i;
    int sz = icons_ui::kSizes[si];
    const uint8_t* bm = icons_ui::bitmap(si) + (size_t)(int)id * sz * sz;
    int gx = (int)std::lround(cx - sz / 2.0f), gy = (int)std::lround(cy - sz / 2.0f);
    float lr = srgbToLinear(c.r), lg = srgbToLinear(c.g), lb = srgbToLinear(c.b);
    for (int yy = 0; yy < sz; yy++) {
        int py = gy + yy;
        if (py < 0 || py >= m_h) continue;
        float* row = &m_px[(size_t)py * m_w * 4];
        for (int xx = 0; xx < sz; xx++) {
            int px = gx + xx;
            if (px < 0 || px >= m_w) continue;
            float a = bm[yy * sz + xx] / 255.0f * c.a;
            if (a <= 0) continue;
            float* p = row + px * 4;
            float k = 1.0f - a;
            p[0] = lr * a + p[0] * k;
            p[1] = lg * a + p[1] * k;
            p[2] = lb * a + p[2] * k;
            p[3] = a + p[3] * k;
        }
    }
}

float Canvas::measure(const std::string& s, const Font& f, float tracking) {
    float w = 0;
    for (size_t i = 0; i < s.size();) {
        const auto& g = f.glyphs[glyphIndex(nextCodepoint(s, i))];
        w += g.adv64 / 64.0f + tracking;
    }
    return s.empty() ? 0 : w - tracking;
}

float Canvas::text(float x, float baseline, const std::string& s, const Font& f, Colour c, float tracking) {
    float lr = srgbToLinear(c.r), lg = srgbToLinear(c.g), lb = srgbToLinear(c.b);
    for (size_t i = 0; i < s.size();) {
        const auto& g = f.glyphs[glyphIndex(nextCodepoint(s, i))];
        int gx = (int)std::lround(x + g.left), gy = (int)std::lround(baseline - g.top);
        const uint8_t* bm = f.bitmap + g.off;
        for (int yy = 0; yy < g.h; yy++) {
            int py = gy + yy;
            if (py < 0 || py >= m_h) continue;
            float* row = &m_px[(size_t)py * m_w * 4];
            for (int xx = 0; xx < g.w; xx++) {
                int px = gx + xx;
                if (px < 0 || px >= m_w) continue;
                float a = bm[yy * g.w + xx] / 255.0f * c.a;
                if (a <= 0) continue;
                float* p = row + px * 4;
                float k = 1.0f - a;
                p[0] = lr * a + p[0] * k;
                p[1] = lg * a + p[1] * k;
                p[2] = lb * a + p[2] * k;
                p[3] = a + p[3] * k;
            }
        }
        x += g.adv64 / 64.0f + tracking;
    }
    return x;
}

void Canvas::encode(std::vector<uint8_t>& out) const {
    const auto& L = lut();
    out.resize((size_t)m_w * m_h * 4);
    for (size_t i = 0; i < (size_t)m_w * m_h; i++) {
        const float* p = &m_px[i * 4];
        for (int k = 0; k < 3; k++) out[i * 4 + k] = L.v[(int)(std::min(1.0f, std::max(0.0f, p[k])) * 4096.0f + 0.5f)];
        out[i * 4 + 3] = (uint8_t)std::lround(std::min(1.0f, std::max(0.0f, p[3])) * 255.0f);
    }
}

// ------------------------------------------------------------------------------------------------ strip

namespace {

void playIcon(Canvas& c, float x, float cy, HudInfo::Play play, Colour col) {
    float s = 22;
    if (play == HudInfo::Playing) {
        c.fillTriangle(x, cy - s / 2, x, cy + s / 2, x + s * 0.9f, cy, col);
    } else if (play == HudInfo::Parked) {
        c.fillRoundRect(x, cy - s / 2, 7, s, 2, col);
        c.fillRoundRect(x + 12, cy - s / 2, 7, s, 2, col);
    } else {
        c.fillRoundRect(x, cy - s / 2, s * 0.85f, s * 0.85f + 1, 3, col);
    }
}

std::string fpsString(double fps) {
    char b[16];
    if (std::fabs(fps - std::round(fps)) < 0.01) snprintf(b, sizeof b, "%.0f", fps);
    else snprintf(b, sizeof b, "%.2f", fps);
    return b;
}

std::string layoutLabel(const std::string& layout, const std::string& projection) {
    if (layout == "mono") return projection == "flat" ? "Flat" : "Mono " + projection;
    return layout + " " + projection;
}

}  // namespace

void drawStrip(Canvas& c, const HudInfo& in) {
    c.reset(kStripW, kStripH);
    const Font& F = font_ui::kMedium40;
    struct Seg {
        std::string text;
        Colour col;
    };
    std::vector<Seg> segs;
    Colour dot = kGreen;
    bool icon = false;
    // The app the headset is set to listen for; named by software, never by transport (Nelson, 30 Oct: "maybe we
    // don't even say NDI, name the software instead").
    const std::string want = in.source == "ndi" ? "Premiere / After Effects" : in.source == "resolve" ? "Resolve" : "Resolve, Premiere or After Effects";
    if (!in.connected) {
        dot = kAmber;
        segs.push_back({"Waiting to connect", kText});
        segs.push_back({want, in.source == "auto" ? kDim : kAmber});
        segs.push_back({"USB or Wi-Fi", kDim});
    } else if (!in.hasVideo) {
        dot = kAmber;
        segs.push_back({in.usb ? "USB" : "Wi-Fi", kText});
        segs.push_back({in.serverName.rfind("NDI", 0) == 0 ? "Premiere / After Effects" : "Resolve", kText});
        segs.push_back({"waiting for picture", kDim});
    } else {
        segs.push_back({in.usb ? "USB" : "Wi-Fi", kText});
        if (!in.warning.empty()) {
            dot = kAmber;
            segs.push_back({in.warning, kAmber});
        } else if (!in.notice.empty()) {
            segs.push_back({in.notice, kText});
        } else {
            segs.push_back({layoutLabel(in.layout, in.projection), kText});
            char b[64];
            snprintf(b, sizeof b, "%d\xC3\x97%d", in.width, in.height);
            segs.push_back({b, kDim});
            segs.push_back({fpsString(in.fps) + "p", kDim});
            snprintf(b, sizeof b, "%.0f Mb/s", in.mbps);
            segs.push_back({b, kDim});
            segs.push_back({timecodeString(in.timecode), kText});
            icon = true;
        }
    }
    const float gap = 34, padL = 30, padR = 36, dotR = 8;
    // drop the least important segments until the strip fits
    auto total = [&] {
        float w = padL + dotR * 2 + 18 + padR + (icon ? 22 + gap : 0);
        for (size_t i = 0; i < segs.size(); i++) w += Canvas::measure(segs[i].text, F) + (i ? gap : 0);
        return w;
    };
    while (total() > kStripW && segs.size() > 2) segs.erase(segs.end() - (icon ? 2 : 1));
    float w = std::min((float)kStripW, total());
    float x0 = (kStripW - w) / 2;
    c.fillRoundRect(x0, 0, w, kStripH, kStripH / 2.0f, kStripBg);
    float cy = kStripH / 2.0f, baseline = cy + 14;
    float x = x0 + padL;
    c.fillCircle(x + dotR, cy, dotR, dot);
    x += dotR * 2 + 18;
    for (size_t i = 0; i < segs.size(); i++) {
        if (i) {
            c.fillCircle(x + gap / 2, cy + 1, 2.5f, kFaint);
            x += gap;
        }
        x = c.text(x, baseline, segs[i].text, F, segs[i].col);
    }
    if (icon) playIcon(c, x + gap, cy, in.play, kText);
}

void drawCursor(Canvas& c) {
    c.reset(kCursorW, kCursorH);
    // idle: ring
    c.strokeCircle(32, 32, 20, 9, Colour{0, 0, 0, 0.35f});
    c.strokeCircle(32, 32, 18, 5, Colour{1, 1, 1, 0.95f});
    // pinching: filled
    c.fillCircle(96, 32, 18, Colour{0, 0, 0, 0.35f});
    c.fillCircle(96, 32, 14, Colour{1, 1, 1, 0.98f});
}

void drawLift(Canvas& c, float boxW, float boxH, float r, float texPerPx) {
    // A highlight, not a halo: a faint light wash with a thin sheen along the top, floated a few millimetres in
    // front of the hovered control so it reads as lifted in stereo. No feathered edge, no outline (v35).
    // texPerPx is texels per panel pixel, so the sheen is 2 panel pixels tall whatever the control's size.
    c.reset(kLiftW, kLiftH);
    float x = (kLiftW - boxW) / 2, y = (kLiftH - boxH) / 2;
    r = std::min(r, std::min(boxW, boxH) / 2);
    float s = std::max(1.0f, 2.0f * texPerPx);
    c.fillRoundRect(x, y, boxW, boxH, r, Colour{1, 1, 1, 0.08f});
    c.fillRoundRect(x + r / 2, y + s, boxW - r, s, s / 2, Colour{1, 1, 1, 0.22f});
}

void drawOutline(Canvas& c) {
    // Same 160 px box inside a 256 px texture as the halo, so the quad sizing in the scene is shared, but just
    // a 2 px line at half opacity with a hairline dark edge for contrast on bright footage. No glow.
    c.reset(kLiftW, kLiftH);
    const float m = 48, r = 10;
    c.strokeRoundRect(m - 1, m - 1, kLiftW - 2 * m + 2, kLiftH - 2 * m + 2, r + 1, 4, Colour{0, 0, 0, 0.18f});
    c.strokeRoundRect(m, m, kLiftW - 2 * m, kLiftH - 2 * m, r, 2, Colour{kAccentBright.r, kAccentBright.g, kAccentBright.b, 0.55f});
}

void drawGrid(Canvas& c) {
    // Equirectangular level grid: x = yaw (-180 left .. +180 right), y = pitch (+90 top .. -90 bottom).
    // The horizon is the reference; pitch lines and yaw ticks are quieter so the footage stays readable.
    c.reset(kGridW, kGridH);
    const float W = kGridW, H = kGridH;
    auto px = [&](float yawDeg) { return (yawDeg + 180.0f) / 360.0f * W; };
    auto py = [&](float pitchDeg) { return (90.0f - pitchDeg) / 180.0f * H; };
    const Colour line{kText.r, kText.g, kText.b, 0.22f};
    const Colour lineStrong{kText.r, kText.g, kText.b, 0.40f};
    const Colour label{kText.r, kText.g, kText.b, 0.70f};
    const Colour copper{kAccentBright.r, kAccentBright.g, kAccentBright.b, 0.95f};
    const Colour copperGlow{kAccent.r, kAccent.g, kAccent.b, 0.28f};
    // yaw meridians every 30 deg, between +-60 deg of pitch (they converge at the poles and would clutter)
    for (int yaw = -180; yaw < 180; yaw += 30) {
        float x = px((float)yaw);
        c.fillRoundRect(x - 0.75f, py(60), 1.5f, py(-60) - py(60), 0, yaw == 0 ? lineStrong : line);
    }
    // pitch parallels
    const int pitches[] = {10, 20, 30, 45, 60};
    for (int p : pitches) {
        for (int s = -1; s <= 1; s += 2) {
            float y = py((float)(s * p));
            bool strong = p == 30 || p == 60;
            c.fillRoundRect(0, y - 0.75f, W, 1.5f, 0, strong ? lineStrong : line);
            char b[16];
            snprintf(b, sizeof b, "%+d\xC2\xB0", s * p);
            c.text(px(0) + 10, y - 6, b, font_ui::kSemiBold20, label);
        }
    }
    // the horizon: a copper line with a soft glow, ticks every 10 deg, labels every 30
    float yh = py(0);
    c.fillRoundRect(0, yh - 5, W, 10, 0, copperGlow);
    c.fillRoundRect(0, yh - 1.5f, W, 3, 0, copper);
    for (int yaw = -180; yaw < 180; yaw += 10) {
        float x = px((float)yaw);
        bool major = yaw % 30 == 0;
        float t = major ? 18 : 9;
        c.fillRoundRect(x - 1, yh - t, 2, 2 * t, 0, major ? copper : Colour{copper.r, copper.g, copper.b, 0.7f});
        if (major) {
            char b[24];
            if (yaw == 0) snprintf(b, sizeof b, "0\xC2\xB0");
            else snprintf(b, sizeof b, "%d\xC2\xB0 %s", std::abs(yaw), yaw < 0 ? "L" : "R");
            float tw = Canvas::measure(b, font_ui::kSemiBold20);
            c.text(x - tw / 2, yh - 26, b, font_ui::kSemiBold20, label);
        }
    }
    // the word LEVEL at the centre, under the line, so the overlay is never mistaken for footage
    c.text(px(0) + 10, yh + 34, "LEVEL", font_ui::kSemiBold20, Colour{copper.r, copper.g, copper.b, 0.8f}, 3);
}

// ------------------------------------------------------------------------------------------------ renderer

void HudRenderer::start() {
    m_running = true;
    m_thread = std::thread([this] { threadMain(); });
}

void HudRenderer::stop() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_running = false;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void HudRenderer::update(const UiState& s) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_state = s;
    m_dirty = true;
    m_cv.notify_all();
}

bool HudRenderer::takeStrip(std::vector<uint8_t>& px) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_stripReady) return false;
    px.swap(m_stripPx);
    m_stripReady = false;
    return true;
}

bool HudRenderer::takePanel(PanelId id, std::vector<uint8_t>& px, std::vector<ButtonRect>& buttons) {
    std::lock_guard<std::mutex> lk(m_mutex);
    int i = (int)id;
    if (!m_panelReady[i]) return false;
    px.swap(m_panelPx[i]);
    buttons = m_buttons[i];
    m_panelReady[i] = false;
    return true;
}

static std::string stripKey(const HudInfo& i) {
    char b[400];
    snprintf(b, sizeof b, "%d%d%d|%dx%d|%s|%s|%.3f|%.0f|%08x|%d|%s|%s", i.connected, i.usb, i.hasVideo, i.width, i.height,
             i.layout.c_str(), i.projection.c_str(), i.fps, i.mbps, i.timecode, (int)i.play, i.notice.c_str(),
             i.warning.c_str());
    return std::string(b) + "|" + i.source + "|" + i.serverName;
}

static std::string infoKey(const HudInfo& i) {
    char b[600];
    snprintf(b, sizeof b, "%s|%d%d%d%d%d%d%d%d%d|%d|%d|%.0f|%.0f|%.0f|%llu|%llu|%llu", stripKey(i).c_str(), i.layoutAuto,
             i.projectionAuto, i.delayAuto, i.swapEyes, i.hudOn, i.pinned, i.hands, i.grid, i.outline, i.delayMs, (int)i.hz,
             i.arriveMs, i.decodeMs, i.rttMs, (unsigned long long)i.shown, (unsigned long long)i.dropped,
             (unsigned long long)i.uneven);
    return std::string(b) + "|" + i.source + "|" + i.serverName;
}

static std::string pointerKey(const UiState& s, PanelId id) {
    char b[80];
    int hp = s.hoverPanel == (int)id ? (int)s.hover : 0, hpa = s.hoverPanel == (int)id ? s.hoverArg : 0;
    int pp = s.pressedPanel == (int)id ? (int)s.pressed : 0, ppa = s.pressedPanel == (int)id ? s.pressedArg : 0;
    snprintf(b, sizeof b, "|%d.%d|%d.%d|r%d|e%d%d%d%d%d", hp, hpa, pp, ppa, (int)s.rolled[(int)id], s.enabled[0], s.enabled[1],
             s.enabled[2], s.enabled[3], s.enabled[4]);
    return b;
}

// What each panel depends on, so a panel is only redrawn when its own inputs change.
static std::string panelKey(const UiState& s, PanelId id) {
    std::string k = pointerKey(s, id);
    const EditModel* m = s.edit.get();
    switch (id) {
        case PanelId::Instance: {
            char rb[48];
            snprintf(rb, sizeof rb, "|%d|%.0f|%.0f", (int)s.info.rendering, s.info.renderWaitMs / 100, s.info.renderMs);
            k += editInstanceKey(m) + rb;
            break;
        }
        case PanelId::Placement: k += editParamsKey(m, true); break;
        case PanelId::Look: k += infoKey(s.info) + "|" + std::to_string(s.lookTab) + editParamsKey(m, false); break;
        case PanelId::Deck: k += editTimelineKey(m); break;
        case PanelId::Scopes: k += std::to_string(s.info.scopesSeq) + "|" + std::to_string(s.info.scopesView) + "|" + (s.info.scopes ? "1" : "0") + "|" + std::to_string((int)(s.info.scopesAgeMs / 1000)); break;
        default: break;
    }
    return k;
}

void HudRenderer::threadMain() {
    pthread_setname_np(pthread_self(), "VRLink.hud");
    Canvas canvas;
    std::vector<uint8_t> px;
    while (true) {
        UiState s;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [&] { return !m_running || m_dirty; });
            if (!m_running) return;
            m_dirty = false;
            s = m_state;
        }
        std::string sk = stripKey(s.info);
        if (sk != m_stripKey) {
            drawStrip(canvas, s.info);
            canvas.encode(px);
            std::lock_guard<std::mutex> lk(m_mutex);
            m_stripPx.swap(px);
            m_stripReady = true;
            m_stripKey = sk;
        }
        for (int i = 0; i < kPanelCount; i++) {
            if (!s.visible[i]) continue;
            std::string pk = panelKey(s, (PanelId)i);
            if (pk == m_panelKey[i]) continue;
            auto rects = drawPanelById(canvas, (PanelId)i, s);
            canvas.encode(px);
            std::lock_guard<std::mutex> lk(m_mutex);
            m_panelPx[i].swap(px);
            m_buttons[i] = rects;
            m_panelReady[i] = true;
            m_panelKey[i] = pk;
        }
    }
}

}  // namespace vrq
