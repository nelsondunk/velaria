#include "edit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace vrq {

using font_ui::Font;
using namespace pal;

// ------------------------------------------------------------------------------------------------ model

bool EditModel::apply(const Json& msg) {
    const std::string& type = msg.get("type").stringOr("");
    if (type == "timeline") {
        haveTimeline = true;
        timelineName = msg.get("name").stringOr("");
        start = (int64_t)msg.get("start").numberOr(0);
        end = (int64_t)msg.get("end").numberOr(0);
        fps = msg.get("fps").numberOr(30);
        tracks = std::max(1, msg.get("tracks").intOr(1));
        {
            const Json& f = msg.get("format");
            fmtWidth = f.get("width").intOr(0);
            fmtHeight = f.get("height").intOr(0);
            fmtOutWidth = f.get("outWidth").intOr(0);
            fmtOutHeight = f.get("outHeight").intOr(0);
            warnings.clear();
            for (auto& w : msg.get("warnings").arr) warnings.push_back(w.stringOr(""));
        }
        clips.clear();
        for (auto& c : msg.get("clips").arr) {
            EditClip e;
            e.key = c.get("key").stringOr("");
            e.name = c.get("name").stringOr("");
            e.colour = c.get("color").stringOr("");
            e.track = c.get("track").intOr(1);
            e.start = (int64_t)c.get("start").numberOr(0);
            e.end = (int64_t)c.get("end").numberOr(0);
            e.fusion = c.get("fusion").boolOr(false);
            clips.push_back(e);
        }
        markers.clear();
        for (auto& m : msg.get("markers").arr) {
            EditMarker e;
            e.frame = (int64_t)m.get("frame").numberOr(0);
            e.name = m.get("name").stringOr("");
            e.colour = m.get("color").stringOr("");
            markers.push_back(e);
        }
        return true;
    }
    if (type == "state") {
        frame = (int64_t)msg.get("frame").numberOr(-1);
        tc = msg.get("tc").stringOr(tc);
        return true;
    }
    if (type == "params") {
        std::string keepTool = group < (int)groups.size() ? groups[group].tool : "";
        groups.clear();
        for (auto& g : msg.get("groups").arr) {
            EditGroup e;
            e.clip = g.get("clip").stringOr("");
            e.clipName = g.get("clipName").stringOr("");
            e.tool = g.get("tool").stringOr("");
            e.track = g.get("track").intOr(0);
            e.source = g.get("source").stringOr("");
            e.kind = g.get("kind").stringOr("card");
            for (auto& p : g.get("params").arr) {
                EditParam q;
                q.id = p.get("id").stringOr("");
                q.name = p.get("name").stringOr(q.id);
                q.kind = p.get("kind").stringOr("number");
                q.min = p.get("min").numberOr(0);
                q.max = p.get("max").numberOr(1);
                q.animated = p.get("animated").boolOr(false);
                q.section = p.get("section").stringOr("Settings");
                q.invert = p.get("invert").boolOr(false);
                for (auto& idj : p.get("ids").arr) q.ids.push_back(idj.stringOr(""));
                const Json& v = p.get("value");
                if (q.kind == "colour" && v.type == Json::Array && v.arr.size() >= 3) {
                    q.cr = (float)v.arr[0].numberOr(1);
                    q.cg = (float)v.arr[1].numberOr(1);
                    q.cb = (float)v.arr[2].numberOr(1);
                } else if (q.kind == "point" && v.type == Json::Array && v.arr.size() >= 2) {
                    q.px = v.arr[0].numberOr(0.5);
                    q.py = v.arr[1].numberOr(0.5);
                } else if (q.kind == "text") {
                    q.text = v.stringOr("");
                } else {
                    q.value = v.numberOr(0);
                }
                e.params.push_back(q);
            }
            groups.push_back(e);
        }
        // keep looking at the same macro when the list is refreshed
        group = 0;
        for (size_t i = 0; i < groups.size(); i++)
            if (groups[i].tool == keepTool) group = (int)i;
        settle();
        firstRow = 0;
        return true;
    }
    if (type == "notice" || type == "error") {
        notice = msg.get("msg").stringOr("");
        return true;
    }
    if (type == "hello") {
        bridgeVersion = msg.get("bridge").stringOr("");
        installedVersion = msg.get("installed").stringOr("");
        resolveVersion = msg.get("resolve").stringOr("");
        projectName = msg.get("project").stringOr("");
        return true;
    }
    return false;
}

void EditModel::viewRange(int64_t& a, int64_t& b) const {
    a = start;
    b = end;
    if (zoom <= 0 || frame < 0) return;
    int64_t half = (int64_t)((zoom == 1 ? 30.0 : 5.0) * fps);
    a = std::max(start, frame - half);
    b = a + 2 * half;
    if (b > end) {
        b = end;
        a = std::max(start, b - 2 * half);
    }
}

