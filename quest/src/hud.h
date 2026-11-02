// Heads-up display for Kalyx View: a slim status strip below the horizon and a set of glass panels you look down
// at (or place anywhere by grabbing their top bar) and operate with hand-tracking pinches or controller triggers.
// Everything is drawn on the CPU into compositor quad layers, on a worker thread so the frame loop never waits on
// text rasterisation. Look and material follow the Kalyx design system (graphite glass, one copper accent).
#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "font_ui.h"
#include "icons_ui.h"

namespace vrq {

struct Colour {
    float r, g, b, a;   // sRGB-encoded components, straight alpha
};

// Kalyx tokens (design system "Kalyx", 28 Oct 2026)
namespace pal {
inline const Colour kGraphite900{0.082f, 0.094f, 0.114f, 1};
inline const Colour kGraphite800{0.114f, 0.133f, 0.165f, 1};
inline const Colour kGraphite700{0.149f, 0.173f, 0.208f, 1};
inline const Colour kGraphite600{0.212f, 0.239f, 0.286f, 1};
inline const Colour kPanelBg{0.114f, 0.133f, 0.165f, 0.80f};    // graphite-800 at 80%
inline const Colour kStripBg{0.082f, 0.094f, 0.114f, 0.80f};
inline const Colour kWell{0, 0, 0, 0.45f};
inline const Colour kRim{1, 1, 1, 0.14f};
inline const Colour kFaint{1, 1, 1, 0.08f};
inline const Colour kText{0.949f, 0.953f, 0.961f, 1};            // chalk #f2f3f5
inline const Colour kDim{0.604f, 0.620f, 0.647f, 1};             // chalk dim #9a9ea5 (4.9:1 on graphite)
inline const Colour kAccent{0.878f, 0.663f, 0.478f, 1};          // copper
inline const Colour kAccentBright{0.941f, 0.737f, 0.549f, 1};
inline const Colour kAccentDeep{0.788f, 0.529f, 0.353f, 1};
inline const Colour kIce{0.561f, 0.757f, 0.851f, 1};             // status only
inline const Colour kSignal{1.0f, 0.353f, 0.302f, 1};            // playhead, errors
inline const Colour kInkOnAccent{0.082f, 0.094f, 0.114f, 1};
inline const Colour kCopperEdgeDark{0.478f, 0.267f, 0.133f, 1};   // #7a4422, the 1 px bottom edge of every copper surface
// legacy names used by the strip
inline const Colour kGreen = kIce;
inline const Colour kAmber = kSignal;
// #rrggbb with an alpha
inline Colour hex(uint32_t rgb, float a = 1) {
    return Colour{((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, a};
}
}  // namespace pal

struct GradStop {
    float t;   // 0 = top / left, 1 = bottom / right
    Colour c;
};

// UI v3 (30 Oct 2026) chip states: Rest, Hover (pointed at), On (copper satin, ink text), Pressed, Off (unavailable),
// Flat (no body: a label-only chip, used for segmented controls and quiet actions)
enum class ChipState { Rest, Hover, On, Pressed, Off, Flat };

// Linear-light, premultiplied float canvas; encode() produces premultiplied sRGB RGBA8 for an sRGB swapchain.
class Canvas {
public:
    void reset(int w, int h);
    int width() const { return m_w; }
    int height() const { return m_h; }
    void fillRoundRect(float x, float y, float w, float h, float r, Colour c);
    void strokeRoundRect(float x, float y, float w, float h, float r, float t, Colour c);
    void fillCircle(float cx, float cy, float r, Colour c);
    void strokeCircle(float cx, float cy, float r, float t, Colour c);
    // Arc of a ring: angles in radians, 0 = +x, counter-clockwise on screen (y up), from a0 to a1 (a1 > a0).
    void strokeArc(float cx, float cy, float r, float t, float a0, float a1, Colour c);
    void fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Colour c);
    // Per-pixel shading: fn(x, y) returns the sRGB colour and coverage for that pixel.
    void shadeRect(int x0, int y0, int x1, int y1, const std::function<Colour(float, float)>& fn);
    // Draws UTF-8 text with its baseline at `baseline`; returns the pen x after the text.
    float text(float x, float baseline, const std::string& s, const font_ui::Font& f, Colour c, float tracking = 0);
    static float measure(const std::string& s, const font_ui::Font& f, float tracking = 0);
    void encode(std::vector<uint8_t>& out) const;   // top row first

    // ---- Kalyx material
    // Glass panel with rim light, base line and a faint copper halo (fills the whole canvas).
    void glass(float r = 28);
    // A soft glow around a rounded rect (concentric rings; cheap stand-in for a blur).
    void glowRoundRect(float x, float y, float w, float h, float r, Colour c, float spread);
    void glowCircle(float cx, float cy, float r, Colour c, float spread);
    // Erases a rounded rect back to transparent (anti-aliased).
    void clearRoundRect(float x, float y, float w, float h, float r);
    // Inset well (tracks, dial faces, lanes): dark fill with an inner shadow at the top.
    void well(float x, float y, float w, float h, float r);
    // Bevelled button body (rest / hover / pressed / active).
    void bevel(float x, float y, float w, float h, float r, bool hover, bool pressed, bool active);
    // Domed knob with a drop shadow and a copper glow when live.
    void knob(float cx, float cy, float r, bool live);

    // ---- UI v3 (Kalyx View headset UI, 30 Oct 2026)
    // Multi-stop gradient fill of a rounded rect, vertical (top -> bottom) or horizontal.
    void fillRoundRectGrad(float x, float y, float w, float h, float r, const std::vector<GradStop>& stops, bool vertical = true);
    // Soft drop shadow under a rounded rect: offset dy, blur radius, peak alpha.
    void shadow(float x, float y, float w, float h, float r, float dy, float blur, float alpha);
    // Copper, as a metal: Satin behind text, Polished (no text) for switches, fills and pointers. 1 px white top
    // edge and 1 px #7a4422 bottom edge, no glow.
    void copper(float x, float y, float w, float h, float r, bool polished);
    // Chip body in one of the five states (text is drawn by the caller: ink on copper, chalk otherwise).
    void chipBody(float x, float y, float w, float h, float r, ChipState st);
    // Inset well for segmented controls, tracks and dial faces: #00000073 with an inner top shadow.
    void wellV3(float x, float y, float w, float h, float r);
    // Panel glass v3: graphite gradient at 92%, lit top edge; focused panels get the copper hairline.
    void glassV3(float r, bool focused, float h = -1);
    // Line icon from the baked set, centred on (cx, cy), at the nearest baked size to `size`.
    void icon(icons_ui::Icon id, float cx, float cy, int size, Colour c);
    // Text colour for a chip state
    static Colour chipInk(ChipState st);

private:
    template <typename Cov>
    void shade(int x0, int y0, int x1, int y1, Colour c, Cov cov);
    int m_w = 0, m_h = 0;
    std::vector<float> m_px;
};

enum class HudButton {
    None,
    Move,                                                 // panel grab bar
    Roll,                                                 // roll a panel up to its title bar / down again
    // view tab (settings)
    Hud, SwapEyes, Layout, Projection, DelayDown, DelayAuto, DelayUp, ResetView, PinAll, Grid, Outline, Source, FovDown, FovUp,
    FovPreset,      // arg = lens field of view in degrees (View tab presets for the fisheye projection)
    PanelToggle,    // arg = PanelId: show / hide that panel altogether (dock chips on the Instance strip)
    // instance strip
    GroupPrev, GroupNext, Copy, Paste, PasteAll, Undo, Refresh,
    Jump,       // arg = timeline frame: seek there (the empty strip's "previous / next overlay")
    Layer,      // arg 0 OVERLAYS, 1 PATCHES, 2 PICTURE: which kind of group the strip and panels show
    Lock,       // make the current patch / picture group read-only in the headset (persisted)
    GroupPick,  // arg = group index: the named chips on the strip
    ScopesView, // arg 0 waveform + vectorscope, 1 RGB parade, 2 histograms
    // deck
    ClipPrev, ClipNext, StepBack, StepFwd, Scrub, Zoom,
    // look panel
    Tab,            // arg = tab index (0 = View)
    PageUp, PageDown,
    Slider,         // arg = param index (bar)
    NudgeDown, NudgeUp,
    Toggle,
    Wheel, Bright,  // colour
    Key,            // keyframe diamond
    // placement panel
    Ring,           // yaw: angle from the centre
    Arc,            // pitch: angle from the arc's centre
    Dial,           // roll / object angles: vertical relative drag; tap = zero
    Throttle,       // size / distance: vertical absolute
};

struct ButtonRect {
    HudButton id;
    float x, y, w, h;
    int arg = 0;
};

enum class PanelId { Instance = 0, Placement, Look, Deck, Scopes, Count };
constexpr int kPanelCount = (int)PanelId::Count;

struct PanelSpec {
    int w, h;        // texture pixels
    float widthM;    // metres in the room
    float radius;    // corner radius px
};
const PanelSpec& panelSpec(PanelId id);

constexpr int kStripW = 1600, kStripH = 96;
constexpr int kCursorW = 128, kCursorH = 64;   // two 64x64 states: idle | pinching
constexpr int kLiftW = 256, kLiftH = 256;      // halo: a 160x160 rounded box with a 48 px glow margin
constexpr int kGridW = 2048, kGridH = 1024;    // level grid, equirectangular (one texel = 0.176 deg)

struct HudInfo {
    bool connected = false, usb = false, hasVideo = false;
    int width = 0, height = 0;
    std::string layout, projection;   // effective, e.g. "TB", "360"
    bool layoutAuto = true, projectionAuto = true;
    double fps = 0, mbps = 0;
    uint32_t timecode = 0xFFFFFFFF;
    enum Play { Stopped, Playing, Parked } play = Stopped;
    int delayMs = 0;
    bool delayAuto = true;
    double arriveMs = 0, decodeMs = 0, rttMs = 0;
    float hz = 0;
    uint64_t shown = 0, dropped = 0, uneven = 0;
    bool swapEyes = false, hudOn = true, pinned = false, hands = false, grid = false, outline = true;
    std::string source;       // preference: auto | resolve | ndi
    float fisheyeFov = 190;   // lens FOV used by the fisheye projection
    std::string serverName;   // what we are connected to ("Resolve", "NDI · Adobe Premiere Pro")
    std::string notice;    // transient message after an action
    std::string warning;   // e.g. dropping frames
    std::string setup;     // a Resolve setup problem with its fix, for the status line (empty = none)
    bool rendering = false;   // a change was sent and Resolve's new frame hasn't arrived yet
    double renderMs = 0;      // last measured change -> new frame round trip (0 = none yet)
    double renderWaitMs = 0;  // how long the current change has been waiting
    // scopes, measured on the Mac from the uncompressed frame
    std::shared_ptr<const std::vector<uint8_t>> scopes;   // kScopesW x kScopesH intensity, or null
    uint32_t scopesSeq = 0;
    uint32_t scopesTimecode = 0xFFFFFFFF;
    double scopesAgeMs = 0;
    int scopesView = 0;       // 0 waveform + vectorscope, 1 parade, 2 histograms
};

struct EditModel;

// Everything the panels are drawn from.
struct UiState {
    HudInfo info;
    std::shared_ptr<const EditModel> edit;
    int lookTab = 0;                       // 0 = View (settings), then the macro's sections
    HudButton hover = HudButton::None, pressed = HudButton::None;
    int hoverArg = 0, pressedArg = 0;
    int hoverPanel = -1, pressedPanel = -1;
    bool visible[kPanelCount] = {false, false, false, false, false};
    bool rolled[kPanelCount] = {false, false, false, false, false};
    bool enabled[kPanelCount] = {true, true, true, true, false};   // dock state: panel switched on at all
};

constexpr int kRolledH = 64;   // texture rows a rolled-up panel shows

void drawStrip(Canvas& c, const HudInfo& info);
void drawCursor(Canvas& c);
// Hover lift: a faint wash with a top sheen, drawn as a boxW x boxH rounded box (radius r) centred in the
// kLiftW x kLiftH texture, so the quad can be scaled uniformly to the control under the pointer (v40: a wide
// chip got a stretched 160x160 box before).
void drawLift(Canvas& c, float boxW = 160, float boxH = 160, float r = 34, float texPerPx = 1);
void drawGrid(Canvas& c);   // horizon line, yaw ticks and pitch lines, equirectangular
void drawOutline(Canvas& c);   // card outline: same geometry as the lift halo, a thin quiet line
std::vector<ButtonRect> drawScopes(Canvas& c, const UiState& s);
std::vector<ButtonRect> drawPanelById(Canvas& c, PanelId id, const UiState& s);

// Shared chrome and controls (panels.cpp uses these)
struct Chrome {
    Canvas& c;
    std::vector<ButtonRect>& rects;
    const UiState& s;
    PanelId panel;
    std::string subtitle;   // set before begin(): what the panel is acting on, drawn after the title
    bool hov(HudButton b, int arg = 0) const { return s.hover == b && s.hoverArg == arg && s.hoverPanel == (int)panel; }
    bool prs(HudButton b, int arg = 0) const { return s.pressed == b && s.pressedArg == arg && s.pressedPanel == (int)panel; }
    // glass + grab bar + small tracked title + roll chevron; returns the content top, or -1 when the panel is
    // rolled up (only the title bar is drawn; the caller returns at once)
    float begin(const std::string& title);
    void button(HudButton id, int arg, float x, float y, float w, float h, const std::string& label, bool active,
                const font_ui::Font* font = nullptr);
    void chip(HudButton id, int arg, float x, float y, float w, float h, const std::string& label, bool active) {
        button(id, arg, x, y, w, h, label, active, &font_ui::kSemiBold20);
    }
    // ---- UI v3 chips: 22 px semibold label, optional 28 px icon before it, optional small tag ("V3") after it.
    // `st` is the chip's own state; hover / pressed from the pointer override it unless it is Off. Returns the width.
    static float chipWidth(const std::string& label, int icon, const std::string& tag, float minW = 60, float padX = 18);
    float chip3(HudButton id, int arg, float x, float y, float w, float h, ChipState st, const std::string& label, int icon = -1,
                const std::string& tag = "", const std::string& tip = "");
    // square icon-only chip (60 or 52); `tip` is the word shown beside it while it is pointed at
    float iconChip(HudButton id, int arg, float x, float y, float size, ChipState st, icons_ui::Icon icon, const std::string& tip = "") {
        return chip3(id, arg, x, y, size, size, st, "", (int)icon, "", tip);
    }
    bool focused() const { return s.hoverPanel == (int)panel || s.pressedPanel == (int)panel; }
};

class HudRenderer {
public:
    void start();
    void stop();
    void update(const UiState& s);
    // Fresh pixels (premultiplied sRGB RGBA8, top row first) if something changed since the last take.
    bool takeStrip(std::vector<uint8_t>& px);
    bool takePanel(PanelId id, std::vector<uint8_t>& px, std::vector<ButtonRect>& buttons);

private:
    void threadMain();
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_running = false, m_dirty = false;
    UiState m_state;
    std::string m_stripKey, m_panelKey[kPanelCount];
    std::vector<uint8_t> m_stripPx, m_panelPx[kPanelCount];
    std::vector<ButtonRect> m_buttons[kPanelCount];
    bool m_stripReady = false, m_panelReady[kPanelCount] = {false, false, false, false};
};

std::string timecodeString(uint32_t bcd);

}  // namespace vrq
