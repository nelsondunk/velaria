// The Kalyx View panels: Instance strip, Placement (ring / arc / throttles / dials), Look (View settings + the
// macro's remaining sections as tabs) and the Timeline deck. Pure drawing: every interactive region is returned as
// a ButtonRect and the frame loop does the hit-testing and the drags.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>

#include "edit.h"
#include "hud.h"
#include "protocol.h"

namespace vrq {

using font_ui::Font;
using namespace pal;

namespace {

const Font& M30 = font_ui::kMedium30;
const Font& B26 = font_ui::kBold26;
const Font& S20 = font_ui::kSemiBold20;
const Font& S22 = font_ui::kSemiBold22;
const Font& S24 = font_ui::kSemiBold24;
const Font& B18 = font_ui::kBold18;
const Font& B30 = font_ui::kBold30;
const Font& B34 = font_ui::kBold34;
const Font& S16 = font_ui::kSemiBold16;
using icons_ui::Icon;

constexpr float kPi = 3.14159265f;
constexpr float kDeg = kPi / 180;

const PanelSpec kSpecs[kPanelCount] = {
    {1280, 272, 1.00f, 28},   // Instance (v3: 1280 x 272)
    {900, 1000, 0.72f, 28},   // Placement
    {900, 1000, 0.72f, 28},   // Look
    {1400, 340, 1.10f, 28},   // Deck
    {1100, 560, 0.90f, 28},   // Scopes
};

std::string fmtDeg(double v) {
    char b[32];
    snprintf(b, sizeof b, "%s%.1f\xC2\xB0", v > 0 ? "+" : "", v);
    return b;
}

// Keyframe diamond: outline = not animated, filled chalk = animated, filled copper = a key on this frame.
void keyDiamond(Chrome& ch, float cx, float cy, const EditParam& p, int pi) {
    bool khov = ch.hov(HudButton::Key, pi);
    Canvas& c = ch.c;
    if (p.animated) {
        c.fillTriangle(cx, cy - 10, cx + 10, cy, cx, cy + 10, kAccent);
        c.fillTriangle(cx, cy - 10, cx - 10, cy, cx, cy + 10, kAccent);
        c.icon(Icon::Keyframe, cx, cy, 28, kAccentBright);
    } else {
        c.icon(Icon::Keyframe, cx, cy, 28, khov ? kText : kDim);
    }
    ch.rects.push_back({HudButton::Key, cx - 26, cy - 26, 52, 52, pi});
}

// polished copper, the fill of every bar and throttle
const std::vector<GradStop> kBarFill = {{0.0f, hex(0xf6d2ae)}, {0.5f, hex(0xd4925f)}, {0.7f, hex(0xb97448)}, {1.0f, hex(0xe9b386)}};

// a bar with nudge chips and a readout: name on the left of the first line, value on the right, the control beneath
void barRow(Chrome& ch, const EditParam& p, int pi, float x, float y, float w, float rowH, bool drag) {
    Canvas& c = ch.c;
    const float chipW = 60, keyW = 52;
    c.text(x, y + 34, fitText(p.name, S22, w - 220), S22, kText);
    std::string v = valueText(p);
    c.text(x + w - keyW - 16 - Canvas::measure(v, B30), y + 35, v, B30, drag ? kAccentBright : kText);
    keyDiamond(ch, x + w - keyW / 2, y + 24, p, pi);
    float cy = y + rowH - 34;
    float trackX = x + chipW + 14, trackW = w - 2 * (chipW + 14);
    ch.chip3(HudButton::NudgeDown, pi, x, cy - 30, chipW, 60, ChipState::Rest, "", (int)Icon::Minus);
    c.wellV3(trackX, cy - 7, trackW, 14, 7);
    float t = EditModel::toT(p, p.value);
    if (p.min < 0 && p.max > 0 && !EditModel::logScale(p)) {
        float z = trackX + trackW * EditModel::toT(p, 0.0), k = trackX + trackW * t;
        c.fillRoundRectGrad(std::min(z, k), cy - 7, std::max(6.0f, std::fabs(k - z)), 14, 7, kBarFill);
        c.fillRoundRect(z - 1.5f, cy - 14, 3, 28, 1.5f, Colour{1, 1, 1, 0.45f});
    } else {
        c.fillRoundRectGrad(trackX, cy - 7, std::max(14.0f, trackW * t), 14, 7, kBarFill);
    }
    c.knob(trackX + trackW * t, cy, 17, drag);
    ch.rects.push_back({HudButton::Slider, trackX - 24, cy - 34, trackW + 48, 68, pi});
    ch.chip3(HudButton::NudgeUp, pi, trackX + trackW + 14, cy - 30, chipW, 60, ChipState::Rest, "", (int)Icon::Plus);
}

// a 0/1 input is a switch: on is polished copper with the knob right, off is an empty well
void switchRow(Chrome& ch, const EditParam& p, int pi, float x, float y, float w, float rowH) {
    Canvas& c = ch.c;
    c.text(x, y + rowH / 2 + 8, fitText(p.name, S22, 420), S22, kText);
    bool on = p.value != 0;
    float sx = x + 460, cy = y + rowH / 2;
    if (on) {
        c.copper(sx, cy - 26, 96, 52, 26, true);
        c.knob(sx + 96 - 26, cy, 20, false);
    } else {
        c.wellV3(sx, cy - 26, 96, 52, 26);
        c.shadow(sx + 6, cy - 20, 40, 40, 20, 4, 6, 0.6f);
        c.fillCircle(sx + 26, cy, 20, kDim);
    }
    ch.rects.push_back({HudButton::Toggle, sx - 10, y, 116, rowH, pi});
    c.text(sx + 116, cy + 8, on ? "On" : "Off", S22, kDim);
    keyDiamond(ch, x + w - 26, cy, p, pi);
}

void colourRow(Chrome& ch, const EditParam& p, int pi, float x, float y, float w, float rowH, bool drag) {
    Canvas& c = ch.c;
    c.text(x, y + 34, fitText(p.name, S22, w - 200), S22, kText);
    keyDiamond(ch, x + w - 26, y + 24, p, pi);
    float h, sat, val;
    rgbToHsv(p.cr, p.cg, p.cb, h, sat, val);
    float R = (rowH - 70) / 2, cx = x + R + 12, cy = y + 56 + R;
    float discV = std::max(0.35f, val);
    c.shadow(cx - R, cy - R, 2 * R, 2 * R, R, 6, 10, 0.5f);
    c.shadeRect((int)(cx - R - 1), (int)(cy - R - 1), (int)(cx + R + 2), (int)(cy + R + 2), [&](float px, float py) {
        float dx = px - cx, dy = py - cy, d = std::hypot(dx, dy);
        float cov = std::min(1.0f, std::max(0.0f, R + 0.5f - d));
        if (cov <= 0) return Colour{0, 0, 0, 0};
        float hue = std::atan2(-dy, dx) / (2 * kPi);
        if (hue < 0) hue += 1;
        float rr, gg, bb;
        hsvToRgb(hue, std::min(1.0f, d / R), discV, rr, gg, bb);
        return Colour{rr, gg, bb, cov};
    });
    c.strokeCircle(cx, cy, R, 2, Colour{1, 1, 1, 0.22f});
    float ang = h * 2 * kPi;
    float kx = cx + std::cos(ang) * sat * R, ky = cy - std::sin(ang) * sat * R;
    c.fillCircle(kx, ky, 13, Colour{p.cr, p.cg, p.cb, 1});
    c.strokeCircle(kx, ky, 13, 3, Colour{1, 1, 1, 1});
    c.strokeCircle(kx, ky, 16, 1.5f, Colour{0, 0, 0, 0.6f});
    ch.rects.push_back({HudButton::Wheel, cx - R - 12, cy - R - 12, 2 * R + 24, 2 * R + 24, pi});
    // swatch at the right, brightness bar between
    float sw = 96, sx = x + w - sw;
    c.shadow(sx, cy - 40, sw, 80, 14, 6, 10, 0.5f);
    c.fillRoundRect(sx, cy - 40, sw, 80, 14, Colour{p.cr, p.cg, p.cb, 1});
    c.strokeRoundRect(sx, cy - 40, sw, 80, 14, 1.5f, Colour{1, 1, 1, 0.25f});
    float bx = cx + R + 44, bw = sx - 30 - bx, by = cy;
    float br, bg, bb;
    hsvToRgb(h, sat, 1, br, bg, bb);
    c.text(bx, by - 30, "BRIGHTNESS", S16, kDim, 3);
    c.wellV3(bx, by - 8, bw, 16, 8);
    c.shadeRect((int)bx + 2, (int)(by - 6), (int)(bx + bw) - 2, (int)(by + 6), [&](float px, float) {
        float t = std::min(1.0f, std::max(0.0f, (px - bx) / bw));
        return Colour{br * t, bg * t, bb * t, 1};
    });
    c.knob(bx + bw * val, by, 17, drag);
    ch.rects.push_back({HudButton::Bright, bx - 20, by - 36, bw + 40, 72, pi});
    char nb[48];
    snprintf(nb, sizeof nb, "%.2f  %.2f  %.2f", p.cr, p.cg, p.cb);
    c.text(bx, by + 52, nb, S20, kDim);
}

// ---- placement widgets (v3: wells with ticks, copper only for the offset arc and the held control)

// a tick from radius r0 to r1 at screen angle a (0 = +x, counter-clockwise, y up)
static void tick(Canvas& c, float cx, float cy, float a, float r0, float r1, float w, Colour col) {
    float x0 = cx + std::cos(a) * r0, y0 = cy - std::sin(a) * r0, x1 = cx + std::cos(a) * r1, y1 = cy - std::sin(a) * r1;
    int n = std::max(2, (int)(std::hypot(x1 - x0, y1 - y0) / 1.5f));
    for (int i = 0; i <= n; i++) c.fillCircle(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n, w / 2, col);
}

void ringWidget(Chrome& ch, const EditParam& p, int pi, float cx, float cy, float R, bool drag) {
    Canvas& c = ch.c;
    const float T = 22;
    c.shadow(cx - R - T / 2, cy - R - T / 2, 2 * (R + T / 2), 2 * (R + T / 2), R + T / 2, 6, 10, 0.4f);
    c.strokeCircle(cx, cy, R, T, Colour{0, 0, 0, 0.45f});
    c.strokeCircle(cx, cy, R + T / 2, 1, Colour{1, 1, 1, 0.08f});
    // ticks every 15 deg inside the ring, long ones at front, sides and back
    for (int k = 0; k < 24; k++) {
        float a = k * 15 * kDeg + kPi / 2;
        bool major = k % 6 == 0;
        tick(c, cx, cy, a, R - T / 2 - (major ? 22 : 16), R - T / 2 - 4, major ? 3 : 2, Colour{kText.r, kText.g, kText.b, major ? 0.7f : 0.3f});
    }
    // the copper arc is the offset from front
    float yaw = (float)p.value * kDeg;   // + = left
    float a0 = kPi / 2, a1 = kPi / 2 + yaw;
    if (std::fabs(yaw) > 0.5f * kDeg) c.strokeArc(cx, cy, R, 6, std::min(a0, a1), std::max(a0, a1), drag ? kAccentBright : kAccent);
    // white notch at the top is front
    c.fillTriangle(cx - 7, cy - R - T / 2 - 12, cx + 7, cy - R - T / 2 - 12, cx, cy - R - T / 2 - 2, kText);
    float dx = cx - std::sin(yaw) * R, dy = cy - std::cos(yaw) * R;
    c.knob(dx, dy, 17, drag);
    std::string v = fmtDeg(p.value);
    c.text(cx - Canvas::measure(v, B34) / 2, cy + 12, v, B34, drag ? kAccentBright : kText);
    c.text(cx - Canvas::measure("PAN", S16, 3) / 2, cy + 42, "PAN", S16, kDim, 3);
    ch.rects.push_back({HudButton::Ring, cx - R - T, cy - R - T, 2 * (R + T), 2 * (R + T), pi});
    keyDiamond(ch, cx + R + 18, cy - R - 12, p, pi);
}

void arcWidget(Chrome& ch, const EditParam& p, int pi, float cx, float cy, float R, bool drag) {
    Canvas& c = ch.c;
    const float T = 22;
    const float lim = 90 * kDeg;
    c.strokeArc(cx, cy, R, T, -lim, lim, Colour{0, 0, 0, 0.45f});
    for (int k = -6; k <= 6; k++) {
        float a = k * 15 * kDeg;
        bool major = k % 3 == 0;
        tick(c, cx, cy, a, R - T / 2 - (major ? 22 : 16), R - T / 2 - 4, major ? 3 : 2, Colour{kText.r, kText.g, kText.b, major ? 0.7f : 0.3f});
    }
    float pitch = std::max(-lim, std::min(lim, (float)p.value * kDeg));
    if (std::fabs(pitch) > 0.5f * kDeg) c.strokeArc(cx, cy, R, 6, std::min(0.0f, pitch), std::max(0.0f, pitch), drag ? kAccentBright : kAccent);
    // the horizon is the white line
    c.fillRoundRect(cx, cy - 1.5f, R - T / 2 - 26, 3, 1.5f, kText);
    float dx = cx + std::cos(pitch) * R, dy = cy - std::sin(pitch) * R;
    c.knob(dx, dy, 17, drag);
    std::string v = fmtDeg(p.value);
    c.text(cx - 12, cy + R + T / 2 + 38, v, B34, drag ? kAccentBright : kText);
    c.text(cx - 12, cy + R + T / 2 + 64, "TILT", S16, kDim, 3);
    ch.rects.push_back({HudButton::Arc, cx - 10, cy - R - T, R + T + 10, 2 * (R + T), pi});
    keyDiamond(ch, cx + R + 18, cy - R - 12, p, pi);
}

void dialWidget(Chrome& ch, const EditParam& p, int pi, float cx, float cy, float R, const std::string& label, bool drag) {
    Canvas& c = ch.c;
    c.shadow(cx - R, cy - R, 2 * R, 2 * R, R, 5, 8, 0.4f);
    c.fillCircle(cx, cy, R, Colour{0, 0, 0, 0.45f});
    c.strokeCircle(cx, cy, R, drag ? 4 : 2, drag ? kAccent : Colour{1, 1, 1, 0.12f});
    for (int k = 0; k < 12; k++) {
        float a = k * 30 * kDeg + kPi / 2;
        bool major = k % 3 == 0;
        tick(c, cx, cy, a, R - (major ? 14 : 10), R - 4, major ? 3 : 2, Colour{kText.r, kText.g, kText.b, major ? 0.7f : 0.3f});
    }
    float ang = (float)p.value * kDeg;
    float px = cx + std::sin(ang) * (R - 20), py = cy - std::cos(ang) * (R - 20);
    Colour pc = drag ? kAccent : kText;
    tick(c, cx, cy, kPi / 2 - ang, 0, R - 20, 6, pc);
    c.fillCircle(cx, cy, 9, pc);
    (void)px; (void)py;
    std::string v = fmtDeg(p.value);
    c.text(cx - Canvas::measure(v, B26) / 2, cy + R + 34, v, B26, drag ? kAccentBright : kText);
    c.text(cx - Canvas::measure(label, S16, 3) / 2, cy + R + 58, label, S16, kDim, 3);
    ch.rects.push_back({HudButton::Dial, cx - R - 10, cy - R - 10, 2 * R + 20, 2 * R + 20, pi});
    keyDiamond(ch, cx + R + 6, cy - R - 6, p, pi);
}

void throttleWidget(Chrome& ch, const EditParam& p, int pi, float x, float y, float w, float h, const std::string& label,
                    bool drag) {
    Canvas& c = ch.c;
    c.wellV3(x, y, w, h, 14);
    float t = EditModel::toT(p, p.value);   // 0 bottom .. 1 top
    float fillH = (h - 12) * t;
    if (fillH > 4) {
        c.fillRoundRectGrad(x + 6, y + h - 6 - fillH, w - 12, fillH, 10, {{0, hex(0xe0a97a, drag ? 0.5f : 0.35f)}, {1, hex(0xc9875a, 0.20f)}});
        c.fillRoundRect(x + 10, y + h - 6 - fillH, w - 20, 2, 1, drag ? kAccentBright : kAccent);
    }
    float ky = y + h - 6 - fillH;
    c.shadow(x + 3, ky - 14, w - 6, 28, 9, 6, 10, 0.6f);
    if (drag) c.fillRoundRect(x, ky - 17, w, 34, 11, kAccent);
    c.fillRoundRectGrad(x + 3, ky - 14, w - 6, 28, 9, {{0, Colour{1, 1, 1, 1}}, {1, hex(0xcfd3d8)}});
    ch.rects.push_back({HudButton::Throttle, x - 16, y - 20, w + 32, h + 40, pi});
    // real units beside the travel
    bool dist = label == "DISTANCE";
    if (dist || label == "SCALE") {
        static const double dm[] = {0.5, 1, 2, 5, 20};
        static const double sm[] = {0.25, 0.5, 1, 2, 6};
        for (int i = 0; i < 5; i++) {
            double val = dist ? -dm[i] : sm[i];
            if (val < std::min(p.min, p.max) || val > std::max(p.min, p.max)) continue;
            float yy = y + h - 6 - (h - 12) * EditModel::toT(p, val);
            char b[16];
            snprintf(b, sizeof b, dist ? "%g m" : "%g\xC3\x97", dist ? dm[i] : sm[i]);
            c.fillRoundRect(x + w + 6, yy - 0.5f, 8, 1, 0.5f, Colour{1, 1, 1, 0.3f});
            c.text(x + w + 20, yy + 6, b, S16, kDim);
        }
    }
    std::string v = valueText(p);
    if (dist) {
        char b[32];
        snprintf(b, sizeof b, "%.2f m", std::fabs(p.value));
        v = b;
    } else if (label == "SCALE") {
        char b[32];
        snprintf(b, sizeof b, "%.2f\xC3\x97", p.value);
        v = b;
    }
    c.text(x, y + h + 40, v, B30, drag ? kAccentBright : kText);
    c.text(x, y + h + 64, label, S16, kDim, 3);
    keyDiamond(ch, x + w + 34, y - 10, p, pi);
}

// ---- the panels

// A readable name for a group's tool: no Skylight_/Kalyx_ prefixes, the macro families spelled out.
static std::string prettyTool(const std::string& t) {
    std::string n = t;
    for (const char* pre : {"Skylight_", "Skylight", "Kalyx_", "Kalyx"})
        if (n.rfind(pre, 0) == 0) { n = n.substr(strlen(pre)); break; }
    auto sub = [&](const char* from, const char* to) { size_t i = n.find(from); if (i != std::string::npos) n.replace(i, strlen(from), to); };
    sub("FactCard360", "Fact Card \xC2\xB7 ");
    sub("3DCard_", "3D Card \xC2\xB7 ");
    sub("3DTitle_", "3D Title \xC2\xB7 ");
    sub("FactWindow", "Footage window");
    sub("CanvasControl", "Footage window");
    sub("Orient360", "Orient 360");
    sub("Trim", "Trim");
    sub("LatLongPatcher", "Patch ");
    sub("PanoMap", "PanoMap ");
    std::string o;
    for (size_t i = 0; i < n.size(); i++) {
        if (i > 0 && isupper((unsigned char)n[i]) && islower((unsigned char)n[i - 1])) o += ' ';
        o += n[i];
    }
    n = o;
    while (!n.empty() && n.back() == ' ') n.pop_back();
    return n.empty() ? t : n;
}

// Timecode of another frame on the same timeline, from the current frame's timecode (non-drop arithmetic).
static std::string tcForFrame(const EditModel& m, int64_t frame) {
    int hh = 0, mm = 0, ss = 0, ff = 0;
    if (sscanf(m.tc.c_str(), "%d:%d:%d:%d", &hh, &mm, &ss, &ff) != 4) return "";
    int fps = std::max(1, (int)std::lround(m.fps));
    int64_t total = ((int64_t)hh * 3600 + mm * 60 + ss) * fps + ff + (frame - m.frame);
    if (total < 0) total = 0;
    char b[24];
    snprintf(b, sizeof b, "%02d:%02d:%02d:%02d", (int)(total / fps / 3600) % 24, (int)(total / fps / 60) % 60, (int)(total / fps) % 60, (int)(total % fps));
    return b;
}

// The nearest clip with a Fusion comp on an upper track before / after the playhead (-1 = none).
static int nearestOverlayClip(const EditModel& m, int dir) {
    int best = -1;
    for (int pass = 0; pass < 2 && best < 0; pass++) {
        for (int i = 0; i < (int)m.clips.size(); i++) {
            const EditClip& c = m.clips[i];
            if (!c.fusion || (pass == 0 && c.track < 2)) continue;
            if (dir < 0 ? c.end > m.frame : c.start <= m.frame) continue;
            if (best < 0 || (dir < 0 ? c.start > m.clips[best].start : c.start < m.clips[best].start)) best = i;
        }
    }
    return best;
}

// The Instance strip, UI v3 (30 Oct 2026): which layer am I on, and which thing will my hand move?
std::vector<ButtonRect> drawInstance(Canvas& c, const UiState& s) {
    std::vector<ButtonRect> rects;
    Chrome ch{c, rects, s, PanelId::Instance};
    const PanelSpec& sp = kSpecs[(int)PanelId::Instance];
    c.reset(sp.w, sp.h);
    const EditModel* m = s.edit.get();
    const EditGroup* g = m ? m->current() : nullptr;
    // no copper top edge when nothing here is under your hand
    bool hairline = ch.focused() && g;
    bool rolled = s.rolled[(int)PanelId::Instance];
    c.glassV3(sp.radius, hairline, rolled ? (float)kRolledH : -1.0f);
    const float barH = rolled ? (float)kRolledH : 76.0f, barCy = barH / 2;
    // ---- bar: grab pill, the layer control, the panel toggles, roll-up
    {
        bool grab = ch.hov(HudButton::Move) || ch.prs(HudButton::Move);
        c.fillRoundRect(24, barCy - 4, 56, 8, 4, Colour{1, 1, 1, grab ? 0.6f : g ? 0.30f : 0.20f});
        rects.push_back({HudButton::Move, 0, 0, (float)sp.w - 80, barH, 0});
        int layer = m ? m->layer : 0;
        struct L { const char* label; Icon icon; };
        static const L ls[3] = {{"Overlays", Icon::Overlays}, {"Patches", Icon::Patches}, {"Picture", Icon::Picture}};
        float x = 24 + 56 + 14;
        float segX = x;
        float ws[3];
        float segW = 8;
        for (int l = 0; l < 3; l++) {
            int n = m ? m->countOnLayer(l) : 0;
            ws[l] = Chrome::chipWidth(ls[l].label, (int)ls[l].icon, std::to_string(n), 52, 18);
            segW += ws[l] + (l ? 4 : 0);
        }
        c.wellV3(segX, barCy - 30, segW, 60, 18);
        x = segX + 4;
        for (int l = 0; l < 3; l++) {
            int n = m ? m->countOnLayer(l) : 0;
            ch.chip3(HudButton::Layer, l, x, barCy - 26, ws[l], 52, layer == l ? ChipState::On : ChipState::Flat, ls[l].label, (int)ls[l].icon, std::to_string(n));
            x += ws[l] + 4;
        }
        // panel toggles: raised = shown, flat = hidden; then the roll-up chevron
        struct Dock { PanelId id; Icon icon; const char* tip; };
        static const Dock dock[4] = {{PanelId::Placement, Icon::Placement, "Placement"}, {PanelId::Look, Icon::Look, "Look"}, {PanelId::Deck, Icon::Timeline, "Timeline"}, {PanelId::Scopes, Icon::Scopes, "Scopes"}};
        float rx = sp.w - 14 - 52;
        ch.chip3(HudButton::Roll, 0, rx, barCy - 26, 52, 52, ChipState::Flat, "", (int)Icon::RollUp, "", rolled ? "Open" : "Roll up");
        rx -= 14;
        c.fillRoundRect(rx - 1, barCy - 18, 1, 36, 0.5f, Colour{1, 1, 1, 0.12f});
        rx -= 14;
        for (int k = 3; k >= 0; k--) {
            rx -= 52;
            ch.chip3(HudButton::PanelToggle, (int)dock[k].id, rx, barCy - 26, 52, 52, s.enabled[(int)dock[k].id] ? ChipState::Rest : ChipState::Flat, "", (int)dock[k].icon, "", dock[k].tip);
            rx -= 14;
        }
        if (rolled) return rects;
        c.fillRoundRect(0, barH - 1, (float)sp.w, 1, 0, Colour{1, 1, 1, 0.08f});
    }
    const float rowA = barH + 14, rowB = rowA + 60 + 8, rowC = rowB + 36 + 8;   // tops: 90, 158, 202
    const float padL = 24, padR = 24;
    const float metaX = padL + 60 + 10;   // row B and C text lines up with the first named chip
    if (!m || !m->bridgeOk) {
        c.text(padL, rowA + 40, "The Resolve bridge isn't running on the Mac", S24, kText);
        c.text(padL, rowB + 26, "python3 ~/VRLink/mac/bridge/vrlink_bridge.py \xC2\xB7 it reconnects on its own once it is up", S20, kDim);
        return rects;
    }
    auto statusLine = [&](float y) {
        // row C left: ice status with an icon; never a chip
        float x = metaX;
        Icon ic = s.info.rendering ? Icon::Rendering : s.info.usb ? Icon::Usb : Icon::Wifi;
        c.icon(ic, x + 12, y + 30, 24, kIce);
        x += 36;
        std::string st = !s.info.connected ? "Waiting to connect" : std::string(s.info.usb ? "USB" : "Wi-Fi") + " \xC2\xB7 " + (s.info.serverName.rfind("NDI", 0) == 0 ? "Premiere / After Effects" : "Resolve");
        if (!s.info.setup.empty()) {
            // a Resolve setup problem with its fix, in Resolve's menu words, takes the whole line (the link is on the View tab)
            c.icon(Icon::Warning, x + 12, y + 30, 24, kIce);
            return c.text(x + 30, y + 37, fitText(s.info.setup, S20, sp.w - padR - (x + 30)), S20, kIce);
        }
        x = c.text(x, y + 37, st, S20, kIce);
        char rb[64];
        std::string rs;
        if (s.info.rendering) snprintf(rb, sizeof rb, "Rendering\xE2\x80\xA6 %.1f s", s.info.renderWaitMs / 1000), rs = rb;
        else if (s.info.renderMs > 0) snprintf(rb, sizeof rb, "Resolve updated in %.1f s", s.info.renderMs / 1000), rs = rb;
        if (!rs.empty()) {
            c.fillRoundRect(x + 12, y + 18, 1, 24, 0.5f, Colour{1, 1, 1, 0.20f});
            x = c.text(x + 25, y + 37, rs, S20, kIce);
        }
        return x;
    };
    if (!g) {
        // ---- nothing on this layer at this frame: say why, and point at the nearest overlay either side
        const char* what = m->layer == 0 ? "place" : m->layer == 1 ? "patch" : "re-orient";
        std::string line = std::string("Nothing to ") + what + " at " + m->tc + ".";
        c.text(padL, rowA + 38, line, S24, kText);
        float rx = sp.w - padR;
        int nx = nearestOverlayClip(*m, 1), pv = nearestOverlayClip(*m, -1);
        if (nx >= 0) {
            std::string lb = "Next \xC2\xB7 " + tcForFrame(*m, m->clips[nx].start);
            float w = Chrome::chipWidth(lb, -1, "") + 38;
            rx -= w;
            ch.chip3(HudButton::Jump, (int)m->clips[nx].start, rx, rowA, w, 60, ChipState::Rest, lb + " \xE2\x80\xBA");
            rx -= 10;
        }
        if (pv >= 0) {
            std::string lb = "\xE2\x80\xB9 Previous overlay \xC2\xB7 " + tcForFrame(*m, m->clips[pv].start);
            float w = Chrome::chipWidth(lb, -1, "");
            rx -= w;
            ch.chip3(HudButton::Jump, (int)m->clips[pv].start, rx, rowA, w, 60, ChipState::Rest, lb);
        }
        std::string where = m->tc;
        std::string elsewhere;
        for (int l = 0; l < 3; l++) {
            if (l == m->layer) continue;
            int n = m->countOnLayer(l);
            if (n > 0) elsewhere += (elsewhere.empty() ? "" : " \xC2\xB7 ") + std::to_string(n) + (l == 0 ? " overlay" : l == 1 ? " patch" : " picture control") + (n > 1 ? (l == 1 ? "es" : "s") : "");
        }
        // the clip under the playhead on V1, or what the other layers hold
        std::string plate;
        for (auto& cl : m->clips) if (cl.track == 1 && m->frame >= cl.start && m->frame < cl.end) plate = cl.name;
        float wx = padL;
        wx = c.text(wx, rowB + 26, where, S20, kText);
        if (!plate.empty()) wx = c.text(wx, rowB + 26, "  \xC2\xB7  V1  \xC2\xB7  " + fitText(plate, S20, 500), S20, kDim);
        if (!elsewhere.empty()) c.text(wx, rowB + 26, fitText("  \xC2\xB7  " + elsewhere + " on the other layers", S20, sp.w - padR - wx), S20, kDim);
        else if (plate.empty()) c.text(wx, rowB + 26, "  \xC2\xB7  plate only", S20, kDim);
        statusLine(rowC);
        return rects;
    }
    // ---- row A: every group on this layer, the picked one in copper; prev / next; the actions
    bool patches = m->layer != 0;
    bool locked = m->isLocked(*g);
    {
        std::vector<int> idx;
        for (int i = 0; i < (int)m->groups.size(); i++) if (m->inLayer(m->groups[i])) idx.push_back(i);
        // right side first, so the names know how much room they have
        float rx = sp.w - padR;
        if (patches) {
            float w = Chrome::chipWidth(locked ? "Locked" : "Lock", (int)Icon::Lock, "");
            rx -= w;
            ch.chip3(HudButton::Lock, 0, rx, rowA, w, 60, locked ? ChipState::On : ChipState::Rest, locked ? "Locked" : "Lock", (int)(locked ? Icon::Lock : Icon::Unlock));
            rx -= 10;
            rx -= 60;
            ch.iconChip(HudButton::Undo, 0, rx, rowA, 60, ChipState::Rest, Icon::Undo, "Undo");
        } else {
            rx -= 60;
            ch.iconChip(HudButton::Undo, 0, rx, rowA, 60, ChipState::Rest, Icon::Undo, "Undo");
            rx -= 10 + 60;
            ch.iconChip(HudButton::Paste, 0, rx, rowA, 60, m->clipboard.empty() ? ChipState::Off : ChipState::Rest, Icon::Paste, m->clipboard.empty() ? "" : "Paste " + m->clipboardFrom);
            rx -= 10 + 60;
            ch.iconChip(HudButton::Copy, 0, rx, rowA, 60, ChipState::Rest, Icon::Copy, "Copy settings");
        }
        float limit = rx - 24;
        float x = padL;
        bool many = idx.size() > 1;
        if (many) {
            ch.iconChip(HudButton::GroupPrev, 0, x, rowA, 60, ChipState::Flat, Icon::Previous, "Previous");
            x += 60 + 10;
        } else {
            x = metaX;
        }
        // names: the picked one first in order, then as many neighbours as fit, then "n more"
        float nextW = many ? 60 + 10 : 0;
        int shown = 0;
        for (size_t k = 0; k < idx.size(); k++) {
            int i = idx[k];
            const EditGroup& gi = m->groups[i];
            std::string name = prettyTool(gi.tool);
            name = fitText(name, S22, 420);
            bool lk = m->isLocked(gi);
            int icon = (gi.layer() != 0 && lk) ? (int)Icon::Lock : -1;
            std::string tag = "V" + std::to_string(gi.track);
            float w = Chrome::chipWidth(name, icon, tag);
            int remaining = (int)idx.size() - (int)k - 1;
            float moreW = remaining > 0 ? Chrome::chipWidth(std::to_string(remaining) + " more", -1, "") + 10 : 0;
            if (x + w + nextW + (remaining > 0 ? moreW : 0) > limit && shown > 0) {
                std::string more = std::to_string(remaining + 1) + " more";
                float mw = Chrome::chipWidth(more, -1, "");
                if (x + mw + nextW <= limit) ch.chip3(HudButton::GroupNext, 0, x, rowA, mw, 60, ChipState::Flat, more), x += mw + 10;
                break;
            }
            ch.chip3(HudButton::GroupPick, i, x, rowA, w, 60, i == m->group ? ChipState::On : ChipState::Rest, name, icon, tag);
            x += w + 10;
            shown++;
        }
        if (many) ch.iconChip(HudButton::GroupNext, 0, x, rowA, 60, ChipState::Flat, Icon::Next, "Next");
    }
    // ---- row B: where we are
    {
        float wx = metaX;
        wx = c.text(wx, rowB + 26, m->tc, S20, kText);
        std::string rest = "  \xC2\xB7  V" + std::to_string(g->track) + "  \xC2\xB7  " + g->clipName;
        if (!g->source.empty()) rest += "  \xC2\xB7  " + g->source;
        c.text(wx, rowB + 26, fitText(rest, S20, sp.w - padR - wx), S20, kDim);
    }
    // ---- row C: status left, hint right
    {
        float x = statusLine(rowC);
        std::string hint;
        if (!m->notice.empty()) hint = m->notice;
        else if (locked) hint = "Locked. Tap Locked to edit this patch.";
        else if (g && g->kind == "trim") hint = "Trim pass on the whole picture: its bars are on the Placement panel.";
        else if (patches) hint = "Patches aren't pickable in the picture. Tap a name, then use Placement.";
        else hint = "Pinch the card in the picture to pick it, or tap a name.";
        float avail = sp.w - padR - x - 24;
        std::string h = fitText(hint, S20, avail);
        c.text(sp.w - padR - Canvas::measure(h, S20), rowC + 37, h, S20, locked || !m->notice.empty() ? kText : kDim);
    }
    return rects;
}

void sectionRows(Chrome& ch, const EditModel& m, const EditGroup& g, const std::string& section, float y, float pad);

std::vector<ButtonRect> drawPlacement(Canvas& c, const UiState& s) {
    std::vector<ButtonRect> rects;
    Chrome ch{c, rects, s, PanelId::Placement};
    const PanelSpec& sp = kSpecs[(int)PanelId::Placement];
    c.reset(sp.w, sp.h);
    const EditModel* m = s.edit.get();
    const EditGroup* g = m ? m->current() : nullptr;
    // a PanoMap / LatLong Patcher turns the whole sphere rather than placing a card on it
    bool sphere = g && !g->isCard();
    bool locked = g && m->isLocked(*g);
    // the bar names the clip being controlled (Nelson, 2 Nov: "just give me the name of the clip")
    if (g) ch.subtitle = g->clipName;
    float y = ch.begin(g && g->kind == "patch" ? "PATCH WINDOW" : g && g->kind == "trim" ? "TRIM" : sphere ? "PICTURE \xC2\xB7 WHOLE SPHERE" : "PLACEMENT");
    if (y < 0) return rects;
    if (g && g->kind == "trim") {
        // a trim has nothing to place: its bars take the panel (+50; they were a Look tab in +48/49)
        c.text(40, y + 14, "TRIM PASS \xC2\xB7 WHOLE PICTURE", S16, kDim, 3);
        sectionRows(ch, *m, *g, "Trim", y + 44, 40);
        return rects;
    }
    if (!g) {
        if (m && m->bridgeOk && m->layer == 1) {
            c.text(40, y + 60, "No patch window at this frame.", M30, kDim);
            c.text(40, y + 104, "The plate stays as it is set in Resolve.", M30, Colour{1, 1, 1, 0.38f});
        } else if (m && m->bridgeOk && m->layer == 2) {
            c.text(40, y + 60, "No PanoMap or Orient 360 at this frame.", M30, kDim);
            c.text(40, y + 104, "Add Kalyx Orient 360 at the end of the plate's chain.", M30, Colour{1, 1, 1, 0.38f});
        } else {
            c.text(40, y + 60, m && m->bridgeOk ? "No overlay at this frame." : "Waiting for the Resolve bridge\xE2\x80\xA6", M30, kDim);
            c.text(40, y + 104, "Park on a clip with a Kalyx overlay (title or footage window).", M30, Colour{1, 1, 1, 0.38f});
        }
        return rects;
    }
    if (locked) {
        // read-only: the dials still show the values, nothing moves them (every write is gated on the lock)
        float tw = Canvas::measure("LOCKED", S20, 3) + 20;
        c.fillRoundRect(sp.w - 40 - tw, y + 8, tw, 32, 8, Colour{kIce.r, kIce.g, kIce.b, 0.22f});
        c.text(sp.w - 40 - tw + 10, y + 30, "LOCKED", S20, kIce, 3);
    }
    int ring = -1, arc = -1;
    std::vector<int> dials, throttles;
    for (int i = 0; i < (int)g->params.size(); i++) {
        const EditParam& p = g->params[i];
        if (!onPlacementPanel(p)) continue;
        switch (widgetFor(p)) {
            case Widget::Ring: if (ring < 0) ring = i; break;
            case Widget::Arc: if (arc < 0) arc = i; break;
            case Widget::Dial: dials.push_back(i); break;
            case Widget::Throttle: throttles.push_back(i); break;
            default: dials.push_back(i); break;   // unexpected bars become dials (they are angles in practice)
        }
    }
    int dragRow = m->dragRow;
    c.text(40, y + 14, sphere ? "SPHERE \xC2\xB7 PAN AND TILT" : "POSITION ON SPHERE", S16, kDim, 3);
    // row 1: ring + arc
    float ry = y + 190;
    if (ring >= 0) ringWidget(ch, g->params[ring], ring, 220, ry, 140, dragRow == ring);
    if (arc >= 0) arcWidget(ch, g->params[arc], arc, 520, ry, 140, dragRow == arc);
    // row 2: throttles left, dials right
    float ty = ry + 262;
    if (!throttles.empty()) c.text(40, ty - 22, "SCALE \xC2\xB7 DISTANCE", S16, kDim, 3);
    if (!dials.empty()) c.text(throttles.empty() ? 40 : 400, ty - 22, sphere ? "ROLL \xC2\xB7 LEVEL THE HORIZON" : "CARD ROTATION", S16, kDim, 3);
    float tx = 48;
    for (int i : throttles) {
        const EditParam& p = g->params[i];
        std::string label = (p.id == "@size" || p.id == "Size") ? "SCALE" : "DISTANCE";
        throttleWidget(ch, p, i, tx, ty, 60, 200, label, dragRow == i);
        tx += 170;
    }
    float dx0 = throttles.empty() ? 40.0f : std::max(tx + 10.0f, 400.0f);
    float cell = (sp.w - 40 - dx0) / 2;
    for (size_t k = 0; k < dials.size(); k++) {
        const EditParam& p = g->params[dials[k]];
        std::string label = p.id == "Roll" ? "ROLL" : p.id == "@spin" || p.id == "ObjectSpin" ? "CARD ROLL"
                            : p.id == "@turn" || p.id == "ObjectTurn" ? "CARD YAW" : p.id == "@tilt" || p.id == "ObjectTilt" ? "CARD PITCH"
                            : fitText(p.name, S16, cell - 20);
        for (auto& ch_ : label) ch_ = (char)toupper((unsigned char)ch_);
        float cx = dx0 + cell * (k % 2) + cell / 2, cy = ty + 62 + (k / 2) * 182;
        if (cy + 118 > sp.h - 40) break;
        dialWidget(ch, p, dials[k], cx, cy, 54, label, dragRow == dials[k]);
    }
    // footer hint
    std::string hint = !m->notice.empty() ? m->notice
                       : sphere ? "Level grid (View tab) shows true level \xC2\xB7 bring the horizon to the line"
                                : "Pinch a dot and move \xC2\xB7 tap a dial to zero it";
    c.text(40, sp.h - 30, fitText(hint, S20, sp.w - 80), S20, !m->notice.empty() ? kText : kDim);
    return rects;
}

void drawViewTab(Chrome& ch, float y) {
    Canvas& c = ch.c;
    const HudInfo& in = ch.s.info;
    const PanelSpec& sp = kSpecs[(int)PanelId::Look];
    const float pad = 40, inner = sp.w - 2 * pad, gap = 12, ch_h = 54;
    auto row = [&](const char* label, const std::string& value, Colour vc = kText) {
        c.text(pad, y + 28, label, S16, kDim, 3);
        c.text(pad, y + 60, fitText(value, S22, inner), S22, vc);
        y += 80;
    };
    char b[200];
    // ---- what is on screen (two lines)
    if (!in.hasVideo) {
        bool ndi = in.serverName.rfind("NDI", 0) == 0;
        if (in.connected && ndi) {
            row("STATUS", "Connected to Premiere / After Effects. Waiting for frames.");
            row("SENDER", "Press play or scrub \xC2\xB7 Settings \xE2\x80\xBA Playback \xE2\x80\xBA Mercury Transmit + NDI");
        } else {
            if (in.connected)
                row("STATUS", "Connected to " + (in.serverName.empty() ? std::string("Resolve") : in.serverName) + ". Waiting for a picture.");
            else if (in.source == "ndi")
                row("STATUS", "Waiting to connect \xC2\xB7 set to Premiere / After Effects only.", kSignal);
            else if (in.source == "resolve")
                row("STATUS", "Waiting to connect \xC2\xB7 set to Resolve only \xC2\xB7 USB or Wi-Fi\xE2\x80\xA6");
            else
                row("STATUS", "Waiting to connect \xC2\xB7 Resolve, Premiere or After Effects \xC2\xB7 USB or Wi-Fi\xE2\x80\xA6");
            if (in.source == "ndi")
                row("SOURCE", "For Resolve, tap SOURCE below to Auto \xC2\xB7 Adobe needs the NDI Tools output on", kSignal);
            else
                row("RESOLVE", "Video I/O \xE2\x80\xBA Kalyx (Quest) \xC2\xB7 square 360: output scaling 3840");
        }
    } else {
        snprintf(b, sizeof b, "%d \xC3\x97 %d  \xC2\xB7  %s %s  \xC2\xB7  %.2f fps  \xC2\xB7  %.0f Mb/s", in.width, in.height,
                 in.layout.c_str(), in.projection.c_str(), in.fps, in.mbps);
        row("STREAM", b);
        const char* play = in.play == HudInfo::Playing ? "Playing" : in.play == HudInfo::Parked ? "Parked" : "Stopped";
        snprintf(b, sizeof b, "%s  \xC2\xB7  %s  \xC2\xB7  delay %d ms%s  \xC2\xB7  %.0f Hz", play, timecodeString(in.timecode).c_str(),
                 in.delayMs, in.delayAuto ? "" : " (manual)", in.hz);
        row("PLAYBACK", b, in.warning.empty() ? kText : kSignal);
    }
    y += 6;
    c.fillRoundRect(pad, y, inner, 2, 1, kFaint);
    y += 18;
    // ---- controls, in groups
    auto group = [&](const char* name) {
        c.text(pad, y + 16, name, S16, kDim, 3);
        y += 30;
    };
    auto cols = [&](int n, int i, float& x, float& w) {
        w = (inner - gap * (n - 1)) / n;
        x = pad + i * (w + gap);
    };
    float x, w;
    group("SOURCE");
    {
        std::string src = in.source == "ndi" ? "Premiere / After Effects" : in.source == "resolve" ? "Resolve" : "Auto";
        std::string label = "Source: " + src;
        if (!in.serverName.empty() && in.connected) label += "  \xC2\xB7  " + in.serverName;
        ch.chip(HudButton::Source, 0, pad, y, inner, ch_h, fitText(label, S20, inner - 30), in.source != "auto");
        y += ch_h + gap + 8;
    }
    group("PICTURE");
    cols(3, 0, x, w);
    ch.chip(HudButton::Layout, 0, x, y, w, ch_h, std::string("Layout: ") + (in.layoutAuto ? "Auto" : in.layout), !in.layoutAuto);
    cols(3, 1, x, w);
    ch.chip(HudButton::Projection, 0, x, y, w, ch_h, std::string("Projection: ") + (in.projectionAuto ? "Auto" : in.projection), !in.projectionAuto);
    cols(3, 2, x, w);
    ch.chip(HudButton::SwapEyes, 0, x, y, w, ch_h, "Swap eyes", in.swapEyes);
    y += ch_h + gap;
    {
        float small = 110, mid = inner - 2 * small - 2 * gap;
        ch.chip(HudButton::DelayDown, 0, pad, y, small, ch_h, "\xE2\x88\x92 10 ms", false);
        snprintf(b, sizeof b, "Delay %d ms %s", in.delayMs, in.delayAuto ? "\xC2\xB7 auto" : "\xC2\xB7 tap for auto");
        ch.chip(HudButton::DelayAuto, 0, pad + small + gap, y, mid, ch_h, b, in.delayAuto);
        ch.chip(HudButton::DelayUp, 0, pad + small + gap + mid + gap, y, small, ch_h, "+ 10 ms", false);
        y += ch_h + gap;
        if (in.projection == "fisheye") {
            // lens presets (field of view the image circle covers), then a fine trim
            struct Lens { const char* name; int fov; };
            static const Lens lenses[] = {{"Canon 5.2", 190}, {"Canon 3.9", 144}, {"180\xC2\xB0", 180}, {"200\xC2\xB0", 200}, {"220\xC2\xB0", 220}};
            float lw = (inner - gap * 4) / 5;
            for (int i = 0; i < 5; i++)
                ch.chip3(HudButton::FovPreset, lenses[i].fov, pad + i * (lw + gap), y, lw, ch_h, std::lround(in.fisheyeFov) == lenses[i].fov ? ChipState::On : ChipState::Rest, lenses[i].name);
            y += ch_h + gap;
            ch.chip3(HudButton::FovDown, 0, pad, y, small, ch_h, ChipState::Rest, "5\xC2\xB0", (int)Icon::Minus);
            snprintf(b, sizeof b, "Lens field of view %.0f\xC2\xB0  \xC2\xB7  circle inscribed per eye", in.fisheyeFov);
            ch.chip3(HudButton::None, 0, pad + small + gap, y, mid, ch_h, ChipState::Flat, b);
            ch.chip3(HudButton::FovUp, 0, pad + small + gap + mid + gap, y, small, ch_h, ChipState::Rest, "5\xC2\xB0", (int)Icon::Plus);
            y += ch_h + gap;
        }
        y += 8;
    }
    group("OVERLAYS");
    cols(3, 0, x, w);
    ch.chip(HudButton::Hud, 0, x, y, w, ch_h, "Status bar", in.hudOn);
    cols(3, 1, x, w);
    ch.chip(HudButton::Grid, 0, x, y, w, ch_h, "Level grid", in.grid);
    cols(3, 2, x, w);
    ch.chip(HudButton::Outline, 0, x, y, w, ch_h, "Card outline", in.outline);
    y += ch_h + gap + 8;
    group("PANELS");
    cols(2, 0, x, w);
    ch.chip(HudButton::ResetView, 0, x, y, w, ch_h, "Recentre view", false);
    cols(2, 1, x, w);
    ch.chip(HudButton::PinAll, 0, x, y, w, ch_h, in.pinned ? "Panels: placed" : "Panels: follow me", in.pinned);
    y += ch_h + gap;
    cols(2, 0, x, w);
    ch.chip(HudButton::PanelToggle, (int)PanelId::Scopes, x, y, w, ch_h, ch.s.enabled[(int)PanelId::Scopes] ? "Scopes: on" : "Scopes: off", ch.s.enabled[(int)PanelId::Scopes]);
    y += ch_h + gap + 8;
    // ---- diagnostics and hint, small
    if (in.hasVideo) {
        snprintf(b, sizeof b, "arrive %+.0f ms \xC2\xB7 decode %.0f ms \xC2\xB7 RTT %.0f ms \xC2\xB7 %llu shown \xC2\xB7 %llu dropped \xC2\xB7 %llu uneven%s",
                 in.arriveMs, in.decodeMs, in.rttMs, (unsigned long long)in.shown, (unsigned long long)in.dropped,
                 (unsigned long long)in.uneven, in.warning.empty() ? "" : ("  \xC2\xB7  " + in.warning).c_str());
        c.text(pad, y + 14, fitText(b, S20, inner), S20, in.warning.empty() ? Colour{1, 1, 1, 0.38f} : kSignal);
        y += 30;
    }
    const char* hint = in.hands ? "Look down to open \xC2\xB7 look up to close \xC2\xB7 grab a top bar to move a panel"
                                : "Trigger to press \xC2\xB7 look down to open \xC2\xB7 menu button places the panels";
    c.text(pad, y + 14, fitText(hint, S20, inner), S20, Colour{1, 1, 1, 0.30f});
#ifndef KALYX_BUILD
#define KALYX_BUILD "dev"
#endif
    {
        // every part a bug report needs to name: headset build, Mac package, bridge, Resolve
        std::string ver = std::string("Kalyx View ") + KALYX_BUILD;
        const EditModel* m = ch.s.edit.get();
        if (m && m->bridgeOk) {
            if (!m->installedVersion.empty()) ver += "  \xC2\xB7  Mac " + m->installedVersion;
            if (!m->bridgeVersion.empty() && m->bridgeVersion != m->installedVersion) ver += "  \xC2\xB7  bridge " + m->bridgeVersion;
            if (!m->resolveVersion.empty()) ver += "  \xC2\xB7  Resolve " + m->resolveVersion;
        }
        ver = fitText(ver, S16, inner);
        c.text(sp.w - pad - Canvas::measure(ver, S16), sp.h - 22, ver, S16, Colour{1, 1, 1, 0.30f});
    }
}

std::vector<ButtonRect> drawLook(Canvas& c, const UiState& s) {
    std::vector<ButtonRect> rects;
    Chrome ch{c, rects, s, PanelId::Look};
    const PanelSpec& sp = kSpecs[(int)PanelId::Look];
    c.reset(sp.w, sp.h);
    if (const EditModel* mm = s.edit.get()) {
        if (const EditGroup* gg = mm->current()) ch.subtitle = gg->clipName;
    }
    float y = ch.begin("LOOK");
    if (y < 0) return rects;
    const EditModel* m = s.edit.get();
    std::vector<std::string> tabs = m ? m->lookTabs() : std::vector<std::string>{"View"};
    int tab = std::max(0, std::min((int)tabs.size() - 1, s.lookTab));
    // tabs row
    const float pad = 40;
    float tx = pad + 4, th = 52;
    std::vector<float> tw;
    float segW = 8;
    for (int i = 0; i < (int)tabs.size(); i++) {
        float w = Chrome::chipWidth(tabs[i], -1, "", 52, 18);
        if (segW + w > sp.w - 2 * pad) break;
        tw.push_back(w);
        segW += w + (i ? 4 : 0);
    }
    c.wellV3(pad, y - 4, segW, th + 8, 18);
    for (size_t i = 0; i < tw.size(); i++) {
        ch.chip3(HudButton::Tab, (int)i, tx, y, tw[i], th, (int)i == tab ? ChipState::On : ChipState::Flat, tabs[i]);
        tx += tw[i] + 4;
    }
    y += th + 28;
    c.fillRoundRect(pad, y - 12, sp.w - 2 * pad, 1, 0.5f, Colour{1, 1, 1, 0.08f});
    if (tab == 0 || !m || !m->current()) {
        drawViewTab(ch, y);
        return rects;
    }
    sectionRows(ch, *m, *m->current(), tabs[tab], y, pad);
    return rects;
}

// One section's controls as a scrolling list of rows (bars, switches, colour wheels). Shared by the Look tabs
// and the Placement panel's TRIM view (+50), so a Slider/Toggle rect for a given row exists on one panel only.
void sectionRows(Chrome& ch, const EditModel& m, const EditGroup& g, const std::string& section, float y, float pad) {
    Canvas& c = ch.c;
    const UiState& s = ch.s;
    const PanelSpec& sp = kSpecs[(int)ch.panel];
    std::vector<int> rows;
    for (int i = 0; i < (int)g.params.size(); i++)
        if (!onPlacementPanel(g.params[i]) && g.params[i].section == section) rows.push_back(i);
    const float rowH = 118, colourH = 270, w = sp.w - 2 * pad;
    float listBottom = sp.h - 48;
    float total = 0;
    for (int r : rows) total += widgetFor(g.params[r]) == Widget::Colour ? colourH : rowH;
    if (total > listBottom - y) listBottom -= 66;   // paged: the page chips get their own strip under the last row
    int first = std::max(0, std::min(m.firstRow, (int)rows.size() - 1));
    float ry = y;
    bool more = false;
    for (int r = first; r < (int)rows.size(); r++) {
        const EditParam& p = g.params[rows[r]];
        Widget wd = widgetFor(p);
        float h = wd == Widget::Colour ? colourH : rowH;
        if (ry + h > listBottom) {
            more = true;
            break;
        }
        int pi = rows[r];
        bool hov = s.hoverPanel == (int)ch.panel && s.hoverArg == pi &&
                   (s.hover == HudButton::Slider || s.hover == HudButton::Toggle || s.hover == HudButton::Wheel ||
                    s.hover == HudButton::Bright);
        bool drag = m.dragRow == pi;
        if (hov || drag) c.fillRoundRect(pad - 12, ry, w + 24, h - 6, 14, Colour{1, 1, 1, drag ? 0.08f : 0.05f});
        // name without the section word ("Glow Size" -> "Size")
        EditParam q = p;
        if (q.name.size() > section.size() + 1 && q.name.compare(0, section.size(), section) == 0 && q.name[section.size()] == ' ') {
            q.name = q.name.substr(section.size() + 1);
            q.name[0] = (char)toupper((unsigned char)q.name[0]);
        }
        switch (wd) {
            case Widget::Switch: switchRow(ch, q, pi, pad, ry, w, rowH); break;
            case Widget::Colour: colourRow(ch, q, pi, pad, ry, w, colourH, drag); break;
            case Widget::Text:
            case Widget::Point:
                c.text(pad, ry + rowH / 2 + 8, fitText(q.name, S22, 300), S22, kText);
                c.text(pad + 300, ry + rowH / 2 + 8, fitText(valueText(q), S22, w - 300), S22, kDim);
                break;
            default: barRow(ch, q, pi, pad, ry, w, rowH, drag); break;
        }
        ry += h;
    }
    if (first > 0 || more) {
        float sx = sp.w - pad - 52, sy = listBottom + 8;
        if (first > 0) ch.chip3(HudButton::PageUp, 0, sx - 60, sy, 52, 52, ChipState::Rest, "", (int)Icon::RollUp);
        if (more) ch.chip3(HudButton::PageDown, 0, sx, sy, 52, 52, ChipState::Rest, "", (int)Icon::RollDown);

    }
}

std::vector<ButtonRect> drawDeck(Canvas& c, const UiState& s) {
    std::vector<ButtonRect> rects;
    Chrome ch{c, rects, s, PanelId::Deck};
    const PanelSpec& sp = kSpecs[(int)PanelId::Deck];
    c.reset(sp.w, sp.h);
    float y = ch.begin("TIMELINE");
    if (y < 0) return rects;
    const EditModel* m = s.edit.get();
    const float pad = 36;
    if (!m || !m->bridgeOk || !m->haveTimeline) {
        c.text(pad, y + 60, !m || !m->bridgeOk ? "The Resolve bridge isn't running on the Mac." : "Waiting for the timeline from Resolve\xE2\x80\xA6", M30, kDim);
        return rects;
    }
    // transport row
    float ty = y - 6, th = 58;
    ch.chip3(HudButton::ClipPrev, 0, pad, ty, 120, th, ChipState::Rest, "Clip", (int)Icon::Previous);
    ch.chip3(HudButton::StepBack, 0, pad + 130, ty, 92, th, ChipState::Rest, "1", (int)Icon::Minus);
    std::string tcs = m->tc;
    float tw = Canvas::measure(tcs, B34);
    c.text(sp.w / 2.0f - tw / 2, ty + 41, tcs, B34, kText);
    static const char* zoomLabels[] = {"All", "60 s", "10 s"};
    ch.chip3(HudButton::Zoom, 0, sp.w - pad - 100, ty, 100, th, m->zoom > 0 ? ChipState::On : ChipState::Rest, zoomLabels[std::max(0, std::min(2, m->zoom))]);
    ch.chip3(HudButton::ClipNext, 0, sp.w - pad - 110 - 120, ty, 120, th, ChipState::Rest, "Clip", (int)Icon::Next);
    ch.chip3(HudButton::StepFwd, 0, sp.w - pad - 110 - 130 - 92, ty, 92, th, ChipState::Rest, "1", (int)Icon::Plus);
    // lanes
    float lx = pad, lw = sp.w - 2 * pad;
    float laneTop = ty + th + 22;
    int tracks = std::min(m->tracks, 6);
    float laneH = tracks <= 3 ? 24 : 16, laneGap = 3;
    float lanesH = tracks * (laneH + laneGap);
    int64_t va, vb;
    m->viewRange(va, vb);
    double span = std::max<double>(1, (double)(vb - va));
    auto fx = [&](int64_t f) { return lx + (float)((double)(f - va) / span * lw); };
    auto clampX = [&](float x) { return std::max(lx, std::min(lx + lw, x)); };
    c.wellV3(lx, laneTop - 8, lw, lanesH + 16, 10);
    std::string selKey = m->current() ? m->current()->clip : "";
    for (auto& clip : m->clips) {
        if (clip.track > tracks) continue;
        if (clip.end <= va || clip.start >= vb) continue;
        float cy = laneTop + (tracks - clip.track) * (laneH + laneGap);
        float x0 = clampX(fx(clip.start)), x1 = clampX(fx(clip.end));
        Colour col = EditModel::clipColour(clip.colour, clip.fusion);
        bool under = m->frame >= clip.start && m->frame < clip.end;
        col.a = under ? 0.95f : 0.5f;
        c.fillRoundRect(x0 + 0.5f, cy, std::max(2.0f, x1 - x0 - 1), laneH, 3, col);
        if (clip.key == selKey && !selKey.empty())
            c.strokeRoundRect(x0 - 1.5f, cy - 2, std::max(2.0f, x1 - x0 + 3), laneH + 4, 4, 3, Colour{1, 1, 1, 1});
        else if (under)
            c.strokeRoundRect(x0 + 0.5f, cy, std::max(2.0f, x1 - x0 - 1), laneH, 3, 2, Colour{1, 1, 1, 0.5f});
    }
    for (auto& mk : m->markers) {
        if (mk.frame < va || mk.frame > vb) continue;
        float x = fx(mk.frame);
        c.fillTriangle(x - 6, laneTop - 8, x + 6, laneTop - 8, x, laneTop + 2, Colour{1, 1, 1, 0.8f});
    }
    if (m->frame >= 0 && m->frame >= va && m->frame <= vb) {
        float x = fx(m->frame);
        c.fillRoundRect(x - 1.5f, laneTop - 14, 3, lanesH + 28, 1.5f, kSignal);
        c.fillTriangle(x - 9, laneTop - 16, x + 9, laneTop - 16, x, laneTop - 5, kSignal);
    }
    rects.push_back({HudButton::Scrub, lx, laneTop - 16, lw, lanesH + 32, 0});
    // clip under the playhead and the timeline name, under the lanes
    float py = laneTop + lanesH + 36;
    std::string under;
    int stacked = 0;
    for (auto& clip : m->clips)
        if (m->frame >= clip.start && m->frame < clip.end && clip.fusion) {
            stacked++;
            if (under.empty() || clip.key == selKey) under = "V" + std::to_string(clip.track) + " \xC2\xB7 " + clip.name;
        }
    if (stacked > 1) under += "  (" + std::to_string(stacked) + " stacked)";
    if (py + 4 < sp.h) {
        c.text(pad, py, fitText(under.empty() ? "No clip under the playhead" : under, S20, lw - 420), S20, kDim);
        std::string tln = fitText(m->timelineName, S20, 400);
        c.text(sp.w - pad - Canvas::measure(tln, S20), py, tln, S20, kDim);
    }
    return rects;
}

}  // namespace

const PanelSpec& panelSpec(PanelId id) { return kSpecs[(int)id]; }

float Chrome::begin(const std::string& title) {
    const PanelSpec& sp = kSpecs[(int)panel];
    bool rolled = s.rolled[(int)panel];
    bool foc = focused();
    c.glassV3(sp.radius, foc && !rolled, rolled ? (float)kRolledH : -1.0f);
    const float barH = 64, cy = barH / 2;
    bool grab = hov(HudButton::Move) || prs(HudButton::Move);
    c.fillRoundRect(22, cy - 4, 56, 8, 4, Colour{1, 1, 1, grab ? 0.6f : foc ? 0.30f : 0.20f});
    c.text(92, cy + 7, title, B18, foc ? kText : kDim, 3);
    if (!subtitle.empty() && !rolled) {
        // what the panel is acting on (the picked thing and its clip), after the title, in the dim ink
        float tx = 92 + Canvas::measure(title, B18, 3) + 18;
        c.text(tx, cy + 7, fitText(subtitle, S20, sp.w - 12 - 48 - 16 - tx), S20, kDim);
    }
    // roll chevron at the right end of the bar
    chip3(HudButton::Roll, 0, sp.w - 12 - 48, cy - 24, 48, 48, ChipState::Flat, "", (int)(rolled ? Icon::Next : Icon::RollUp));
    if (!rolled) c.fillRoundRect(0, barH - 1, (float)sp.w, 1, 0, Colour{1, 1, 1, 0.08f});
    rects.push_back({HudButton::Move, 0, 0, (float)sp.w - 80, barH, 0});
    return rolled ? -1 : barH + 12;
}

void Chrome::button(HudButton id, int arg, float x, float y, float w, float h, const std::string& label, bool active,
                    const Font* font) {
    const Font& F = font ? *font : S22;
    bool hv = hov(id, arg), pr = prs(id, arg);
    ChipState st = active ? ChipState::On : pr ? ChipState::Pressed : hv ? ChipState::Hover : ChipState::Rest;
    c.chipBody(x, y, w, h, std::min(14.0f, h / 2), st);
    float tw = Canvas::measure(label, F);
    float base = y + h / 2 + (F.size >= 30 ? 11 : F.size >= 22 ? 8 : 7);
    c.text(x + (w - tw) / 2, base, label, F, Canvas::chipInk(st));
    rects.push_back({id, x, y, w, h, arg});
}

float Chrome::chipWidth(const std::string& label, int icon, const std::string& tag, float minW, float padX) {
    float w = 2 * padX;
    if (icon >= 0) w += 28 + (label.empty() && tag.empty() ? 0 : 10);
    if (!label.empty()) w += Canvas::measure(label, S22);
    if (!tag.empty()) w += 10 + Canvas::measure(tag, B18, 1);
    if (label.empty() && tag.empty() && icon >= 0) w = minW;
    return std::max(minW, w);
}

float Chrome::chip3(HudButton id, int arg, float x, float y, float w, float h, ChipState st, const std::string& label, int icon,
                    const std::string& tag, const std::string& tip) {
    bool hv = hov(id, arg), pr = prs(id, arg);
    ChipState eff = st;
    if (st != ChipState::Off && id != HudButton::None) {
        if (pr) eff = st == ChipState::On ? ChipState::On : ChipState::Pressed;
        else if (hv && st == ChipState::Rest) eff = ChipState::Hover;
    }
    float r = std::min(14.0f, h / 2);
    c.chipBody(x, y, w, h, r, eff);
    Colour ink = Canvas::chipInk(eff);
    if (eff == ChipState::Flat && (hv || pr) && st != ChipState::Off) ink = kText;   // a flat chip lights its label when pointed at
    float cx = x + w / 2, cy = y + h / 2;
    float tw = label.empty() ? 0 : Canvas::measure(label, S22);
    float tagW = tag.empty() ? 0 : Canvas::measure(tag, B18, 1);
    float content = (icon >= 0 ? 28 : 0) + (icon >= 0 && (tw > 0 || tagW > 0) ? 10 : 0) + tw + (tagW > 0 ? 10 + tagW : 0);
    float px = cx - content / 2;
    if (icon >= 0) {
        c.icon((Icon)icon, px + 14, cy, 28, ink);
        px += 28 + ((tw > 0 || tagW > 0) ? 10 : 0);
    }
    if (tw > 0) px = c.text(px, cy + 8, label, S22, ink);
    if (tagW > 0) c.text(px + 10, cy + 7, tag, B18, Colour{ink.r, ink.g, ink.b, ink.a * 0.65f}, 1);
    if (!tip.empty() && (hv || pr) && st != ChipState::Off) {
        // the word for an icon-only chip: a small dark pill above it (below, when the chip sits at the top edge)
        float tw2 = Canvas::measure(tip, S16) + 20, th = 30;
        float tx = std::max(6.0f, std::min((float)c.width() - tw2 - 6, cx - tw2 / 2));
        float ty = y + h + 8 + th <= (float)c.height() - 4 ? y + h + 8 : y - th - 8;
        c.shadow(tx, ty, tw2, th, 8, 4, 6, 0.5f);
        c.fillRoundRect(tx, ty, tw2, th, 8, hex(0x15181d, 0.96f));
        c.strokeRoundRect(tx, ty, tw2, th, 8, 1, Colour{1, 1, 1, 0.12f});
        c.text(tx + 10, ty + 21, tip, S16, kText);
    }
    if (id != HudButton::None) rects.push_back({id, x, y, w, h, arg});
    return w;
}

// ---- Scopes: waveform / parade / vectorscope / histograms, from the raster the Mac measured (protocol.h layout)
namespace {

// Draws one tile of the scope raster into a box, tinted; the box is a dark well first. The raster is 128–256
// texels across and the box is two to four times that, so it is resampled bilinearly through a small tent filter
// (four taps, ±0.4 texel) instead of nearest-neighbour: smooth traces, no staircase, no dither (Nelson, 2 Nov).
void scopeTile(Canvas& c, const std::vector<uint8_t>* R, int tx, int ty, int tw, int th, float x, float y, float w, float h,
               Colour tint) {
    c.fillRoundRect(x, y, w, h, 6, Colour{0, 0, 0, 0.55f});
    if (!R) return;
    const int W = (int)vrlproto::kScopesW;
    auto texel = [&](int u, int v) {
        u = std::max(tx, std::min(tx + tw - 1, u));
        v = std::max(ty, std::min(ty + th - 1, v));
        return (*R)[(size_t)v * W + u] / 255.0f;
    };
    auto bilinear = [&](float fu, float fv) {
        int u0 = (int)std::floor(fu), v0 = (int)std::floor(fv);
        float du = fu - u0, dv = fv - v0;
        return (texel(u0, v0) * (1 - du) + texel(u0 + 1, v0) * du) * (1 - dv) + (texel(u0, v0 + 1) * (1 - du) + texel(u0 + 1, v0 + 1) * du) * dv;
    };
    c.shadeRect((int)x, (int)y, (int)(x + w), (int)(y + h), [&](float px, float py) {
        float fu = tx + (px - x) / w * tw - 0.5f, fv = ty + (py - y) / h * th - 0.5f;
        float a = 0.25f * (bilinear(fu - 0.4f, fv - 0.4f) + bilinear(fu + 0.4f, fv - 0.4f) + bilinear(fu - 0.4f, fv + 0.4f) + bilinear(fu + 0.4f, fv + 0.4f));
        a = std::pow(a, 0.85f);   // lift the faint single-pixel traces a little so they read as a line, not grit
        return Colour{tint.r, tint.g, tint.b, a * tint.a};
    });
}

// Level lines for a 0..1023 scale box: 0% (64) and 100% (940) solid, 25/50/75 faint.
void levelLines(Canvas& c, float x, float y, float w, float h, bool labels) {
    auto rowFor = [&](double v) { return y + h * (1.0 - v / 1024.0); };
    struct L { double v; float a; const char* t; };
    static const L ls[] = {{940, 0.8f, "100"}, {721, 0.3f, "75"}, {502, 0.3f, "50"}, {283, 0.3f, "25"}, {64, 0.8f, "0"}};
    for (auto& l : ls) {
        float yy = rowFor(l.v);
        c.fillRoundRect(x, yy - 0.5f, w, 1, 0, Colour{kIce.r, kIce.g, kIce.b, l.a});
        if (labels) c.text(x + w + 8, yy + 6, l.t, S20, Colour{kIce.r, kIce.g, kIce.b, 0.8f});
    }
}

void vectorGraticule(Canvas& c, float x, float y, float sz) {
    float cx = x + sz / 2, cy = y + sz / 2, r = sz / 2;
    c.strokeCircle(cx, cy, r - 1, 1.5f, Colour{kIce.r, kIce.g, kIce.b, 0.5f});
    c.strokeCircle(cx, cy, r * 0.5f, 1, Colour{kIce.r, kIce.g, kIce.b, 0.25f});
    c.fillRoundRect(cx - 0.5f, y, 1, sz, 0, Colour{kIce.r, kIce.g, kIce.b, 0.25f});
    c.fillRoundRect(x, cy - 0.5f, sz, 1, 0, Colour{kIce.r, kIce.g, kIce.b, 0.25f});
    // 75% colour-bar targets, BT.709 (Cb, Cr) offsets on the 0..1023 scale; the tile maps 0..1023 across sz
    struct T { const char* n; double cb, cr; };
    static const T ts[] = {{"R", -0.0735, 0.3296}, {"Mg", 0.1915, 0.2650}, {"B", 0.3296, -0.0735},
                           {"Cy", 0.0735, -0.3296}, {"G", -0.1915, -0.2650}, {"Yl", -0.3296, 0.0735}};
    for (auto& t : ts) {
        // table holds the 75%-bar chroma offsets (fraction of full scale); 896 codes = full chroma swing on 0..1023
        float px = cx + (float)(t.cb * 896 / 1024.0) * sz, py = cy - (float)(t.cr * 896 / 1024.0) * sz;
        c.strokeRoundRect(px - 6, py - 6, 12, 12, 2, 1.5f, Colour{kIce.r, kIce.g, kIce.b, 0.8f});
        c.text(px + 9, py + 5, t.n, S20, Colour{kIce.r, kIce.g, kIce.b, 0.8f});
    }
    // skin-tone line (I axis, ~123 deg from +Cb)
    float a = 123.0f * 3.14159265f / 180.0f;
    c.fillTriangle(cx, cy, cx + std::cos(a) * r * 0.95f, cy - std::sin(a) * r * 0.95f, cx + std::cos(a) * r * 0.95f + 1, cy - std::sin(a) * r * 0.95f + 1,
                   Colour{kIce.r, kIce.g, kIce.b, 0.5f});
}

}  // namespace

std::vector<ButtonRect> drawScopes(Canvas& c, const UiState& s) {
    std::vector<ButtonRect> rects;
    Chrome ch{c, rects, s, PanelId::Scopes};
    const PanelSpec& sp = kSpecs[(int)PanelId::Scopes];
    c.reset(sp.w, sp.h);
    float y = ch.begin("SCOPES");
    if (y < 0) return rects;
    const std::vector<uint8_t>* R = s.info.scopes ? s.info.scopes.get() : nullptr;
    // view chips
    float x = 40;
    const char* views[3] = {"Waveform", "Parade", "Histogram"};
    const float vw[3] = {150, 120, 150};
    for (int i = 0; i < 3; i++) {
        ch.chip(HudButton::ScopesView, i, x, y + 8, vw[i], 44, views[i], s.info.scopesView == i);
        x += vw[i] + 8;
    }
    {
        std::string st;
        if (!s.info.connected) st = "Not connected";
        else if (!R) st = "Waiting for the first measurement from the Mac\xE2\x80\xA6";
        else {
            st = s.info.scopesTimecode != 0xFFFFFFFF ? "Measured at " + timecodeString(s.info.scopesTimecode) : "Measured on the Mac";
            if (s.info.scopesAgeMs > 2500) st += "  \xC2\xB7  held (" + std::to_string((int)(s.info.scopesAgeMs / 1000)) + " s ago)";
            st += "  \xC2\xB7  uncompressed, before HEVC";
        }
        c.text(x + 16, y + 38, fitText(st, S20, sp.w - 60 - x), S20, kDim);
    }
    y += 70;
    const float bodyH = sp.h - y - 56;
    const Colour luma{0.92f, 0.93f, 0.95f, 1}, red{1.0f, 0.42f, 0.38f, 1}, green{0.45f, 0.95f, 0.5f, 1}, blue{0.5f, 0.68f, 1.0f, 1};
    if (s.info.scopesView == 0) {
        // waveform (luma) left, vectorscope right
        float wf = sp.w - 80 - bodyH - 70;
        scopeTile(c, R, 0, 0, 256, 128, 40, y, wf, bodyH, luma);
        levelLines(c, 40, y, wf, bodyH, true);
        c.text(44, y + 24, "LUMA", S20, kDim, 3);
        float vx = sp.w - 40 - bodyH;
        scopeTile(c, R, 640, 0, 128, 128, vx, y, bodyH, bodyH, Colour{0.95f, 0.95f, 1.0f, 1});
        vectorGraticule(c, vx, y, bodyH);
        c.text(vx + 8, y + 24, "VECTOR", S20, kDim, 3);
    } else if (s.info.scopesView == 1) {
        float tw = (sp.w - 80 - 2 * 16 - 50) / 3;
        const int ox[3] = {256, 384, 512};
        const Colour tint[3] = {red, green, blue};
        const char* name[3] = {"R", "G", "B"};
        for (int i = 0; i < 3; i++) {
            float tx = 40 + i * (tw + 16);
            scopeTile(c, R, ox[i], 0, 128, 128, tx, y, tw, bodyH, tint[i]);
            levelLines(c, tx, y, tw, bodyH, i == 2);
            c.text(tx + 8, y + 24, name[i], S20, kDim, 3);
        }
    } else {
        float hh = (bodyH - 16) / 2;
        scopeTile(c, R, 0, 128, 256, 64, 40, y, sp.w - 80, hh, luma);
        c.text(sp.w - 40 - Canvas::measure("LUMA", S20, 3) - 8, y + 24, "LUMA", S20, kDim, 3);
        c.text(44, y + hh - 8, "0", S20, Colour{kIce.r, kIce.g, kIce.b, 0.8f});
        c.text(sp.w - 40 - Canvas::measure("100", S20), y + hh - 8, "100", S20, Colour{kIce.r, kIce.g, kIce.b, 0.8f});
        float tw = (sp.w - 80 - 32) / 3;
        const int ox[3] = {256, 384, 512};
        const Colour tint[3] = {red, green, blue};
        for (int i = 0; i < 3; i++) scopeTile(c, R, ox[i], 128, 128, 64, 40 + i * (tw + 16), y + hh + 16, tw, hh, tint[i]);
    }
    c.text(40, sp.h - 22, fitText("Video range: 0 = 64, 100 = 940 on the 10-bit scale \xC2\xB7 measured on the Mac, not from the headset's decoded picture", S20, sp.w - 80), S20, kDim);
    return rects;
}

std::vector<ButtonRect> drawPanelById(Canvas& c, PanelId id, const UiState& s) {
    switch (id) {
        case PanelId::Instance: return drawInstance(c, s);
        case PanelId::Placement: return drawPlacement(c, s);
        case PanelId::Look: return drawLook(c, s);
        case PanelId::Deck: return drawDeck(c, s);
        case PanelId::Scopes: return drawScopes(c, s);
        default: return {};
    }
}

}  // namespace vrq