bool EditModel::logScale(const EditParam& p) {
    double lo = std::min(p.min, p.max), hi = std::max(p.min, p.max);
    if (lo > 0) return hi / lo > 10;
    if (hi < 0) return lo / hi > 10;
    return false;
}

float EditModel::toT(const EditParam& p, double v) {
    if (p.invert) {
        EditParam q = p;
        q.invert = false;
        return 1.0f - toT(q, v);
    }
    if (p.max == p.min) return 0;
    if (logScale(p)) {
        double a = std::log(std::fabs(p.min)), b = std::log(std::fabs(p.max));
        double x = std::log(std::max(1e-9, std::fabs(v)));
        return (float)std::min(1.0, std::max(0.0, (x - a) / (b - a)));
    }
    return (float)std::min(1.0, std::max(0.0, (v - p.min) / (p.max - p.min)));
}

double EditModel::fromT(const EditParam& p, float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    if (p.invert) t = 1.0f - t;
    if (logScale(p)) {
        double a = std::log(std::fabs(p.min)), b = std::log(std::fabs(p.max));
        double v = std::exp(a + (b - a) * t);
        return p.min < 0 ? -v : v;
    }
    return p.min + (p.max - p.min) * t;
}

void rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    v = mx;
    s = mx > 0 ? d / mx : 0;
    if (d <= 1e-6f) h = 0;
    else if (mx == r) h = std::fmod((g - b) / d + 6.0f, 6.0f) / 6.0f;
    else if (mx == g) h = ((b - r) / d + 2.0f) / 6.0f;
    else h = ((r - g) / d + 4.0f) / 6.0f;
}

void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    float hh = std::fmod(std::max(0.0f, h), 1.0f) * 6.0f;
    int i = (int)hh;
    float f = hh - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

Colour EditModel::clipColour(const std::string& n, bool fusion) {
    struct C {
        const char* name;
        float r, g, b;
    };
    static const C table[] = {{"Orange", 0.93f, 0.45f, 0.14f}, {"Apricot", 0.95f, 0.68f, 0.40f}, {"Yellow", 0.93f, 0.80f, 0.25f},
                              {"Lime", 0.68f, 0.85f, 0.30f},   {"Olive", 0.55f, 0.62f, 0.25f},   {"Green", 0.30f, 0.72f, 0.40f},
                              {"Teal", 0.25f, 0.68f, 0.68f},   {"Navy", 0.25f, 0.35f, 0.65f},    {"Blue", 0.32f, 0.55f, 0.90f},
                              {"Purple", 0.55f, 0.38f, 0.80f}, {"Violet", 0.70f, 0.45f, 0.85f},  {"Pink", 0.92f, 0.45f, 0.70f},
                              {"Tan", 0.75f, 0.62f, 0.45f},    {"Beige", 0.80f, 0.75f, 0.62f},   {"Brown", 0.55f, 0.40f, 0.28f},
                              {"Chocolate", 0.42f, 0.28f, 0.20f}};
    for (auto& t : table)
        if (n == t.name) return Colour{t.r, t.g, t.b, 0.85f};
    return fusion ? Colour{0.45f, 0.55f, 0.70f, 0.85f} : Colour{0.40f, 0.44f, 0.50f, 0.85f};
}

// ------------------------------------------------------------------------------------------------ text

static std::string shortName(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    size_t cut = n - 1;
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;   // don't split a UTF-8 sequence
    return s.substr(0, cut) + "\xE2\x80\xA6";
}

// Trims `s` (dropping whole UTF-8 characters) until it fits in maxW, appending an ellipsis.
std::string fitText(std::string s, const font_ui::Font& f, float maxW) {
    if (Canvas::measure(s, f) <= maxW) return s;
    size_t cut = s.size();
    for (int guard = 0; guard < 400 && cut > 1; guard++) {
        cut--;
        while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
        std::string t = s.substr(0, cut) + "\xE2\x80\xA6";
        if (Canvas::measure(t, f) <= maxW) return t;
    }
    return "\xE2\x80\xA6";
}

std::string valueText(const EditParam& p) {
    char b[64];
    if (p.kind == "bool") return p.value != 0 ? "On" : "Off";
    if (p.kind == "text") return shortName(p.text, 22);
    if (p.kind == "point") {
        snprintf(b, sizeof b, "%.3f, %.3f", p.px, p.py);
        return b;
    }
    double span = std::fabs(p.max - p.min);
    if (span > 50) snprintf(b, sizeof b, "%.1f", p.value);
    else if (span > 2) snprintf(b, sizeof b, "%.2f", p.value);
    else snprintf(b, sizeof b, "%.3f", p.value);
    return b;
}


