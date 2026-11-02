// Draws the Velaria View panels on the desktop with sample data, so the UI can be judged at true pixel size without a
// headset. Writes <out>/<panel>-<state>.ppm (composited over graphite) for PIL to turn into PNGs.
//   g++ -std=c++17 -O2 -I../common -Isrc tools/hudpreview.cpp src/hud.cpp src/panels.cpp src/edit.cpp -lpthread -o host/hudpreview
#include <cstdio>
#include <string>

#include "edit.h"
#include "protocol.h"
#include <cmath>
#include <memory>
#include "hud.h"

using namespace vrq;

static void writePpm(const std::string& path, const Canvas& c) {
    std::vector<uint8_t> px;
    c.encode(px);   // premultiplied sRGB
    FILE* f = fopen(path.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", c.width(), c.height());
    const uint8_t bg[3] = {0x15, 0x18, 0x1d};
    for (size_t i = 0; i < px.size(); i += 4) {
        float a = px[i + 3] / 255.0f;
        for (int k = 0; k < 3; k++) {
            int v = (int)(px[i + k] + bg[k] * (1 - a) + 0.5f);
            fputc(v > 255 ? 255 : v, f);
        }
    }
    fclose(f);
}

static EditParam num(const char* id, const char* name, const char* section, double v, double mn, double mx, bool anim = false) {
    EditParam p;
    p.id = id; p.name = name; p.section = section; p.kind = "number"; p.value = v; p.min = mn; p.max = mx; p.animated = anim;
    return p;
}

static EditGroup card(const char* tool, const char* clipName, int track) {
    EditGroup g;
    g.tool = tool; g.clipName = clipName; g.clip = std::string(clipName) + std::to_string(track); g.track = track; g.kind = "card";
    g.source = std::string("Comp: ") + tool;
    g.params = {num("Yaw", "Yaw (Pan)", "Placement", -45, -180, 180), num("Pitch", "Pitch (Tilt)", "Placement", 15, -90, 90),
                num("Roll", "Roll", "Placement", -20, -180, 180), num("Distance", "Distance", "Placement", -3.2, -20, -0.5),
                num("ObjectTurn", "Object Turn", "Object", 35, -180, 180), num("ObjectTilt", "Object Tilt", "Object", 0, -180, 180),
                num("ObjectSpin", "Object Spin", "Object", 0, -180, 180),
                num("FrameThickness", "Frame Thickness", "Frame", 0.012, 0, 0.05, true), num("CornerRadius", "Frame Corner Radius", "Frame", 0.06, 0, 0.3),
                num("GlowAmount", "Glow Amount", "Glow", 0.7, 0, 1), num("GlowSize", "Glow Size", "Glow", 20, 0, 100)};
    EditParam sw; sw.id = "Mode"; sw.name = "Stereo Footage"; sw.section = "Stereo"; sw.kind = "bool"; sw.value = 1; sw.min = 0; sw.max = 1;
    g.params.push_back(sw);
    EditParam col; col.id = "FrameColour"; col.name = "Frame Colour"; col.section = "Frame"; col.kind = "colour"; col.cr = 0.95f; col.cg = 0.80f; col.cb = 0.55f;
    g.params.push_back(col);
    return g;
}

static EditGroup patch(const char* tool, const char* clipName, int track) {
    EditGroup g = card(tool, clipName, track);
    g.kind = "patch";
    g.params = {num("Rotate.X", "Rotate X", "Placement", 90, -180, 180), num("Rotate.Y", "Rotate Y", "Placement", 0, -180, 180),
                num("Rotate.Z", "Rotate Z", "Placement", 0, -180, 180), num("AngleOfView", "Angle of View", "Placement", 90, 10, 170)};
    return g;
}

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : "host/preview";
    auto model = std::make_shared<EditModel>();
    EditModel& m = *model;
    m.bridgeOk = true; m.haveTimeline = true; m.timelineName = "Strange Worlds 360 v12"; m.fps = 29.97;
    m.start = 108000; m.end = 108000 + 120 * 30; m.frame = 108000 + 84 * 30 + 11; m.tc = "01:01:24:11"; m.tracks = 7;
    for (int i = 0; i < 6; i++) { EditClip c; c.key = "p" + std::to_string(i); c.name = "C03" + std::to_string(i) + "_03122139"; c.track = 1; c.start = m.start + i * 600; c.end = c.start + 600; c.fusion = i % 2 == 0; m.clips.push_back(c); }
    { EditClip c; c.key = "hb"; c.name = "Hay Bale V3"; c.track = 3; c.start = m.frame - 200; c.end = m.frame + 300; c.fusion = true; c.colour = "Orange"; m.clips.push_back(c); }
    { EditClip c; c.key = "fw"; c.name = "Footage window"; c.track = 4; c.start = m.frame - 100; c.end = m.frame + 500; c.fusion = true; c.colour = "Teal"; m.clips.push_back(c); }
    { EditClip c; c.key = "lt"; c.name = "Lower third"; c.track = 5; c.start = m.frame - 50; c.end = m.frame + 200; c.fusion = true; m.clips.push_back(c); }
    m.groups = {card("Velaria3DCard_HayBale", "Hay Bale V3", 3), card("Velaria_CanvasControl", "Footage window", 4), card("Velaria_CanvasControl", "Lower third", 5),
                patch("LatLongPatcher", "C037_03122139", 2), patch("LatLongPatcher", "C037_03122139", 2), patch("LatLongPatcher", "C037_03122139", 2),
                patch("LatLongPatcher", "C037_03122139", 2), patch("LatLongPatcher", "C037_03122139", 2), patch("LatLongPatcher", "C037_03122139", 2)};
    m.groups[3].clipName = "Nadir \xC2\xB7 left eye"; m.groups[4].clipName = "Nadir \xC2\xB7 right eye"; m.groups[5].clipName = "Tripod leg";
    EditGroup orient = patch("Velaria_Orient360", "C037_03122139", 2); orient.kind = "orient"; m.groups.push_back(orient);
    EditGroup trim = card("Velaria_Trim", "Adjustment Clip", 4); trim.kind = "trim";
    trim.params = {num("Exposure", "Exposure", "Trim", 0.05, -0.5, 0.5), num("Lift", "Lift", "Trim", 0, -0.25, 0.25),
                   num("Gamma", "Gamma", "Trim", 1.05, 0.5, 2), num("Gain", "Gain", "Trim", 1, 0, 2), num("Contrast", "Contrast", "Trim", 1.1, 0.5, 2),
                   num("Saturation", "Saturation", "Trim", 1, 0, 2), num("Temperature", "Temperature", "Trim", 0.3, -1, 1), num("Tint", "Tint", "Trim", 0, -1, 1),
                   num("Amount", "Amount", "Trim", 1, 0, 1)};
    m.groups.push_back(trim);
    m.locked.insert(m.groups[3].key());
    m.group = 0; m.layer = 0;

    UiState s;
    s.edit = model;
    s.info.connected = true; s.info.usb = true; s.info.hasVideo = true; s.info.width = 5760; s.info.height = 5760;
    s.info.layout = "TB"; s.info.projection = "360"; s.info.fps = 29.97; s.info.mbps = 88; s.info.timecode = 0x01012411;
    s.info.play = HudInfo::Parked; s.info.serverName = "Resolve"; s.info.renderMs = 400; s.info.source = "auto";
    for (int i = 0; i < kPanelCount; i++) s.visible[i] = true;
    s.enabled[(int)PanelId::Scopes] = false;
    s.hoverPanel = (int)PanelId::Instance; s.hover = HudButton::Move;

    Canvas c;
    auto dump = [&](PanelId id, const char* state) {
        drawPanelById(c, id, s);
        const char* names[] = {"strip", "placement", "look", "deck", "scopes"};
        writePpm(out + "/" + names[(int)id] + "-" + state + ".ppm", c);
    };
    dump(PanelId::Instance, "overlays");
    s.hover = HudButton::Copy; s.hoverArg = 0;
    dump(PanelId::Instance, "hover");
    s.hover = HudButton::Move;
    dump(PanelId::Placement, "card");
    s.lookTab = 1;
    dump(PanelId::Look, "frame");
    s.lookTab = 0;
    dump(PanelId::Look, "view");
    dump(PanelId::Deck, "timeline");
    dump(PanelId::Scopes, "waveform");
    // patches layer, locked patch picked
    m.layer = 1; m.group = 3;
    dump(PanelId::Instance, "patches");
    dump(PanelId::Placement, "patch");
    // picture layer: Orient 360, then the Trim pass
    m.layer = 2; m.group = (int)m.groups.size() - 2;
    dump(PanelId::Instance, "picture");
    dump(PanelId::Placement, "picture");
    m.group = (int)m.groups.size() - 1;
    dump(PanelId::Instance, "trim");
    dump(PanelId::Placement, "trim");
    s.lookTab = 1; dump(PanelId::Look, "trim"); s.lookTab = 0;
    m.layer = 0; m.group = 0;
    // look tabs
    s.lookTab = 2; dump(PanelId::Look, "glow");
    s.lookTab = 3; dump(PanelId::Look, "stereo");
    s.lookTab = 0;
    // scopes views, with a synthetic measurement so the resampling can be judged: a wavy luma trace, a parade,
    // a blob on the vectorscope, a bell histogram
    {
        auto raster = std::make_shared<std::vector<uint8_t>>((size_t)vrlproto::kScopesW * vrlproto::kScopesH, 0);
        auto& R = *raster;
        auto put = [&](int x, int y, int v) { if (x >= 0 && y >= 0 && x < 768 && y < 192) R[(size_t)y * 768 + x] = (uint8_t)std::min(255, R[(size_t)y * 768 + x] + v); };
        for (int x = 0; x < 256; x++) {
            float t = x / 255.0f;
            int yc = (int)(96 - 40 * std::sin(t * 6.28f) - 10 * std::sin(t * 31.0f));
            for (int k = -6; k <= 6; k++) put(x, yc + k, 200 / (1 + k * k));
            for (int k = 0; k < 8; k++) put(x, 20 + (x * 7 + k * 13) % 100, 60);   // scattered pixels, as real footage gives
        }
        for (int ch = 0; ch < 3; ch++) for (int x = 0; x < 128; x++) {
            int yc = (int)(70 - 30 * std::sin(x / 127.0f * 6.28f + ch));
            for (int k = -4; k <= 4; k++) put(256 + ch * 128 + x, yc + k, 180 / (1 + k * k));
        }
        for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) {
            float dx = x - 70, dy = y - 60;
            put(640 + x, y, (int)(220 * std::exp(-(dx * dx + dy * dy) / 120.0f)));
        }
        for (int x = 0; x < 256; x++) {
            int hgt = (int)(60 * std::exp(-(x - 110) * (x - 110) / 1800.0f));
            for (int y = 0; y < hgt; y++) put(x, 191 - y, 200);
        }
        s.info.scopes = raster;
    }
    s.info.scopesView = 0; dump(PanelId::Scopes, "waveform");
    s.info.scopesView = 1; dump(PanelId::Scopes, "parade");
    s.info.scopesView = 2; dump(PanelId::Scopes, "histogram");
    s.info.scopesView = 0;
    // deck zoomed in
    m.zoom = 2; dump(PanelId::Deck, "zoom10s"); m.zoom = 0;
    // rolled strip
    s.rolled[(int)PanelId::Instance] = true; dump(PanelId::Instance, "rolled"); s.rolled[(int)PanelId::Instance] = false;
    // rendering state
    s.info.rendering = true; s.info.renderWaitMs = 1200; dump(PanelId::Instance, "rendering"); s.info.rendering = false;
    s.info.setup = "Timeline 5760x5760 exceeds the 7680x4320 monitor: Timeline Settings > Output scaling > 3840 x 3840";
    dump(PanelId::Instance, "setup-warning"); s.info.setup.clear();
    // fisheye view tab with lens presets
    s.info.projection = "fisheye"; s.info.layout = "SBS"; dump(PanelId::Look, "fisheye"); s.info.projection = "360"; s.info.layout = "TB";
    // nothing on this layer
    m.layer = 0; m.groups.clear(); m.frame = m.start + 1500; m.tc = "01:01:47:22";
    dump(PanelId::Instance, "empty");
    printf("wrote previews to %s\n", out.c_str());
    return 0;
}
