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
    bool enabled = true;     // Resolve's Enable Clip (D): a disabled clip renders nothing and is "hidden" in the headset
};

struct EditMarker {
    int64_t frame = 0;
    std::string name, colour, note;
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
    std::string label;                          // what the bridge calls it (1.0.14+): "Title · Editorial", "Footage window"
    std::vector<std::string> children;          // macros folded into this card (a title plugged into Canvas Control)
    std::string kind = "card";                  // card (Velaria macro) | patch (LatLong Patcher) | orient (PanoMap) | trim (Velaria Trim) | stereo (Velaria Stereo)
    int track = 0;
    std::vector<EditParam> params;
    bool isCard() const { return kind == "card"; }
    // which strip layer it lives on: 0 OVERLAYS (titles and footage windows), 1 PATCHES, 2 PICTURE (whole sphere)
    // "bars only" kinds have nothing to place: their controls take the Placement panel as a list of bars
    bool barsOnly() const { return kind == "trim" || kind == "stereo"; }
    int layer() const { return kind == "patch" ? 1 : (kind == "orient" || barsOnly()) ? 2 : 0; }
    // which review stage owns it (+94): 0 Edit (PanoMap / Orient, patches, stereo), 1 Graphics (cards), 2 Colour (trim)
    int stage() const { return kind == "trim" ? 2 : isCard() ? 1 : 0; }
    // what the strip calls it: FOOTAGE / TITLE / OVERLAY / PATCH / PICTURE / TRIM, from the macro's name
    std::string tag() const {
        if (kind == "patch") return "PATCH";
        if (kind == "orient") return "FOOTAGE";
        if (kind == "trim") return "TRIM";
        if (kind == "stereo") return "STEREO";
        if (!children.empty() || label.rfind("Title", 0) == 0) return "TITLE";
        if (label == "Graphic") return "GRAPHIC";
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
    // Stages (+94, Nelson 21 Nov: "like how Resolve has the different pages for different stages"): Edit, Graphics,
    // Colour, Sound. A stage decides which groups the strip lists and what a pinch in the picture can grab (cards,
    // on Graphics only), and sets the panels' defaults when you switch to it. Sound has no groups of its own.
    enum Stage { Edit = 0, Graphics = 1, Grade = 2, Sound = 3, StageCount = 4 };   // Grade is the Colour stage (Colour is a type here)
    int stage = Edit;
    std::set<std::string> locked;   // group keys whose controls are read-only in the headset
    bool inStage(const EditGroup& g) const { return g.stage() == stage; }
    int countOnStage(int st) const { int n = 0; for (auto& g : groups) if (g.stage() == st) n++; return n; }
    int firstInStage() const { for (size_t i = 0; i < groups.size(); i++) if (inStage(groups[i])) return (int)i; return -1; }
    int countInStage() const { int n = 0; for (auto& g : groups) if (inStage(g)) n++; return n; }
    int indexInStage() const { int n = 0; for (int i = 0; i < group && i < (int)groups.size(); i++) if (inStage(groups[i])) n++; return n; }
    int neighbourInStage(int dir) const {
        int n = (int)groups.size();
        if (n == 0) return -1;
        for (int step = 1; step <= n; step++) { int i = ((group + dir * step) % n + n) % n; if (inStage(groups[i])) return i; }
        return -1;
    }
    bool isLocked(const EditGroup& g) const { return locked.count(g.key()) > 0; }
    // hidden = the group's clip is disabled in Resolve (the headset's Hide chip, or D on the Edit page)
    bool isHidden(const EditGroup& g) const { for (auto& c : clips) if (c.key == g.clip) return !c.enabled; return false; }
    int hiddenInStage() const { int n = 0; for (auto& g : groups) if (inStage(g) && isHidden(g)) n++; return n; }
    const EditMarker* markerAt(int64_t f) const { for (auto& m : markers) if (m.frame == f) return &m; return nullptr; }
    bool currentLocked() const { const EditGroup* g = current(); return g && isLocked(*g); }
    void settle() { if (group < 0 || group >= (int)groups.size() || !inStage(groups[group])) { int f = firstInStage(); group = f < 0 ? 0 : f; } }
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
    const EditGroup* current() const { return group >= 0 && group < (int)groups.size() && inStage(groups[group]) ? &groups[group] : nullptr; }
    // Look tabs: "View" then the current macro's sections that are not on the Placement panel.
    std::vector<std::string> lookTabs() const;
    // Selected panel tabs (+87): Placement, then the card's sections in the bridge's order
    std::vector<std::string> selectedTabs() const;
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
