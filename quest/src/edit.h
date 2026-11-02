// Edit page: the Resolve timeline under the playhead and the Fusion macro controls of the clips there, fed by the
// bridge on the Mac. Values changed here go back to Resolve, which re-renders to the headset.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "hud.h"
#include "json.h"

namespace vrq {

struct EditClip {
    std::string key, name, colour;
    int track = 1;
    int64_t start = 0, end = 0;
    bool fusion = false;
};

struct EditMarker {
    int64_t frame = 0;
    std::string name, colour;
};

struct EditParam {
    std::string id, name, kind, section;   // kind: number | bool | text | point | colour
    std::vector<std::string> ids;          // colour: the R, G, B input ids
    double min = 0, max = 1, value = 0;
    double px = 0, py = 0;                 // point kind
    float cr = 1, cg = 1, cb = 1;          // colour kind (sRGB 0..1 as Fusion stores it)
    std::string text;
    bool animated = false;
    bool invert = false;   // slider runs right-to-left (pan: dragging right should move the object right)
};

struct EditGroup {
    std::string clip, clipName, tool, source;   // source: comp name / footage files the card is built from
    std::string kind = "card";                  // card (Velaria macro) | patch (LatLong Patcher) | orient (PanoMap) | trim (Velaria Trim)
    int track = 0;
    std::vector<EditParam> params;
    bool isCard() const { return kind == "card"; }
    // which strip layer it lives on: 0 OVERLAYS (titles and footage windows), 1 PATCHES, 2 PICTURE (whole sphere)
    int layer() const { return kind == "patch" ? 1 : (kind == "orient" || kind == "trim") ? 2 : 0; }
    // what the strip calls it: FOOTAGE / TITLE / OVERLAY / PATCH / PICTURE / TRIM, from the macro's name
    std::string tag() const {
        if (kind == "patch") return "PATCH";
        if (kind == "orient") return "PICTURE";
        if (kind == "trim") return "TRIM";
        if (tool.find("FactWindow") != std::string::npos || tool.find("CanvasControl") != std::string::npos || tool.find("PIP") != std::string::npos) return "FOOTAGE";
        if (tool.find("FactCard") != std::string::npos || tool.find("Title") != std::string::npos || tool.find("Text") != std::string::npos) return "TITLE";
        return "OVERLAY";
    }
    std::string key() const { return clip + "|" + tool; }
};

// Which control a parameter gets, from its id/name (Velaria control vocabulary).
enum class Widget { Bar, Switch, Colour, Text, Point, Ring, Arc, Dial, Throttle };
Widget widgetFor(const EditParam& p);
// Placement panel takes Placement + Object sections; everything else is a Look tab.
bool onPlacementPanel(const EditParam& p);

struct EditModel {
    bool bridgeOk = false;
    std::string bridgeVersion, installedVersion, resolveVersion, projectName;   // from the bridge's hello reply
    bool haveTimeline = false;
    std::string timelineName;
    // what Resolve plays out for this timeline (from the bridge) and the setup warnings it found
    int fmtWidth = 0, fmtHeight = 0, fmtOutWidth = 0, fmtOutHeight = 0;
    std::vector<std::string> warnings;
    int64_t start = 0, end = 0;
    double fps = 30;
    int tracks = 1;
    std::vector<EditClip> clips;
    std::vector<EditMarker> markers;
    int64_t frame = -1;
    std::string tc = "--:--:--:--";
    std::vector<EditGroup> groups;
    int group = 0;                // which macro's controls are shown
    // Layers: CARDS (the Velaria macros, pickable in the scene) or PATCHES (LatLong Patcher windows and PanoMap
    // re-orientation, which are normally set up in Resolve). Only the current layer's groups are shown, so it is
    // always obvious whether a drag moves a card or the plate's patch (Nelson, 30 Oct).
    int layer = 0;                  // 0 OVERLAYS, 1 PATCHES, 2 PICTURE
    std::set<std::string> locked;   // group keys whose controls are read-only in the headset
    bool inLayer(const EditGroup& g) const { return g.layer() == layer; }
    int countOnLayer(int l) const { int n = 0; for (auto& g : groups) if (g.layer() == l) n++; return n; }
    int firstInLayer() const { for (size_t i = 0; i < groups.size(); i++) if (inLayer(groups[i])) return (int)i; return -1; }
    int countInLayer() const { int n = 0; for (auto& g : groups) if (inLayer(g)) n++; return n; }
    int indexInLayer() const { int n = 0; for (int i = 0; i < group && i < (int)groups.size(); i++) if (inLayer(groups[i])) n++; return n; }
    int neighbourInLayer(int dir) const {
        int n = (int)groups.size();
        if (n == 0) return -1;
        for (int step = 1; step <= n; step++) { int i = ((group + dir * step) % n + n) % n; if (inLayer(groups[i])) return i; }
        return -1;
    }
    bool isLocked(const EditGroup& g) const { return locked.count(g.key()) > 0; }
    bool currentLocked() const { const EditGroup* g = current(); return g && isLocked(*g); }
    void settle() { if (group < 0 || group >= (int)groups.size() || !inLayer(groups[group])) { int f = firstInLayer(); group = f < 0 ? 0 : f; } }
    int firstRow = 0;             // scrolling within a Look tab
    std::string notice;
    // the control being dragged (-1 none); the renderer highlights it
    int dragRow = -1;
    int zoom = 0;                 // timeline view: 0 whole, 1 = 60 s, 2 = 10 s around the playhead
    // Copy/Paste between macro instances: input id -> value, with the section it came from
    struct ClipEntry { std::string section; double value; };
    std::map<std::string, ClipEntry> clipboard;
    std::string clipboardFrom;    // "Skylight_FactWindow on V7"

    // Applies a bridge message; returns true if anything shown changed.
    bool apply(const Json& msg);
    const EditGroup* current() const { return group >= 0 && group < (int)groups.size() && inLayer(groups[group]) ? &groups[group] : nullptr; }
    // Look tabs: "View" then the current macro's sections that are not on the Placement panel.
    std::vector<std::string> lookTabs() const;
    // Frames shown in the timeline strip at the current zoom.
    void viewRange(int64_t& a, int64_t& b) const;
    // Slider position <-> value; logarithmic when the range spans more than 10x on one side of zero.
    static bool logScale(const EditParam& p);
    static float toT(const EditParam& p, double v);
    static double fromT(const EditParam& p, float t);
    // one nudge step for a bar
    static double nudgeStep(const EditParam& p);
    // Resolve clip colours as drawn in the timeline
    static Colour clipColour(const std::string& name, bool fusion);
};

// sRGB <-> HSV helpers for the colour wheel
void rgbToHsv(float r, float g, float b, float& h, float& s, float& v);
void hsvToRgb(float h, float s, float v, float& r, float& g, float& b);

// Cache keys for the renderer (what each panel depends on)
std::string editInstanceKey(const EditModel* m);
std::string editParamsKey(const EditModel* m, bool placement);
std::string editTimelineKey(const EditModel* m);

std::string valueText(const EditParam& p);
std::string fitText(std::string s, const font_ui::Font& f, float maxW);

}  // namespace vrq