// ------------------------------------------------------------------------------------------------ widgets

static std::string lower(std::string s) {
    for (auto& ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
}

Widget widgetFor(const EditParam& p) {
    if (p.kind == "bool") return Widget::Switch;
    if (p.kind == "colour") return Widget::Colour;
    if (p.kind == "text") return Widget::Text;
    if (p.kind == "point") return Widget::Point;
    std::string id = lower(p.id), nm = lower(p.name);
    auto starts = [&](const char* k) { return id.rfind(k, 0) == 0 || nm.rfind(k, 0) == 0; };
    // Dials first: the card's own rotation controls (@tilt / @turn / @spin, "Pitch (card)", "Yaw (card)") would
    // otherwise match the ring/arc prefixes below and vanish from the Placement panel (v35 fix).
    if (id == "roll" || starts("roll") || (id[0] == '@' && (id == "@spin" || id == "@turn" || id == "@tilt")) ||
        starts("objectspin") || starts("objectturn") || starts("objecttilt") || starts("object spin") ||
        starts("object turn") || starts("object tilt") || starts("object lean") || nm.find("(card)") != std::string::npos)
        return Widget::Dial;
    if (id == "yaw" || id == "pan" || starts("yaw") || starts("pan")) return Widget::Ring;
    if (id == "pitch" || id == "tilt" || starts("pitch") || (starts("tilt") && p.section == "Placement")) return Widget::Arc;
    if (id == "@size" || id == "size" || id == "distance" || starts("distance")) return Widget::Throttle;
    return Widget::Bar;
}

bool onPlacementPanel(const EditParam& p) { return p.section == "Placement" || p.section == "Object"; }

std::vector<std::string> EditModel::lookTabs() const {
    std::vector<std::string> tabs{"View"};
    const EditGroup* g = current();
    if (!g) return tabs;
    for (auto& p : g->params) {
        if (onPlacementPanel(p)) continue;
        if (std::find(tabs.begin(), tabs.end(), p.section) == tabs.end()) tabs.push_back(p.section);
    }
    return tabs;
}

double EditModel::nudgeStep(const EditParam& p) {
    double span = std::fabs(p.max - p.min);
    if (span > 50) return 1.0;
    if (span > 5) return 0.1;
    if (span > 0.5) return 0.01;
    return 0.001;
}

std::string editInstanceKey(const EditModel* m) {
    if (!m) return "-";
    std::string k = std::to_string(m->group) + "/" + std::to_string(m->groups.size()) + "|" + m->notice + "|" +
                    std::to_string(m->clipboard.size()) + "|" + (m->bridgeOk ? "1" : "0") + "|" + std::to_string(m->layer) +
                    std::to_string(m->countInLayer()) + "|";
    const EditGroup* g = m->current();
    if (g) k += g->tool + "@" + std::to_string(g->track) + g->clipName + "|" + g->source + "|" + g->kind + (m->isLocked(*g) ? "|L" : "");
    return k;
}

std::string editParamsKey(const EditModel* m, bool placement) {
    if (!m) return "-";
    std::string k = std::to_string(m->group) + "|" + std::to_string(m->firstRow) + "|" + std::to_string(m->dragRow) + "|" +
                    (m->bridgeOk ? "1" : "0") + (m->haveTimeline ? "1" : "0") + "|";
    const EditGroup* g = m->current();
    if (!g) return k;
    k += g->tool + (m->isLocked(*g) ? "#L" : "") + "#" + std::to_string(m->layer) + ":";
    for (auto& p : g->params) {
        if (onPlacementPanel(p) != placement) continue;
        char b[64];
        snprintf(b, sizeof b, "%.5g,%.4g,%.4g,%.3g,%.3g,%.3g,%d,%.4g,%.4g;", p.value, p.px, p.py, p.cr, p.cg, p.cb,
                 (int)p.animated, p.min, p.max);
        k += p.id + b + p.text;
    }
    return k;
}

std::string editTimelineKey(const EditModel* m) {
    if (!m) return "-";
    std::string k = m->timelineName + "|" + std::to_string(m->frame) + "|" + std::to_string(m->clips.size()) + "|" +
                    std::to_string(m->markers.size()) + "|" + std::to_string(m->zoom) + "|" + m->tc + "|" +
                    (m->bridgeOk ? "1" : "0") + (m->haveTimeline ? "1" : "0") + "|";
    const EditGroup* g = m->current();
    if (g) k += g->clip;
    return k;
}

}  // namespace vrq
