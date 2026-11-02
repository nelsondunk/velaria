// VR Link for Meta Quest: shows Resolve's virtual-card output on per-eye equirect compositor layers, each frame
// scheduled to the display refresh nearest its scan-out time on the Mac. A slim status strip sits below the
// horizon; looking further down opens a control panel driven by hand tracking (point and pinch) or controllers.
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <media/NdkImage.h>
#include <sys/stat.h>
#include <unistd.h>

#include <jni.h>
#include <time.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <set>
#include <string>
#include <vector>

#include "bridge.h"
#include "decoder.h"
#include "edit.h"
#include "gfx.h"
#include "hud.h"
#include "net.h"
#include "protocol.h"
#include "timing.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VRLink", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "VRLink", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "VRLink", __VA_ARGS__)

#define XRC(call)                                                         \
    do {                                                                  \
        XrResult _r = (call);                                             \
        if (XR_FAILED(_r)) LOGE("%s failed: %d (line %d)", #call, _r, __LINE__); \
    } while (0)

using namespace vrq;
using namespace vrlproto;

// ------------------------------------------------------------------------------------------------ settings

struct Settings {
    int layout = -1;       // -1 auto (from stream), else Layout
    int projection = -1;   // -1 auto, else Projection
    bool swapEyes = false;
    int delayMs = -1;      // -1 auto
    int autoDelayMs = 50;  // last settled automatic delay (start point for the next session)
    bool strip = true;     // status strip visible
    bool grid = false;     // horizon / level grid over the picture
    bool outline = true;   // thin outline on the selected / pointed-at card
    float fisheyeFov = 190; // lens field of view for the dual-fisheye projection (Canon RF 5.2: 190)
    bool panelOn[kPanelCount] = {true, true, true, true, false};   // dock: which panels exist at all (Instance strip always); Scopes off until asked
    int scopesView = 0;
    std::string source = "auto";   // auto | resolve | ndi (which Mac-side server to follow)
    std::set<std::string> locked;  // patch / orientation groups (clip|tool) the headset must not change
    float yawDeg = 0;      // panorama recentre offset
    std::string lastHost;
    std::string path;
    // Panel layout, relative to the cockpit anchor (head position and yaw when the panels opened):
    // placed flag, rolled flag, position (metres, anchor frame) and orientation (quaternion, anchor frame).
    struct PanelSave {
        bool valid = false, placed = false, rolled = false;
        float px = 0, py = 0, pz = 0, qx = 0, qy = 0, qz = 0, qw = 1;
    } panel[kPanelCount];

    void load(const std::string& dir) {
        path = dir + "/settings.txt";
        std::ifstream f(path);
        std::string k, v;
        while (f >> k >> v) {
            if (k == "layout") layout = atoi(v.c_str());
            else if (k == "projection") projection = atoi(v.c_str());
            else if (k == "swapEyes") swapEyes = v == "1";
            else if (k == "delayMs") delayMs = atoi(v.c_str());
            else if (k == "overlay" || k == "strip") strip = v == "1";
            else if (k == "grid") grid = v == "1";
            else if (k == "outline") outline = v == "1";
            else if (k == "fisheyeFov") fisheyeFov = (float)atof(v.c_str());
            else if (k == "panelOn" && v.size() >= 7) {
                for (int i = 0; i < kPanelCount; i++) if ((size_t)(i * 2) < v.size()) panelOn[i] = v[i * 2] == '1';
                panelOn[0] = true;
            }
            else if (k == "scopesView") scopesView = atoi(v.c_str());
            else if (k == "source") source = v;
            else if (k == "locked") { size_t a = 0; while (a < v.size()) { size_t b = v.find(';', a); if (b == std::string::npos) b = v.size(); if (b > a) locked.insert(v.substr(a, b - a)); a = b + 1; } }
            else if (k == "autoDelayMs") autoDelayMs = atoi(v.c_str());
            else if (k == "yawDeg") yawDeg = (float)atof(v.c_str());
            else if (k == "lastHost") lastHost = v;
            else if (k.rfind("panel", 0) == 0 && k.size() == 6) {
                int i = k[5] - '0';
                if (i >= 0 && i < kPanelCount) {
                    PanelSave& p = panel[i];
                    int pl = 0, ro = 0;
                    if (sscanf(v.c_str(), "%d,%d,%f,%f,%f,%f,%f,%f,%f", &pl, &ro, &p.px, &p.py, &p.pz, &p.qx, &p.qy, &p.qz, &p.qw) == 9) {
                        p.placed = pl != 0;
                        p.rolled = ro != 0;
                        p.valid = true;
                    }
                }
            }
        }
    }
    void save() const {
        std::ofstream f(path);
        f << "layout " << layout << "\nprojection " << projection << "\nswapEyes " << (swapEyes ? 1 : 0) << "\ndelayMs "
          << delayMs << "\nautoDelayMs " << autoDelayMs << "\nstrip " << (strip ? 1 : 0) << "\ngrid " << (grid ? 1 : 0)
          << "\noutline " << (outline ? 1 : 0) << "\nsource " << source << "\nfisheyeFov " << fisheyeFov
          << "\npanelOn " << (panelOn[0] ? 1 : 0) << "," << (panelOn[1] ? 1 : 0) << "," << (panelOn[2] ? 1 : 0) << "," << (panelOn[3] ? 1 : 0) << "," << (panelOn[4] ? 1 : 0)
          << "\nscopesView " << scopesView
          << "\nyawDeg " << yawDeg
          << "\nlastHost " << (lastHost.empty() ? "-" : lastHost) << "\n";
        if (!locked.empty()) { f << "locked "; bool first = true; for (auto& k : locked) { if (!first) f << ';'; f << k; first = false; } f << "\n"; }
        for (int i = 0; i < kPanelCount; i++) {
            const PanelSave& p = panel[i];
            if (!p.valid) continue;
            char b[200];
            snprintf(b, sizeof b, "panel%d %d,%d,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f\n", i, p.placed ? 1 : 0, p.rolled ? 1 : 0, p.px,
                     p.py, p.pz, p.qx, p.qy, p.qz, p.qw);
            f << b;
        }
    }
};

// ------------------------------------------------------------------------------------------------ app state

struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    int width = 0, height = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    bool hasContent = false;
};

struct HeldFrame {
    AImage* image = nullptr;
    FrameInfo info;
    bool haveInfo = false;
    int64_t acquiredNs = 0;
};

// One pointing source: a tracked hand (aim ray + index pinch) or a controller (aim pose + trigger).
struct Pointer {
    bool valid = false;        // has a usable ray this frame
    bool isHand = false;
    XrPosef aim{};             // in local space
    bool pressed = false;      // pinch / trigger down
    bool wasPressed = false;
    HudButton hover = HudButton::None;
    int hoverArg = 0;
    HudButton pressStart = HudButton::None;   // button under the ray when the press began
    int pressStartArg = 0;
    float hitU = 0, hitV = 0;  // panel texture coordinates of the hit (pixels), clamped to the panel
    int panel = -1;            // which panel the ray hits this frame (-1 none)
    int pressPanel = -1;       // panel the press began on (drags stay on it)
    bool onPanel = false;      // ray actually inside a panel
    bool onPlane = false;      // ray hits the press panel's plane (drags keep working just outside it)
    float grabDist = 0;        // moving a panel: distance along the ray to the grabbed point
    XrVector3f grabLocal{};    // grabbed point in panel space (metres from its centre)
    float pressU = 0, pressV = 0, maxMove = 0;   // for tap detection and relative drags
    double pressValue = 0;     // value when the drag began (dials)
    // picking cards in the scene (ray not on a panel)
    int sceneHover = -1;       // group index under the ray
    int sceneDrag = -1;        // group being moved by this pinch
    float dragPan0 = 0, dragTilt0 = 0, rayPan0 = 0, rayTilt0 = 0;   // card and ray angles when the drag began
};

struct App {
    android_app* android = nullptr;
    bool resumed = false;
    Settings settings;

    // XR
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool sessionRunning = false;
    bool exitRequested = false;
    XrSpace localSpace = XR_NULL_HANDLE, viewSpace = XR_NULL_HANDLE;
    int64_t swapchainFormat = 0;
    Swapchain video, strip, cursor, lift;   // lift: halo a few mm in front of the active control
    std::string liftKey = "160,160,34";     // box drawn in the lift texture (see drawLift)
    int64_t bridgeSinceNs = 0;   // when the bridge last connected (the "no picture" check waits a few seconds)
    bool debugAimOn = false, debugAimPressed = false;   // cmd.txt "point": a fake hand ray for tests without hands
    XrPosef debugAim{};
    Swapchain grid;                         // horizon / level grid, an equirect layer over the picture
    Swapchain outline;                      // thin card outline (quiet cousin of the lift halo)
    bool extRefresh = false, extTimespec = false, extColorSpace = false, extPerf = false, extHands = false, extAim = false;
    PFN_xrEnumerateDisplayRefreshRatesFB enumRates = nullptr;
    PFN_xrRequestDisplayRefreshRateFB requestRate = nullptr;
    PFN_xrConvertTimeToTimespecTimeKHR timeToTimespec = nullptr;
    PFN_xrSetColorSpaceFB setColorSpace = nullptr;
    PFN_xrPerfSettingsSetPerformanceLevelEXT setPerf = nullptr;
    PFN_xrCreateHandTrackerEXT createHandTracker = nullptr;
    PFN_xrDestroyHandTrackerEXT destroyHandTracker = nullptr;
    PFN_xrLocateHandJointsEXT locateHandJoints = nullptr;
    XrHandTrackerEXT handTracker[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    float currentHz = 72;
    float wantHz = 0;          // rate picked for the current stream; re-requested if the runtime switches away
    int refreshRetries = 0;

    // input
    XrActionSet actionSet = XR_NULL_HANDLE;
    XrAction actAim = XR_NULL_HANDLE, actSelect = XR_NULL_HANDLE, actMenu = XR_NULL_HANDLE, actToggleStrip = XR_NULL_HANDLE,
             actSwapEyes = XR_NULL_HANDLE, actDelay = XR_NULL_HANDLE, actDelayAuto = XR_NULL_HANDLE;
    XrPath handPath[2] = {XR_NULL_PATH, XR_NULL_PATH};
    XrSpace aimSpace[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
    Pointer pointers[2];
    float lastStickY = 0;

    // head and HUD placement
    XrPosef head{};              // view space in local space, this frame
    bool headValid = false;
    float headYaw = 0, headPitch = 0;   // radians; pitch > 0 looking up
    float stripYaw = 0;          // lazy-follow yaw of the strip and the unpinned panel
    bool stripYawInit = false;
    bool panelOpen = false;      // by looking down
    bool panelsPlaced = false;   // "Pin all": every panel stays where it is, whatever the head does
    // The unplaced panels form a cockpit anchored in the room where the head was when they opened; it stays put
    // while open (turning to look at a side panel must not move it) and re-anchors on the next open.
    XrVector3f cockpitPos{};
    float cockpitYaw = 0;
    bool cockpitValid = false;
    struct Panel {
        Swapchain sc;
        XrPosef pose{};              // this frame (hit tests and the layer)
        XrPosef placedPose{};        // where it was put by hand
        bool placed = false;
        bool rolled = false;         // only the title bar shows
        float w = 0, h = 0;          // metres (full size)
        std::vector<ButtonRect> buttons;
        bool visible = false;
    } panels[kPanelCount];
    HudRenderer hud;
    HudInfo hudInfo;
    int64_t scopesReceivedNs = 0;
    HudButton hover = HudButton::None, pressed = HudButton::None;
    int hoverArg = 0, pressedArg = 0, hoverPanel = -1, pressedPanel = -1;
    std::string lastUiKey;
    int lookTab = 0;

    // edit mode
    BridgeClient bridge;
    EditModel edit;
    int64_t lastBridgeSendNs = 0;
    int64_t lastSetNs = 0;      // when a parameter change was last sent (to time Resolve's re-render)
    double lastRenderMs = 0;
    // One change in flight at a time: while Resolve is rendering the last value we sent, a drag only updates
    // `pendingRow`; the latest value goes out the moment the new frame lands (or the wait times out). Resolve
    // then always renders the most recent hand position instead of working through a backlog of stale ones.
    std::vector<int> pendingRows;
    bool editDirty = false;
    int64_t lastDebugPollNs = 0;
    bool lastPanelVisible = false;
    int64_t lastHudUpdate = 0;
    bool hudForce = true;
    std::string notice;
    int64_t noticeUntil = 0;
    uint64_t droppedAtWindow = 0;
    int64_t dropWindowNs = 0;
    bool handsSeen = false;
    bool cursorShown = false;
    bool stripShown = false, panelShown = false;

    // media
    Gfx gfx;
    NetClient net;
    Decoder decoder{net};
    uint32_t readerGen = UINT32_MAX;
    std::vector<HeldFrame> held;                     // decoded, not yet shown (sorted by seq)
    HeldFrame current;                               // on screen
    std::deque<std::pair<AImage*, int>> retired;     // images to delete after the GPU is done (frame countdown)
    uint32_t configId = 0;
    StreamConfig cfg;
    vrltime::FrameScheduler scheduler;

    // timing stats
    int64_t autoDelayNs = 0;
    vrltime::RollingStat lateness{300};      // arrival - scanout (ms), last ~10 s of playback; negative = early
    vrltime::RollingStat photonOffset{240};  // photon time - scanout (ms); ~delay when on time
    int framesSincePlay = 0;
    int64_t lastMotionNs = 0;                // last time a new (non-parked) frame went on screen
    int64_t shrinkSinceNs = 0, lastShrinkNs = 0;
    uint32_t playbackGen = 0;
    uint32_t lastTimecode = 0xFFFFFFFF;
    int64_t lastStatsSent = 0;
};

static App g;

// ------------------------------------------------------------------------------------------------ helpers

static int64_t xrToMono(XrTime t) {
    if (g.timeToTimespec) {
        timespec ts{};
        if (XR_SUCCEEDED(g.timeToTimespec(g.instance, t, &ts))) return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    }
    return (int64_t)t;   // Quest runtime uses CLOCK_MONOTONIC nanoseconds
}

static XrQuaternionf qMul(const XrQuaternionf& a, const XrQuaternionf& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
static XrQuaternionf qConj(const XrQuaternionf& q) { return {-q.x, -q.y, -q.z, q.w}; }
static XrVector3f qRotate(const XrQuaternionf& q, const XrVector3f& v) {
    XrQuaternionf p{v.x, v.y, v.z, 0};
    XrQuaternionf r = qMul(qMul(q, p), qConj(q));
    return {r.x, r.y, r.z};
}
static XrQuaternionf qYaw(float yaw) { return {0, std::sin(yaw / 2), 0, std::cos(yaw / 2)}; }
static XrQuaternionf qPitch(float pitch) { return {std::sin(pitch / 2), 0, 0, std::cos(pitch / 2)}; }
static float wrapAngle(float a) {
    while (a > (float)M_PI) a -= 2 * (float)M_PI;
    while (a < -(float)M_PI) a += 2 * (float)M_PI;
    return a;
}
static XrVector3f vAdd(const XrVector3f& a, const XrVector3f& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static XrVector3f vSub(const XrVector3f& a, const XrVector3f& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static XrVector3f vScale(const XrVector3f& a, float s) { return {a.x * s, a.y * s, a.z * s}; }

// A quad at `dist` metres from the head, `pitch` radians above the forward horizon (negative = below), on the
// given yaw, facing the viewer.
static XrPosef placeQuad(const XrVector3f& eye, float yaw, float pitch, float dist) {
    XrQuaternionf o = qMul(qYaw(yaw), qPitch(pitch));
    XrVector3f fwd = qRotate(o, {0, 0, -1});
    return {o, vAdd(eye, vScale(fwd, dist))};
}

static Layout effectiveLayout() {
    return g.settings.layout >= 0 ? (Layout)g.settings.layout : (Layout)g.cfg.msg.layout;
}
static Projection effectiveProjection() {
    return g.settings.projection >= 0 ? (Projection)g.settings.projection : (Projection)g.cfg.msg.projection;
}

static bool createSwapchain(Swapchain& sc, int w, int h) {
    if (sc.handle != XR_NULL_HANDLE) xrDestroySwapchain(sc.handle);
    sc = Swapchain();
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    ci.format = g.swapchainFormat;
    ci.sampleCount = 1;
    ci.width = (uint32_t)w;
    ci.height = (uint32_t)h;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    XrResult r = xrCreateSwapchain(g.session, &ci, &sc.handle);
    if (XR_FAILED(r)) {
        LOGE("xrCreateSwapchain %dx%d failed %d", w, h, r);
        return false;
    }
    uint32_t n = 0;
    xrEnumerateSwapchainImages(sc.handle, 0, &n, nullptr);
    sc.images.resize(n, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    xrEnumerateSwapchainImages(sc.handle, n, &n, (XrSwapchainImageBaseHeader*)sc.images.data());
    sc.width = w;
    sc.height = h;
    std::string names;
    for (auto& im : sc.images) names += " " + std::to_string(im.image);
    LOGI("swapchain %dx%d with %u images (GL textures%s)", w, h, n, names.c_str());
    return true;
}

template <typename F>
static void renderInto(Swapchain& sc, F&& draw) {
    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(sc.handle, &ai, &idx))) return;
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = 100000000;
    XRC(xrWaitSwapchainImage(sc.handle, &wi));
    draw(sc.images[idx].image);
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    XRC(xrReleaseSwapchainImage(sc.handle, &ri));
    sc.hasContent = true;
}

static void applyRefreshRate(double fps) {
    if (!g.extRefresh || fps <= 0) return;
    uint32_t n = 0;
    g.enumRates(g.session, 0, &n, nullptr);
    std::vector<float> rates(n);
    g.enumRates(g.session, n, &n, rates.data());
    double err = 0;
    float want = vrltime::pickRefreshRate(rates, fps, &err);
    std::string list;
    for (float r : rates) list += std::to_string((int)r) + " ";
    LOGI("refresh rates available: %s-> %.0f Hz for %.3f fps (cadence error %.4f)", list.c_str(), want, fps, err);
    g.wantHz = want;
    g.refreshRetries = 0;
    if (want > 0 && std::fabs(want - g.currentHz) > 0.01f) {
        if (XR_SUCCEEDED(g.requestRate(g.session, want))) g.currentHz = want;
    }
}

static void showNotice(const std::string& s) {
    g.notice = s;
    g.noticeUntil = monoNs() + 2500000000LL;
    g.hudForce = true;
}

// ------------------------------------------------------------------------------------------------ XR setup

static bool initXr() {
    PFN_xrInitializeLoaderKHR initLoader = nullptr;
    xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&initLoader);
    if (initLoader) {
        XrLoaderInitInfoAndroidKHR li{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        li.applicationVM = g.android->activity->vm;
        li.applicationContext = g.android->activity->clazz;
        initLoader((XrLoaderInitInfoBaseHeaderKHR*)&li);
    }
    uint32_t n = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr);
    std::vector<XrExtensionProperties> props(n, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, n, &n, props.data());
    auto has = [&](const char* name) {
        for (auto& p : props)
            if (!strcmp(p.extensionName, name)) return true;
        return false;
    };
    std::vector<const char*> exts = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
                                     XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME};
    if (!has(XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME)) LOGE("runtime lacks equirect2 layers");
    if ((g.extRefresh = has(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))) exts.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    if ((g.extTimespec = has(XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME))) exts.push_back(XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME);
    if ((g.extColorSpace = has(XR_FB_COLOR_SPACE_EXTENSION_NAME))) exts.push_back(XR_FB_COLOR_SPACE_EXTENSION_NAME);
    if ((g.extPerf = has(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME))) exts.push_back(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
    if ((g.extHands = has(XR_EXT_HAND_TRACKING_EXTENSION_NAME))) exts.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
    if (g.extHands && (g.extAim = has(XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME))) exts.push_back(XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);

    XrInstanceCreateInfoAndroidKHR ai{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    ai.applicationVM = g.android->activity->vm;
    ai.applicationActivity = g.android->activity->clazz;
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    ci.next = &ai;
    strcpy(ci.applicationInfo.applicationName, "Kalyx View");
    ci.applicationInfo.applicationVersion = 1;
    strcpy(ci.applicationInfo.engineName, "Kalyx");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 34);
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.enabledExtensionNames = exts.data();
    XrResult r = xrCreateInstance(&ci, &g.instance);
    if (XR_FAILED(r)) {
        LOGE("xrCreateInstance failed %d", r);
        return false;
    }
    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XRC(xrGetSystem(g.instance, &si, &g.systemId));
    if (g.extHands) {
        XrSystemHandTrackingPropertiesEXT hp{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
        XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
        sp.next = &hp;
        xrGetSystemProperties(g.instance, g.systemId, &sp);
        g.extHands = hp.supportsHandTracking == XR_TRUE;
        LOGI("hand tracking %s, aim %s", g.extHands ? "supported" : "unsupported", g.extAim ? "yes" : "no");
    }

    PFN_xrGetOpenGLESGraphicsRequirementsKHR getReq = nullptr;
    xrGetInstanceProcAddr(g.instance, "xrGetOpenGLESGraphicsRequirementsKHR", (PFN_xrVoidFunction*)&getReq);
    XrGraphicsRequirementsOpenGLESKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    if (getReq) getReq(g.instance, g.systemId, &req);
    if (g.extRefresh) {
        xrGetInstanceProcAddr(g.instance, "xrEnumerateDisplayRefreshRatesFB", (PFN_xrVoidFunction*)&g.enumRates);
        xrGetInstanceProcAddr(g.instance, "xrRequestDisplayRefreshRateFB", (PFN_xrVoidFunction*)&g.requestRate);
        g.extRefresh = g.enumRates && g.requestRate;
    }
    if (g.extTimespec)
        xrGetInstanceProcAddr(g.instance, "xrConvertTimeToTimespecTimeKHR", (PFN_xrVoidFunction*)&g.timeToTimespec);
    if (g.extColorSpace) xrGetInstanceProcAddr(g.instance, "xrSetColorSpaceFB", (PFN_xrVoidFunction*)&g.setColorSpace);
    if (g.extPerf)
        xrGetInstanceProcAddr(g.instance, "xrPerfSettingsSetPerformanceLevelEXT", (PFN_xrVoidFunction*)&g.setPerf);
    if (g.extHands) {
        xrGetInstanceProcAddr(g.instance, "xrCreateHandTrackerEXT", (PFN_xrVoidFunction*)&g.createHandTracker);
        xrGetInstanceProcAddr(g.instance, "xrDestroyHandTrackerEXT", (PFN_xrVoidFunction*)&g.destroyHandTracker);
        xrGetInstanceProcAddr(g.instance, "xrLocateHandJointsEXT", (PFN_xrVoidFunction*)&g.locateHandJoints);
        g.extHands = g.createHandTracker && g.locateHandJoints;
    }

    if (!g.gfx.init()) {
        LOGE("EGL/GLES init failed");
        return false;
    }
    XrGraphicsBindingOpenGLESAndroidKHR gb{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    gb.display = g.gfx.display();
    gb.config = g.gfx.config();
    gb.context = g.gfx.context();
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &gb;
    sci.systemId = g.systemId;
    r = xrCreateSession(g.instance, &sci, &g.session);
    if (XR_FAILED(r)) {
        LOGE("xrCreateSession failed %d", r);
        return false;
    }
    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rs.poseInReferenceSpace.orientation.w = 1;
    XRC(xrCreateReferenceSpace(g.session, &rs, &g.localSpace));
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    XRC(xrCreateReferenceSpace(g.session, &rs, &g.viewSpace));

    uint32_t fc = 0;
    xrEnumerateSwapchainFormats(g.session, 0, &fc, nullptr);
    std::vector<int64_t> formats(fc);
    xrEnumerateSwapchainFormats(g.session, fc, &fc, formats.data());
    g.swapchainFormat = formats.empty() ? GL_SRGB8_ALPHA8 : formats[0];
    for (int64_t f : formats)
        if (f == GL_SRGB8_ALPHA8) g.swapchainFormat = f;
    LOGI("swapchain format 0x%llx", (long long)g.swapchainFormat);
    createSwapchain(g.strip, kStripW, kStripH);
    for (int i = 0; i < kPanelCount; i++) {
        const PanelSpec& sp = panelSpec((PanelId)i);
        createSwapchain(g.panels[i].sc, sp.w, sp.h);
        g.panels[i].w = sp.widthM;
        g.panels[i].h = sp.widthM * sp.h / sp.w;
    }
    createSwapchain(g.cursor, kCursorW, kCursorH);
    createSwapchain(g.lift, kLiftW, kLiftH);
    {
        Canvas c;
        drawCursor(c);
        std::vector<uint8_t> px;
        c.encode(px);
        renderInto(g.cursor, [&](GLuint tex) { g.gfx.upload(tex, px.data(), kCursorW, kCursorH); });
        drawLift(c);
        c.encode(px);
        renderInto(g.lift, [&](GLuint tex) { g.gfx.upload(tex, px.data(), kLiftW, kLiftH); });
    }
    createSwapchain(g.grid, kGridW, kGridH);
    createSwapchain(g.outline, kLiftW, kLiftH);
    {
        Canvas c;
        drawGrid(c);
        std::vector<uint8_t> px;
        c.encode(px);
        renderInto(g.grid, [&](GLuint tex) { g.gfx.upload(tex, px.data(), kGridW, kGridH); });
        drawOutline(c);
        c.encode(px);
        renderInto(g.outline, [&](GLuint tex) { g.gfx.upload(tex, px.data(), kLiftW, kLiftH); });
    }

    if (g.extHands) {
        for (int h = 0; h < 2; h++) {
            XrHandTrackerCreateInfoEXT hci{XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT};
            hci.hand = h == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
            hci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
            if (XR_FAILED(g.createHandTracker(g.session, &hci, &g.handTracker[h]))) {
                LOGW("hand tracker %d failed", h);
                g.handTracker[h] = XR_NULL_HANDLE;
            }
        }
    }

    // ---- input (controllers)
    XrActionSetCreateInfo asci{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(asci.actionSetName, "vrlink");
    strcpy(asci.localizedActionSetName, "Kalyx View");
    XRC(xrCreateActionSet(g.instance, &asci, &g.actionSet));
    auto path = [&](const char* s) {
        XrPath p;
        xrStringToPath(g.instance, s, &p);
        return p;
    };
    g.handPath[0] = path("/user/hand/left");
    g.handPath[1] = path("/user/hand/right");
    auto mk = [&](const char* name, const char* loc, XrActionType type, XrAction& out, bool perHand) {
        XrActionCreateInfo a{XR_TYPE_ACTION_CREATE_INFO};
        a.actionType = type;
        strcpy(a.actionName, name);
        strcpy(a.localizedActionName, loc);
        if (perHand) {
            a.countSubactionPaths = 2;
            a.subactionPaths = g.handPath;
        }
        XRC(xrCreateAction(g.actionSet, &a, &out));
    };
    mk("aim", "Aim", XR_ACTION_TYPE_POSE_INPUT, g.actAim, true);
    mk("select", "Select", XR_ACTION_TYPE_FLOAT_INPUT, g.actSelect, true);
    mk("menu", "Pin panel", XR_ACTION_TYPE_BOOLEAN_INPUT, g.actMenu, false);
    mk("toggle_strip", "Toggle status strip", XR_ACTION_TYPE_BOOLEAN_INPUT, g.actToggleStrip, false);
    mk("swap_eyes", "Swap eyes", XR_ACTION_TYPE_BOOLEAN_INPUT, g.actSwapEyes, false);
    mk("delay", "Playout delay", XR_ACTION_TYPE_FLOAT_INPUT, g.actDelay, false);
    mk("delay_auto", "Automatic delay", XR_ACTION_TYPE_BOOLEAN_INPUT, g.actDelayAuto, false);
    std::vector<XrActionSuggestedBinding> b = {
        {g.actAim, path("/user/hand/left/input/aim/pose")},
        {g.actAim, path("/user/hand/right/input/aim/pose")},
        {g.actSelect, path("/user/hand/left/input/trigger/value")},
        {g.actSelect, path("/user/hand/right/input/trigger/value")},
        {g.actMenu, path("/user/hand/left/input/menu/click")},
        {g.actToggleStrip, path("/user/hand/right/input/a/click")},
        {g.actSwapEyes, path("/user/hand/right/input/b/click")},
        {g.actDelay, path("/user/hand/right/input/thumbstick/y")},
        {g.actDelayAuto, path("/user/hand/right/input/thumbstick/click")},
    };
    XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sb.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
    sb.suggestedBindings = b.data();
    sb.countSuggestedBindings = (uint32_t)b.size();
    XRC(xrSuggestInteractionProfileBindings(g.instance, &sb));
    XrSessionActionSetsAttachInfo at{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    at.countActionSets = 1;
    at.actionSets = &g.actionSet;
    XRC(xrAttachSessionActionSets(g.session, &at));
    for (int h = 0; h < 2; h++) {
        XrActionSpaceCreateInfo asc{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        asc.action = g.actAim;
        asc.subactionPath = g.handPath[h];
        asc.poseInActionSpace.orientation.w = 1;
        XRC(xrCreateActionSpace(g.session, &asc, &g.aimSpace[h]));
    }
    return true;
}

static bool pressed(XrAction a) {
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a;
    XrActionStateBoolean s{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_FAILED(xrGetActionStateBoolean(g.session, &gi, &s))) return false;
    return s.isActive && s.changedSinceLastSync && s.currentState;
}

// ------------------------------------------------------------------------------------------------ actions

static void setLayout(int v) {
    g.settings.layout = v;
    const char* lay[] = {"Mono", "Side by side", "Over/under"};
    showNotice(v < 0 ? "Layout: auto" : std::string("Layout: ") + lay[v % 3]);
}
static void setProjection(int v) {
    g.settings.projection = v;
    const char* proj[] = {"Flat", "180\xC2\xB0", "360\xC2\xB0", "Fisheye 180\xC2\xB0"};
    showNotice(v < 0 ? "Projection: auto" : std::string("Projection: ") + proj[v % 4]);
}
static void nudgeDelay(int deltaMs) {
    int cur = g.settings.delayMs >= 0 ? g.settings.delayMs : (int)(g.autoDelayNs / 1000000);
    g.settings.delayMs = std::max(0, std::min(1000, cur + deltaMs));
    char b[48];
    snprintf(b, sizeof b, "Delay %d ms", g.settings.delayMs);
    showNotice(b);
}
static void recentreView() {
    if (!g.headValid) return;
    g.settings.yawDeg = g.headYaw * 180.0f / (float)M_PI;
    showNotice("View recentred");
}
// Panel layout persistence: poses are stored relative to the cockpit anchor so they come back in the same place
// around the user next session, whatever the runtime picked as the origin.
static XrPosef toAnchorFrame(const XrPosef& p) {
    XrQuaternionf inv = qYaw(-g.cockpitYaw);
    XrVector3f rel = qRotate(inv, vSub(p.position, g.cockpitPos));
    return {qMul(inv, p.orientation), rel};
}
static XrPosef fromAnchorFrame(const XrPosef& p) {
    XrQuaternionf r = qYaw(g.cockpitYaw);
    return {qMul(r, p.orientation), vAdd(g.cockpitPos, qRotate(r, p.position))};
}
static void savePanelLayout() {
    if (!g.cockpitValid) return;
    for (int i = 0; i < kPanelCount; i++) {
        const App::Panel& pn = g.panels[i];
        Settings::PanelSave& sv = g.settings.panel[i];
        sv.valid = true;
        sv.placed = pn.placed;
        sv.rolled = pn.rolled;
        XrPosef a = toAnchorFrame(pn.placed ? pn.placedPose : pn.pose);
        sv.px = a.position.x;
        sv.py = a.position.y;
        sv.pz = a.position.z;
        sv.qx = a.orientation.x;
        sv.qy = a.orientation.y;
        sv.qz = a.orientation.z;
        sv.qw = a.orientation.w;
    }
    g.settings.save();
}
static void restorePanelLayout() {
    for (int i = 0; i < kPanelCount; i++) {
        const Settings::PanelSave& sv = g.settings.panel[i];
        if (!sv.valid) continue;
        App::Panel& pn = g.panels[i];
        pn.rolled = sv.rolled;
        float n2 = sv.qx * sv.qx + sv.qy * sv.qy + sv.qz * sv.qz + sv.qw * sv.qw;
        bool sane = std::isfinite(n2) && std::fabs(n2 - 1.0f) < 0.02f && std::isfinite(sv.px) && std::isfinite(sv.py) && std::isfinite(sv.pz) &&
                    std::fabs(sv.px) < 10 && std::fabs(sv.py) < 10 && std::fabs(sv.pz) < 10;
        if (sv.placed && sane) {
            // the file holds five decimals: renormalise, the compositor wants a unit quaternion exactly
            float inv = 1.0f / std::sqrt(n2);
            pn.placedPose = fromAnchorFrame({{sv.qx * inv, sv.qy * inv, sv.qz * inv, sv.qw * inv}, {sv.px, sv.py, sv.pz}});
            pn.placed = true;
        } else if (sv.placed) {
            // a saved pose that is not a real pose (zero quaternion from a panel that was never positioned) would
            // fail every frame with XR_ERROR_POSE_INVALID: the panel goes back to its cockpit slot instead
            LOGE("panel %d: ignoring a bad saved pose (q norm2 %.3f)", i, n2);
            pn.placed = false;
        }
    }
    g.panelsPlaced = true;
    for (auto& pn : g.panels) g.panelsPlaced = g.panelsPlaced && pn.placed;
}

static void placeAll(bool place) {
    g.panelsPlaced = place;
    if (place) {
        for (auto& p : g.panels) {
            p.placedPose = p.pose;
            p.placed = true;
        }
    } else {
        for (auto& p : g.panels) p.placed = false;
    }
    showNotice(place ? "Panels placed" : "Panels follow you again");
    savePanelLayout();
}

static void bridgeCmd(const char* cmd);
static EditParam* editParam(int row);
static void sendParam(int row);
static void nudge(int row, int dir);

static void activate(HudButton b, int arg = 0) {
    LOGI("activate %d arg %d", (int)b, arg);
    switch (b) {
        case HudButton::Tab:
            g.lookTab = arg;
            g.edit.firstRow = 0;
            g.hudForce = true;
            return;
        case HudButton::ClipPrev:
        case HudButton::ClipNext: {
            Json j = Json::object();
            j["cmd"] = "clip";
            j["dir"] = b == HudButton::ClipNext ? 1 : -1;
            g.bridge.send(j);
            return;
        }
        case HudButton::StepBack:
        case HudButton::StepFwd: {
            Json j = Json::object();
            j["cmd"] = "step";
            j["delta"] = b == HudButton::StepFwd ? 1 : -1;
            g.bridge.send(j);
            return;
        }
        case HudButton::Undo: bridgeCmd("undo"); return;
        case HudButton::Jump: {
            Json j = Json::object();
            j["cmd"] = "seek";
            j["frame"] = (int64_t)arg;
            g.bridge.send(j);
            return;
        }
        case HudButton::Refresh:
            bridgeCmd("timeline");
            bridgeCmd("params");
            return;
        case HudButton::GroupPrev:
        case HudButton::GroupNext:
            if (!g.edit.groups.empty()) {
                int i = g.edit.neighbourInLayer(b == HudButton::GroupNext ? 1 : -1);
                if (i >= 0) g.edit.group = i;
                g.edit.firstRow = 0;
                g.hudForce = true;
            }
            return;
        case HudButton::Layer:
            g.edit.layer = std::max(0, std::min(2, arg));
            g.edit.settle();
            g.edit.firstRow = 0;
            g.hudForce = true;
            showNotice(g.edit.layer == 0 ? "OVERLAYS: titles and footage windows placed on the picture. Pinch one in the scene to pick it."
                       : g.edit.layer == 1 ? "PATCHES: tripod and sky patch windows. Set up in Resolve; move here only on purpose."
                                           : "PICTURE: the whole sphere (PanoMap / Kalyx Orient 360) and the Kalyx Trim pass.");
            return;
        case HudButton::ScopesView:
            g.settings.scopesView = std::max(0, std::min(2, arg));
            g.settings.save();
            g.hudForce = true;
            return;
        case HudButton::GroupPick:
            if (arg >= 0 && arg < (int)g.edit.groups.size() && g.edit.inLayer(g.edit.groups[arg])) {
                g.edit.group = arg;
                g.edit.firstRow = 0;
                g.hudForce = true;
            }
            return;
        case HudButton::Lock:
            if (const EditGroup* gr = g.edit.current()) {
                std::string k = gr->key();
                if (g.edit.locked.count(k)) g.edit.locked.erase(k); else g.edit.locked.insert(k);
                g.settings.locked = g.edit.locked;
                g.settings.save();
                g.hudForce = true;
                showNotice(g.edit.locked.count(k) ? "Locked: the headset won't change this one" : "Unlocked");
            }
            return;
        case HudButton::PageUp:
            g.edit.firstRow = std::max(0, g.edit.firstRow - 3);
            g.hudForce = true;
            return;
        case HudButton::PageDown:
            g.edit.firstRow += 3;
            g.hudForce = true;
            return;
        case HudButton::NudgeDown: nudge(arg, -1); return;
        case HudButton::NudgeUp: nudge(arg, +1); return;
        case HudButton::Toggle: {
            EditParam* p = editParam(arg);
            if (p) {
                p->value = p->value != 0 ? 0 : 1;
                sendParam(arg);
                g.hudForce = true;
            }
            return;
        }
        case HudButton::Roll:
            if (g.hoverPanel >= 0 && g.hoverPanel < kPanelCount) {
                g.panels[g.hoverPanel].rolled = !g.panels[g.hoverPanel].rolled;
                g.hudForce = true;
                savePanelLayout();
            }
            return;
        case HudButton::Zoom:
            g.edit.zoom = (g.edit.zoom + 1) % 3;
            g.hudForce = true;
            return;
        case HudButton::Key: {
            EditParam* p = editParam(arg);
            if (!p) return;
            const EditGroup& gr = g.edit.groups[g.edit.group];
            Json j = Json::object();
            j["cmd"] = "key";
            j["clip"] = gr.clip;
            j["tool"] = gr.tool;
            Json ids = Json::array(), v = Json::array();
            if (p->kind == "colour" && p->ids.size() >= 3) {
                for (auto& id : p->ids) ids.push(id);
                v.push((double)p->cr);
                v.push((double)p->cg);
                v.push((double)p->cb);
            } else {
                ids.push(p->id);
                v.push(p->value);
            }
            j["inputs"] = ids;
            j["values"] = v;
            g.bridge.send(j);
            LOGI("key %s.%s", gr.tool.c_str(), p->id.c_str());
            return;
        }
        case HudButton::Copy: {
            if (g.edit.group >= (int)g.edit.groups.size()) return;
            const EditGroup& gr = g.edit.groups[g.edit.group];
            g.edit.clipboard.clear();
            for (auto& p : gr.params) {
                if (p.kind == "colour" && p.ids.size() >= 3) {
                    g.edit.clipboard[p.ids[0]] = {p.section, (double)p.cr};
                    g.edit.clipboard[p.ids[1]] = {p.section, (double)p.cg};
                    g.edit.clipboard[p.ids[2]] = {p.section, (double)p.cb};
                } else if (p.kind == "number" || p.kind == "bool") {
                    g.edit.clipboard[p.id] = {p.section, p.value};
                }
            }
            g.edit.clipboardFrom = gr.tool + " on V" + std::to_string(gr.track);
            g.edit.notice = "Copied " + std::to_string(g.edit.clipboard.size()) + " settings";
            g.hudForce = true;
            LOGI("copy %zu inputs from %s", g.edit.clipboard.size(), g.edit.clipboardFrom.c_str());
            return;
        }
        case HudButton::Paste:
        case HudButton::PasteAll: {
            if (g.edit.group >= (int)g.edit.groups.size() || g.edit.clipboard.empty()) return;
            const EditGroup& gr = g.edit.groups[g.edit.group];
            Json j = Json::object();
            j["cmd"] = "paste";
            j["clip"] = gr.clip;
            j["tool"] = gr.tool;
            Json vals = Json::object();
            int n = 0;
            for (auto& kv : g.edit.clipboard) {
                if (b == HudButton::Paste && (kv.second.section == "Placement" || kv.second.section == "Object")) continue;
                vals[kv.first] = kv.second.value;
                n++;
            }
            j["values"] = vals;
            g.bridge.send(j);
            LOGI("paste %d inputs into %s (%s)", n, gr.tool.c_str(), b == HudButton::Paste ? "look" : "all");
            return;
        }
        case HudButton::Dial: {   // a tap (no drag) zeroes the dial
            EditParam* p = editParam(arg);
            if (p) {
                p->value = 0;
                sendParam(arg);
                g.hudForce = true;
            }
            return;
        }
        case HudButton::Move:
        case HudButton::Slider:
        case HudButton::Scrub:
        case HudButton::Wheel:
        case HudButton::Bright:
        case HudButton::Ring:
        case HudButton::Arc:
        case HudButton::Throttle:
            return;   // handled as drags
        case HudButton::Hud:
            g.settings.strip = !g.settings.strip;
            showNotice(g.settings.strip ? "Status bar on" : "Status bar off");
            break;
        case HudButton::SwapEyes:
            g.settings.swapEyes = !g.settings.swapEyes;
            showNotice(g.settings.swapEyes ? "Eyes swapped" : "Eyes normal");
            break;
        case HudButton::Layout: setLayout(g.settings.layout >= 2 ? -1 : g.settings.layout + 1); break;
        case HudButton::Projection: setProjection(g.settings.projection >= 3 ? -1 : g.settings.projection + 1); break;
        case HudButton::DelayDown: nudgeDelay(-10); break;
        case HudButton::DelayUp: nudgeDelay(+10); break;
        case HudButton::DelayAuto:
            g.settings.delayMs = -1;
            showNotice("Delay: automatic");
            break;
        case HudButton::ResetView: recentreView(); break;
        case HudButton::PinAll: placeAll(!g.panelsPlaced); break;
        case HudButton::Grid:
            g.settings.grid = !g.settings.grid;
            showNotice(g.settings.grid ? "Level grid on" : "Level grid off");
            break;
        case HudButton::Outline:
            g.settings.outline = !g.settings.outline;
            showNotice(g.settings.outline ? "Card outline on" : "Card outline off");
            break;
        case HudButton::PanelToggle:
            if (arg > 0 && arg < kPanelCount) {
                g.settings.panelOn[arg] = !g.settings.panelOn[arg];
                static const char* names[] = {"Instance", "Placement", "Look", "Timeline", "Scopes"};
                showNotice(std::string(names[arg]) + (g.settings.panelOn[arg] ? " panel on" : " panel hidden"));
            }
            break;
        case HudButton::FovPreset: {
            g.settings.fisheyeFov = std::max(120.0f, std::min(240.0f, (float)arg));
            char m[64];
            snprintf(m, sizeof m, "Lens field of view %.0f\xC2\xB0", g.settings.fisheyeFov);
            showNotice(m);
            g.settings.save();
            return;
        }
        case HudButton::FovDown:
        case HudButton::FovUp: {
            g.settings.fisheyeFov = std::max(120.0f, std::min(240.0f, g.settings.fisheyeFov + (b == HudButton::FovUp ? 5 : -5)));
            char m[64];
            snprintf(m, sizeof m, "Lens field of view %.0f\xC2\xB0", g.settings.fisheyeFov);
            showNotice(m);
            break;
        }
        case HudButton::Source: {
            // auto -> resolve -> ndi -> auto
            const std::string& s = g.settings.source;
            g.settings.source = s == "auto" ? "resolve" : s == "resolve" ? "ndi" : "auto";
            g.net.setSource(g.settings.source);
            showNotice(g.settings.source == "auto" ? "Source: automatic (Resolve, Premiere or After Effects)" : g.settings.source == "ndi" ? "Source: Premiere / After Effects only" : "Source: Resolve only");
            break;
        }
        default: return;
    }
    g.settings.save();
    g.hudForce = true;
}

// ------------------------------------------------------------------------------------------------ input

// Ray vs one panel quad. Returns true when the ray hits the panel's plane in front of the hand; `t` is the distance,
// `inside` whether it is within the panel. u/v are texture coordinates, clamped to the panel.
// The quad the panel is shown as: full, or the title bar only when rolled (its top edge stays where it was).
static void panelQuadGeom(const App::Panel& pn, PanelId id, XrPosef& pose, float& hM, int& texH) {
    const PanelSpec& sp = panelSpec(id);
    texH = pn.rolled ? kRolledH : sp.h;
    hM = pn.w * texH / sp.w;
    pose = pn.pose;
    if (pn.rolled) pose.position = vAdd(pose.position, qRotate(pose.orientation, {0, (pn.h - hM) / 2, 0}));
}

static bool hitOnePanel(const App::Panel& pn, PanelId id, const XrPosef& ray, float& u, float& v, bool& inside, float& t) {
    const PanelSpec& sp = panelSpec(id);
    XrPosef pose;
    float hM;
    int texH;
    panelQuadGeom(pn, id, pose, hM, texH);
    XrQuaternionf inv = qConj(pose.orientation);
    XrVector3f o = qRotate(inv, vSub(ray.position, pose.position));
    XrVector3f d = qRotate(inv, qRotate(ray.orientation, {0, 0, -1}));
    if (std::fabs(d.z) < 1e-4f) return false;
    t = -o.z / d.z;
    if (t < 0.02f || t > 4.0f) return false;
    float x = o.x + d.x * t, y = o.y + d.y * t;
    inside = std::fabs(x) <= pn.w / 2 && std::fabs(y) <= hM / 2;
    u = std::max(0.0f, std::min((float)sp.w, (x + pn.w / 2) / pn.w * sp.w));
    v = std::max(0.0f, std::min((float)texH, (hM / 2 - y) / hM * texH));
    return true;
}

static void buttonAt(const App::Panel& pn, float u, float v, HudButton& button, int& arg) {
    button = HudButton::None;
    arg = 0;
    for (auto& r : pn.buttons)
        if (u >= r.x && u < r.x + r.w && v >= r.y && v < r.y + r.h) {
            button = r.id;
            arg = r.arg;
        }
}

// Nearest visible panel the ray enters. Returns its index or -1.
static int hitPanels(const XrPosef& ray, float& u, float& v, HudButton& button, int& arg) {
    int best = -1;
    float bestT = 1e9f;
    for (int i = 0; i < kPanelCount; i++) {
        const App::Panel& pn = g.panels[i];
        if (!pn.visible) continue;
        float uu, vv, t;
        bool inside;
        if (!hitOnePanel(pn, (PanelId)i, ray, uu, vv, inside, t) || !inside) continue;
        if (t < bestT) {
            bestT = t;
            best = i;
            u = uu;
            v = vv;
        }
    }
    if (best >= 0) buttonAt(g.panels[best], u, v, button, arg);
    return best;
}

static const ButtonRect* findRect(HudButton id, int arg, int* panelOut = nullptr) {
    for (int i = 0; i < kPanelCount; i++)
        for (auto& r : g.panels[i].buttons)
            if (r.id == id && r.arg == arg) {
                if (panelOut) *panelOut = i;
                return &r;
            }
    return nullptr;
}

// ---- edit-mode actions (through the bridge)
static void bridgeCmd(const char* cmd) {
    Json j = Json::object();
    j["cmd"] = cmd;
    g.bridge.send(j);
}

static EditParam* editParam(int row) {
    if (g.edit.group < 0 || g.edit.group >= (int)g.edit.groups.size()) return nullptr;
    if (g.edit.currentLocked()) {   // every write path comes through here: a locked group is read-only
        static int64_t lastNoticeNs = 0;
        int64_t now = monoNs();
        if (now - lastNoticeNs > 1500000000LL) { lastNoticeNs = now; showNotice("Locked \xC2\xB7 tap Lock on the strip to edit"); }
        return nullptr;
    }
    auto& ps = g.edit.groups[g.edit.group].params;
    if (row < 0 || row >= (int)ps.size()) return nullptr;
    return &ps[row];
}

static void sendParam(int row) {
    EditParam* p = editParam(row);
    if (!p) return;
    const EditGroup& gr = g.edit.groups[g.edit.group];
    if (g.edit.isLocked(gr)) return;
    if (p->kind == "colour" && p->ids.size() >= 3) {
        float comp[3] = {p->cr, p->cg, p->cb};
        for (int i = 0; i < 3; i++) {
            Json j = Json::object();
            j["cmd"] = "set";
            j["clip"] = gr.clip;
            j["tool"] = gr.tool;
            j["input"] = p->ids[i];
            j["value"] = (double)comp[i];
            g.bridge.send(j);
        }
        g.lastBridgeSendNs = monoNs();
        g.lastSetNs = g.lastBridgeSendNs;
        LOGI("set %s.%s = %.3f %.3f %.3f", gr.tool.c_str(), p->id.c_str(), p->cr, p->cg, p->cb);
        return;
    }
    Json j = Json::object();
    j["cmd"] = "set";
    j["clip"] = gr.clip;
    j["tool"] = gr.tool;
    j["input"] = p->id;
    if (p->kind == "point") {
        Json v = Json::array();
        v.push(p->px);
        v.push(p->py);
        j["value"] = v;
    } else
        j["value"] = p->value;
    g.bridge.send(j);
    g.lastBridgeSendNs = monoNs();
    g.lastSetNs = g.lastBridgeSendNs;
    LOGI("set %s.%s = %s", gr.tool.c_str(), p->id.c_str(), j["value"].dump().c_str());
}

static void scrubTo(float u, bool release);

// Is Resolve still rendering the last change we sent? Waits at most ~1.5x the last measured render time
// (bounded 150 ms .. 2.5 s) so a lost frame can't stall a drag.
static bool renderInFlight(int64_t now) {
    if (!g.lastSetNs) return false;
    double budget = std::max(150.0, std::min(2500.0, g.lastRenderMs > 0 ? g.lastRenderMs * 1.5 : 400.0));
    return (now - g.lastSetNs) / 1e6 < budget;
}

// Sends a changed row now if Resolve is free, otherwise remembers it for when the current render lands.
static void queueParam(int row, bool release) {
    int64_t now = monoNs();
    if (renderInFlight(now) && !release) {
        if (std::find(g.pendingRows.begin(), g.pendingRows.end(), row) == g.pendingRows.end()) g.pendingRows.push_back(row);
        return;
    }
    if (release) {
        // the release value supersedes anything queued for this row; other queued rows go out with it
        g.pendingRows.erase(std::remove(g.pendingRows.begin(), g.pendingRows.end(), row), g.pendingRows.end());
        for (int r : g.pendingRows) sendParam(r);
        g.pendingRows.clear();
    }
    sendParam(row);
}

// Called when a new frame arrives after a change (and from the frame loop as a timeout): flushes the values
// the hand has moved to in the meantime (the bridge applies them as one batch, so Resolve renders once).
static void flushPendingParam() {
    if (g.pendingRows.empty()) return;
    if (renderInFlight(monoNs())) return;
    std::vector<int> rows;
    rows.swap(g.pendingRows);
    for (int r : rows) sendParam(r);
}

static void setValue(int row, double v, bool release) {
    EditParam* p = editParam(row);
    if (!p) return;
    if (v == p->value && !release) return;
    p->value = v;
    g.edit.dragRow = release ? -1 : row;
    g.hudForce = true;
    if (release || monoNs() - g.lastBridgeSendNs > 50000000LL) queueParam(row, release);
}

static double roundFor(const EditParam& p, double v) {
    if (EditModel::logScale(p)) return std::round(v * 100) / 100;
    if (std::fabs(p.max - p.min) > 50) return std::round(v * 10) / 10;   // degrees etc.: 0.1 steps
    return v;
}

// Bar: position along the track sets the value (absolute), sent at most every 50 ms.
static void dragSlider(int row, float u, bool release) {
    const ButtonRect* r = findRect(HudButton::Slider, row);
    EditParam* p = editParam(row);
    if (!r || !p || p->kind != "number") return;
    float t = std::max(0.0f, std::min(1.0f, (u - (r->x + 24)) / (r->w - 48)));
    setValue(row, roundFor(*p, EditModel::fromT(*p, t)), release);
}

static void nudge(int row, int dir) {
    EditParam* p = editParam(row);
    if (!p || p->kind != "number") return;
    double step = EditModel::nudgeStep(*p);
    double v = std::round((p->value + dir * step) / step) * step;
    v = std::max(std::min(p->min, p->max), std::min(std::max(p->min, p->max), v));
    setValue(row, v, true);
}

// Throttle: vertical, up = more.
static void dragThrottle(int row, float v, bool release) {
    const ButtonRect* r = findRect(HudButton::Throttle, row);
    EditParam* p = editParam(row);
    if (!r || !p) return;
    float top = r->y + 20 + 6, bottom = r->y + r->h - 20 - 6;
    float t = std::max(0.0f, std::min(1.0f, (bottom - v) / (bottom - top)));
    setValue(row, roundFor(*p, EditModel::fromT(*p, t)), release);
}

// Ring: angle of the hit point around the centre (front = up, positive = left).
static void dragRing(int row, float u, float v, bool release) {
    const ButtonRect* r = findRect(HudButton::Ring, row);
    EditParam* p = editParam(row);
    if (!r || !p) return;
    float cx = r->x + r->w / 2, cy = r->y + r->h / 2;
    float dx = u - cx, dy = v - cy;
    if (std::hypot(dx, dy) < 25) return;   // too close to the centre to mean anything
    double yaw = std::atan2(-dx, -dy) * 180 / M_PI;
    setValue(row, std::round(yaw * 10) / 10, release);
}

// Arc: angle above the horizon of the hit point, clamped to the control's range.
static void dragArc(int row, float u, float v, bool release) {
    const ButtonRect* r = findRect(HudButton::Arc, row);
    EditParam* p = editParam(row);
    if (!r || !p) return;
    float cx = r->x + 10, cy = r->y + r->h / 2;
    float dx = u - cx, dy = cy - v;
    if (std::hypot(dx, dy) < 25) return;
    double pitch = std::atan2(dy, std::max(1.0f, dx)) * 180 / M_PI;
    pitch = std::max(std::min(p->min, p->max), std::min(std::max(p->min, p->max), pitch));
    setValue(row, std::round(pitch * 10) / 10, release);
}

// Dial: relative vertical drag, 0.25 degrees per pixel (a 10 px pinch jitter is 2.5 degrees).
static void dragDial(int row, const Pointer& ptr, bool release) {
    EditParam* p = editParam(row);
    if (!p) return;
    double v = ptr.pressValue + (ptr.pressV - ptr.hitV) * 0.25;
    double lo = std::min(p->min, p->max), hi = std::max(p->min, p->max);
    if (hi - lo >= 359) {   // full-circle dials wrap
        while (v > 180) v -= 360;
        while (v < -180) v += 360;
    } else
        v = std::max(lo, std::min(hi, v));
    setValue(row, std::round(v * 10) / 10, release);
}

// Colour wheel: the hit point's angle is the hue, its distance from the centre the saturation; brightness is kept.
static void dragColour(HudButton which, int row, float u, float v, bool release) {
    EditParam* p = editParam(row);
    const ButtonRect* wheel = findRect(HudButton::Wheel, row);
    const ButtonRect* bar = findRect(HudButton::Bright, row);
    if (!p || p->kind != "colour" || !wheel || !bar) return;
    float R = (wheel->w - 24) / 2, cx = wheel->x + wheel->w / 2, cy = wheel->y + wheel->h / 2;
    float barX = bar->x + 20, barW = bar->w - 40;
    float h, sat, val;
    rgbToHsv(p->cr, p->cg, p->cb, h, sat, val);
    if (which == HudButton::Wheel) {
        float dx = u - cx, dy = v - cy;
        float d = std::hypot(dx, dy);
        sat = std::min(1.0f, d / R);
        if (d > 2) {
            h = std::atan2(-dy, dx) / (2 * 3.14159265f);
            if (h < 0) h += 1;
        }
        if (val < 0.05f) val = 0.05f;
    } else {
        val = std::max(0.0f, std::min(1.0f, (u - barX) / barW));
    }
    float r, gg, b;
    hsvToRgb(h, sat, val, r, gg, b);
    if (std::fabs(r - p->cr) < 1e-4f && std::fabs(gg - p->cg) < 1e-4f && std::fabs(b - p->cb) < 1e-4f && !release) return;
    p->cr = r;
    p->cg = gg;
    p->cb = b;
    g.edit.dragRow = release ? -1 : row;
    g.hudForce = true;
    if (release || monoNs() - g.lastBridgeSendNs > 50000000LL) queueParam(row, release);
}

static bool liftable(HudButton b) {
    switch (b) {
        case HudButton::None: case HudButton::Move: case HudButton::Scrub: case HudButton::Roll: return false;
        default: return true;
    }
}

static bool isDragButton(HudButton b) {
    return b == HudButton::Slider || b == HudButton::Scrub || b == HudButton::Wheel || b == HudButton::Bright ||
           b == HudButton::Ring || b == HudButton::Arc || b == HudButton::Dial || b == HudButton::Throttle;
}

static void drag(const Pointer& ptr, bool release) {
    HudButton b = ptr.pressStart;
    int arg = ptr.pressStartArg;
    float u = ptr.hitU, v = ptr.hitV;
    if (b == HudButton::Slider) dragSlider(arg, u, release);
    else if (b == HudButton::Scrub) scrubTo(u, release);
    else if (b == HudButton::Wheel || b == HudButton::Bright) dragColour(b, arg, u, v, release);
    else if (b == HudButton::Ring) dragRing(arg, u, v, release);
    else if (b == HudButton::Arc) dragArc(arg, u, v, release);
    else if (b == HudButton::Throttle) dragThrottle(arg, v, release);
    else if (b == HudButton::Dial) {
        if (ptr.maxMove > 14 || release) dragDial(arg, ptr, release);
    }
}

static void scrubTo(float u, bool release) {
    const ButtonRect* r = findRect(HudButton::Scrub, 0);
    if (!r || !g.edit.haveTimeline) return;
    float t = std::max(0.0f, std::min(1.0f, (u - r->x) / r->w));
    int64_t va, vb;
    g.edit.viewRange(va, vb);
    int64_t frame = va + (int64_t)std::llround(t * (double)(vb - va));
    if (!release && monoNs() - g.lastBridgeSendNs < 100000000LL) return;
    Json j = Json::object();
    j["cmd"] = "seek";
    j["frame"] = (int64_t)frame;
    g.bridge.send(j);
    g.lastBridgeSendNs = monoNs();
}


// ---- cards in the scene
// A card's direction on the sphere from its Pan (positive = left) and Tilt (positive = up), in the video's frame.
struct CardGeom {
    bool valid = false;
    int panRow = -1, tiltRow = -1;
    float pan = 0, tilt = 0;        // degrees
    float dist = 1.5f, scale = 1;   // metres, Fusion units
};
static CardGeom cardGeom(const EditGroup& gr) {
    CardGeom c;
    if (!gr.isCard() || g.edit.layer != 0) return c;   // only overlays are picked or outlined in the scene
    for (int i = 0; i < (int)gr.params.size(); i++) {
        const EditParam& p = gr.params[i];
        if (p.id == "Yaw") { c.panRow = i; c.pan = (float)p.value; }
        else if (p.id == "Pitch") { c.tiltRow = i; c.tilt = (float)p.value; }
        else if (p.id == "Distance") c.dist = std::max(0.3f, std::min(6.0f, (float)std::fabs(p.value)));
        else if (p.id == "@size" || p.id == "Size") c.scale = std::max(0.1f, (float)p.value);
    }
    c.valid = c.panRow >= 0 && c.tiltRow >= 0;
    return c;
}
static XrVector3f cardDir(float panDeg, float tiltDeg) {
    const float deg = (float)M_PI / 180;
    XrQuaternionf o = qMul(qMul(qYaw(g.settings.yawDeg * deg), qYaw(panDeg * deg)), qPitch(tiltDeg * deg));
    return qRotate(o, {0, 0, -1});
}
// Ray direction -> pan/tilt in the video's frame (inverse of cardDir).
static void rayAngles(const XrVector3f& d, float& panDeg, float& tiltDeg) {
    const float deg = (float)M_PI / 180;
    XrVector3f v = qRotate(qYaw(-g.settings.yawDeg * deg), d);
    panDeg = std::atan2(-v.x, -v.z) / deg;
    tiltDeg = std::asin(std::max(-1.0f, std::min(1.0f, v.y))) / deg;
}
// Half-angle a card covers as seen from the viewer (the plane is `scale` units wide at `dist`), with a floor so
// small distant cards can still be picked.
static float cardHalfAngle(const CardGeom& c) {
    return std::max(5.0f * (float)M_PI / 180, std::atan(0.55f * c.scale / c.dist));
}
// Monitor mode: the picture comes from an NDI source (Premiere, After Effects...) or the Resolve bridge is not
// there, so there is nothing to edit; only the View panel shows and the scene is not pickable.
static bool monitorMode() {
    std::string n = g.net.serverName();
    bool ndi = n.rfind("NDI", 0) == 0;
    return ndi || !g.edit.bridgeOk;
}

static int pickCard(const XrPosef& ray) {
    if (monitorMode()) return -1;
    XrVector3f d = qRotate(ray.orientation, {0, 0, -1});
    int best = -1;
    float bestAng = 1e9f;
    for (int i = 0; i < (int)g.edit.groups.size(); i++) {
        CardGeom c = cardGeom(g.edit.groups[i]);
        if (!c.valid) continue;
        XrVector3f cd = cardDir(c.pan, c.tilt);
        float dot = std::max(-1.0f, std::min(1.0f, d.x * cd.x + d.y * cd.y + d.z * cd.z));
        float ang = std::acos(dot);
        if (ang < cardHalfAngle(c) * 1.15f && ang < bestAng) {
            bestAng = ang;
            best = i;
        }
    }
    return best;
}

static void updatePointers(XrTime time) {
    // controllers
    XrActiveActionSet aas{g.actionSet, XR_NULL_PATH};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
    si.countActiveActionSets = 1;
    si.activeActionSets = &aas;
    bool synced = XR_SUCCEEDED(xrSyncActions(g.session, &si));
    g.handsSeen = false;
    for (int h = 0; h < 2; h++) {
        Pointer& p = g.pointers[h];
        p.wasPressed = p.pressed;
        p.valid = false;
        p.onPanel = false;
        // 1) hand tracking with the system aim ray and index pinch
        if (g.handTracker[h] != XR_NULL_HANDLE) {
            XrHandTrackingAimStateFB aim{XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
            XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT];
            XrHandJointLocationsEXT locs{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
            locs.next = g.extAim ? &aim : nullptr;
            locs.jointCount = XR_HAND_JOINT_COUNT_EXT;
            locs.jointLocations = joints;
            XrHandJointsLocateInfoEXT li{XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT};
            li.baseSpace = g.localSpace;
            li.time = time;
            if (XR_SUCCEEDED(g.locateHandJoints(g.handTracker[h], &li, &locs)) && locs.isActive && g.extAim &&
                (aim.status & XR_HAND_TRACKING_AIM_VALID_BIT_FB)) {
                p.valid = true;
                p.isHand = true;
                p.aim = aim.aimPose;
                p.pressed = (aim.status & XR_HAND_TRACKING_AIM_INDEX_PINCHING_BIT_FB) != 0;
                g.handsSeen = true;
            }
        }
        // 2) controller aim pose and trigger
        if (!p.valid && synced) {
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action = g.actAim;
            gi.subactionPath = g.handPath[h];
            XrActionStatePose ps{XR_TYPE_ACTION_STATE_POSE};
            if (XR_SUCCEEDED(xrGetActionStatePose(g.session, &gi, &ps)) && ps.isActive) {
                XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
                if (XR_SUCCEEDED(xrLocateSpace(g.aimSpace[h], g.localSpace, time, &loc)) &&
                    (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) &&
                    (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
                    p.valid = true;
                    p.isHand = false;
                    p.aim = loc.pose;
                    gi.action = g.actSelect;
                    XrActionStateFloat fs{XR_TYPE_ACTION_STATE_FLOAT};
                    if (XR_SUCCEEDED(xrGetActionStateFloat(g.session, &gi, &fs)) && fs.isActive)
                        p.pressed = p.pressed ? fs.currentState > 0.4f : fs.currentState > 0.6f;
                    else
                        p.pressed = false;
                }
            }
        }
        // test hook: a ray aimed from the head at a point on a panel (cmd.txt "point <panel> <u> <v> [press]")
        if (h == 0 && g.debugAimOn) {
            p.valid = true;
            p.isHand = true;
            p.aim = g.debugAim;
            p.pressed = g.debugAimPressed;
        }
        if (!p.valid) {
            if (isDragButton(p.pressStart)) drag(p, true);
            if (p.sceneDrag >= 0) {
                p.sceneDrag = -1;
                g.edit.dragRow = -1;
            }
            p.pressed = false;
            p.hover = HudButton::None;
            p.pressStart = HudButton::None;
            p.pressPanel = -1;
            p.panel = -1;
            continue;
        }
        HudButton under = HudButton::None;
        int underArg = 0;
        p.panel = hitPanels(p.aim, p.hitU, p.hitV, under, underArg);
        p.onPanel = p.panel >= 0;
        // while pressing, keep tracking the plane of the panel the press began on, even outside it
        p.onPlane = p.onPanel;
        if (p.pressed && p.pressPanel >= 0 && p.pressPanel != p.panel) {
            float u, v, t;
            bool inside;
            if (hitOnePanel(g.panels[p.pressPanel], (PanelId)p.pressPanel, p.aim, u, v, inside, t)) {
                p.hitU = u;
                p.hitV = v;
                p.onPlane = true;
            }
        }
        p.hover = under;
        p.hoverArg = underArg;
        // scene: a ray that misses every panel can point at a card
        p.sceneHover = (!p.onPanel && !p.pressed) ? pickCard(p.aim) : (p.pressed ? p.sceneHover : -1);
        if (p.pressed && !p.wasPressed && !p.onPanel && p.sceneHover >= 0) {
            p.sceneDrag = p.sceneHover;
            if (g.edit.group != p.sceneDrag) {
                g.edit.group = p.sceneDrag;
                g.edit.firstRow = 0;
                g.hudForce = true;
            }
            CardGeom c = cardGeom(g.edit.groups[p.sceneDrag]);
            p.dragPan0 = c.pan;
            p.dragTilt0 = c.tilt;
            rayAngles(qRotate(p.aim.orientation, {0, 0, -1}), p.rayPan0, p.rayTilt0);
            p.pressStart = HudButton::None;
            p.pressPanel = -1;
            p.wasPressed = true;   // consume the press so the panel path ignores it
            LOGI("%s pick card %d (%s)", p.isHand ? "pinch" : "trigger", p.sceneDrag, g.edit.groups[p.sceneDrag].clipName.c_str());
        }
        if (p.sceneDrag >= 0) {
            if (p.sceneDrag < (int)g.edit.groups.size() && p.sceneDrag == g.edit.group) {
                CardGeom c = cardGeom(g.edit.groups[p.sceneDrag]);
                float rp, rt;
                rayAngles(qRotate(p.aim.orientation, {0, 0, -1}), rp, rt);
                float dp = rp - p.rayPan0, dt = rt - p.rayTilt0;
                while (dp > 180) dp -= 360;
                while (dp < -180) dp += 360;
                if (c.valid && (std::fabs(dp) > 0.3f || std::fabs(dt) > 0.3f || !p.pressed)) {
                    double pan = p.dragPan0 + dp, tilt = std::max(-89.0, std::min(89.0, (double)p.dragTilt0 + dt));
                    while (pan > 180) pan -= 360;
                    while (pan < -180) pan += 360;
                    setValue(c.panRow, std::round(pan * 10) / 10, !p.pressed);
                    setValue(c.tiltRow, std::round(tilt * 10) / 10, !p.pressed);
                }
            }
            if (!p.pressed) {
                p.sceneDrag = -1;
                g.edit.dragRow = -1;
            }
            continue;
        }
        if (p.pressed && !p.wasPressed) {
            p.pressStart = under;
            p.pressStartArg = underArg;
            p.pressPanel = p.panel;
            p.pressU = p.hitU;
            p.pressV = p.hitV;
            p.maxMove = 0;
            if (under == HudButton::Dial) {
                EditParam* ep = editParam(underArg);
                p.pressValue = ep ? ep->value : 0;
            }
            if (under == HudButton::Move && p.panel >= 0) {
                // grab the panel where the ray hits it; it stays wherever it is let go
                App::Panel& pn = g.panels[p.panel];
                const PanelSpec& sp = panelSpec((PanelId)p.panel);
                float lx = p.hitU / sp.w * pn.w - pn.w / 2, ly = pn.h / 2 - p.hitV / sp.h * pn.h;
                p.grabLocal = {lx, ly, 0};
                XrVector3f hit = vAdd(pn.pose.position, qRotate(pn.pose.orientation, p.grabLocal));
                XrVector3f d = vSub(hit, p.aim.position);
                p.grabDist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                pn.placedPose = pn.pose;
                pn.placed = true;
            }
            LOGI("%s press on %d/%d panel %d (u %.0f v %.0f)", p.isHand ? "pinch" : "trigger", (int)under, underArg, p.panel,
                 p.hitU, p.hitV);
        }
        if (p.pressed) p.maxMove = std::max(p.maxMove, std::hypot(p.hitU - p.pressU, p.hitV - p.pressV));
        if (!p.pressed && p.wasPressed)
            LOGI("%s release on %d/%d (started %d/%d, moved %.0f)", p.isHand ? "pinch" : "trigger", (int)under, underArg,
                 (int)p.pressStart, p.pressStartArg, p.maxMove);
        if (p.pressed && p.pressStart == HudButton::Move && p.pressPanel >= 0 && g.headValid) {
            // keep the grabbed point on the ray at the same distance, and turn the panel to face the head
            App::Panel& pn = g.panels[p.pressPanel];
            XrVector3f dir = qRotate(p.aim.orientation, {0, 0, -1});
            XrVector3f target = vAdd(p.aim.position, vScale(dir, p.grabDist));
            XrVector3f v = vSub(target, g.head.position);
            float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            if (len > 0.2f) {
                float yaw = std::atan2(-v.x, -v.z), pitch = std::asin(std::max(-1.0f, std::min(1.0f, v.y / len)));
                XrQuaternionf o = qMul(qYaw(yaw), qPitch(pitch));
                pn.placedPose = {o, vSub(target, qRotate(o, p.grabLocal))};
                pn.pose = pn.placedPose;
            }
        }
        bool dragging = p.pressed && p.onPlane;
        if (dragging && isDragButton(p.pressStart)) drag(p, false);
        if (!p.pressed && p.wasPressed) {
            if (p.pressStart == HudButton::Move) {
                g.panelsPlaced = true;
                for (auto& pn : g.panels) g.panelsPlaced = g.panelsPlaced && pn.placed;
                savePanelLayout();
            } else if (p.pressStart == HudButton::Dial && p.maxMove <= 14 && p.pressStart == under && p.pressStartArg == underArg) {
                activate(under, underArg);
            } else if (isDragButton(p.pressStart)) {
                drag(p, true);
            } else if (p.pressStart != HudButton::None && p.pressStart == under && p.pressStartArg == underArg &&
                       p.pressPanel == p.panel) {
                g.hoverPanel = p.panel;
                activate(under, underArg);
            }
            p.pressStart = HudButton::None;
            p.pressPanel = -1;
        }
    }
    // controller buttons (kept for people who prefer them)
    if (synced) {
        bool changed = false;
        if (pressed(g.actToggleStrip)) {
            g.settings.strip = !g.settings.strip;
            showNotice(g.settings.strip ? "Status bar on" : "Status bar off");
            changed = true;
        }
        if (pressed(g.actSwapEyes)) {
            g.settings.swapEyes = !g.settings.swapEyes;
            showNotice(g.settings.swapEyes ? "Eyes swapped" : "Eyes normal");
            changed = true;
        }
        if (pressed(g.actMenu)) placeAll(!g.panelsPlaced);
        if (pressed(g.actDelayAuto)) {
            g.settings.delayMs = -1;
            showNotice("Delay: automatic");
            changed = true;
        }
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = g.actDelay;
        XrActionStateFloat f{XR_TYPE_ACTION_STATE_FLOAT};
        if (XR_SUCCEEDED(xrGetActionStateFloat(g.session, &gi, &f)) && f.isActive) {
            float y = f.currentState;
            if (std::fabs(y) > 0.7f && std::fabs(g.lastStickY) <= 0.7f) {
                nudgeDelay(y > 0 ? 10 : -10);
                changed = true;
            }
            g.lastStickY = y;
        }
        if (changed) {
            g.settings.save();
            g.hudForce = true;
        }
    }
    // the pointer closest to a button wins the highlight; a press in progress keeps its highlight
    g.hover = HudButton::None;
    g.pressed = HudButton::None;
    g.hoverArg = g.pressedArg = 0;
    for (auto& p : g.pointers) {
        if (p.hover != HudButton::None) {
            g.hover = p.hover;
            g.hoverArg = p.hoverArg;
        }
        if (p.pressed && p.pressStart != HudButton::None && (p.pressStart == p.hover || p.pressStart == HudButton::Slider)) {
            g.pressed = p.pressStart;
            g.pressedArg = p.pressStartArg;
        }
    }
}

// ------------------------------------------------------------------------------------------------ frames

static void retire(AImage* img) {
    if (img) g.retired.push_back({img, 2});   // GPU finishes with it within a couple of frames
}

static void syncReader() {
    uint32_t gen = 0;
    AImageReader* reader = g.decoder.reader(gen);
    if (gen != g.readerGen) {
        // new stream: drop everything tied to the old reader
        for (auto& h : g.held) AImage_delete(h.image);
        g.held.clear();
        if (g.current.image) AImage_delete(g.current.image);
        g.current = HeldFrame();
        for (auto& r : g.retired) AImage_delete(r.first);
        g.retired.clear();
        glFinish();
        g.gfx.dropImageCache();
        std::vector<AImageReader*> old;
        g.decoder.collectRetired(old);
        for (auto* r : old) AImageReader_delete(r);
        g.readerGen = gen;
        g.framesSincePlay = 0;   // (re)connect or new stream starts with a big keyframe: don't let it set the delay
        StreamConfig cfg = g.decoder.config();
        if (cfg.msg.configId != g.configId || g.video.width != (int)cfg.msg.width || g.video.height != (int)cfg.msg.height) {
            g.cfg = cfg;
            g.configId = cfg.msg.configId;
            createSwapchain(g.video, (int)cfg.msg.width, (int)cfg.msg.height);
            applyRefreshRate(cfg.msg.fpsDen ? (double)cfg.msg.fpsNum / cfg.msg.fpsDen : 0);
            if (g.setColorSpace)
                g.setColorSpace(g.session, cfg.msg.colorPrimaries == 9 ? XR_COLOR_SPACE_REC2020_FB : XR_COLOR_SPACE_REC709_FB);
        }
    }
    // age out retired images
    for (auto it = g.retired.begin(); it != g.retired.end();) {
        if (--it->second <= 0) {
            AImage_delete(it->first);
            it = g.retired.erase(it);
        } else
            ++it;
    }
    if (!reader) return;
    // pull every newly decoded image
    // Budget: reader has 6 images = up to 3 held + 1 on screen + up to 2 retiring.
    while ((int)g.held.size() < 3) {
        AImage* img = nullptr;
        if (AImageReader_acquireNextImage(reader, &img) != AMEDIA_OK || !img) break;
        int64_t ts = 0;
        AImage_getTimestamp(img, &ts);
        HeldFrame h;
        h.image = img;
        h.acquiredNs = monoNs();
        h.haveInfo = g.decoder.takeInfo((uint64_t)(ts / 1000), h.info);
        if (!h.haveInfo) {
            h.info.seq = (uint64_t)(ts / 1000);
            h.info.sync = true;   // unknown timing: show as soon as possible
        }
        if (g.lastSetNs && (h.info.sync || monoNs() - g.lastMotionNs > 400000000LL)) {
            // a parked re-render after a parameter change: measure the round trip
            g.lastRenderMs = (monoNs() - g.lastSetNs) / 1e6;
            LOGI("Resolve updated in %.0f ms", g.lastRenderMs);
            g.lastSetNs = 0;
            g.hudForce = true;
            flushPendingParam();   // the hand has moved on: send where it is now
        }
        if (!h.info.sync && h.haveInfo && g.net.clockValid()) {
            // Arrival lateness (compressed frame fully received vs its scan-out). Measured at arrival, not at
            // decode: decoding is deliberately held back until just before display.
            double late = (h.info.rxNs - g.net.senderToLocal(h.info.ptsSender)) / 1e6;
            if (g.framesSincePlay > 45) g.lateness.add(late);
        }
        g.held.push_back(h);
    }
    std::sort(g.held.begin(), g.held.end(), [](const HeldFrame& a, const HeldFrame& b) { return a.info.seq < b.info.seq; });
    // never go backwards
    while (!g.held.empty() && g.current.image && g.held.front().info.seq <= g.current.info.seq) {
        retire(g.held.front().image);
        g.held.erase(g.held.begin());
    }
}

static void updateAutoDelay(bool playing) {
    double refreshMs = 1000.0 / g.currentHz;
    if (g.lateness.count() < 24) return;
    int64_t now = monoNs();
    // Rolling = frames actually advancing. Resolve keeps its output clock running while parked, so the
    // playback flag alone can't tell a parked viewer from real playback.
    bool rolling = playing && !g.current.info.sync && now - g.lastMotionNs < 300000000LL;
    // Needed delay = worst-case arrival lateness + decode time + one refresh to get it into a frame, plus a small
    // margin: at p97 alone Wi-Fi jitter still dropped ~2% of frames.
    double want = std::max(0.0, g.lateness.percentile(98) + g.decoder.decodeMsP95() + refreshMs + 8.0);
    int64_t wantNs = (int64_t)(std::min(500.0, want) * 1e6);
    if (wantNs > g.autoDelayNs + 2000000) {
        LOGI("auto delay up: %.0f -> %.0f ms (p98 lateness %.1f ms)", g.autoDelayNs / 1e6, wantNs / 1e6, g.lateness.percentile(98));
        g.autoDelayNs = wantNs;
        g.shrinkSinceNs = 0;
    } else if (wantNs + 15000000 < g.autoDelayNs) {
        if (!rolling) {
            g.autoDelayNs = wantNs;   // nothing moving: settle straight away
            g.shrinkSinceNs = 0;
        } else {
            // A burst (keyframe over Wi-Fi, a second client joining) must not leave the delay high for the rest of
            // the session: once the window has been calm for 5 s, walk down 10 ms every 2 s. Each step can shorten
            // one frame by a refresh at most.
            if (!g.shrinkSinceNs) g.shrinkSinceNs = now;
            if (now - g.shrinkSinceNs > 5000000000LL && now - g.lastShrinkNs > 2000000000LL) {
                int64_t next = std::max(wantNs, g.autoDelayNs - 10000000);
                LOGI("auto delay down: %.0f -> %.0f ms", g.autoDelayNs / 1e6, next / 1e6);
                g.autoDelayNs = next;
                g.lastShrinkNs = now;
            }
        }
    } else {
        g.shrinkSinceNs = 0;
    }
    int ms = (int)(g.autoDelayNs / 1000000);
    if (!rolling && ms != g.settings.autoDelayMs) {   // remember the settled value for next time
        g.settings.autoDelayMs = ms;
        g.settings.save();
    }
}

static void chooseFrame(int64_t displayMono) {
    PlaybackState pb = g.net.playback();
    if (pb.generation != g.playbackGen) {
        g.playbackGen = pb.generation;
        g.framesSincePlay = 0;
    }
    // Resolve flushed frames scheduled after the stop point: never show them.
    if (!pb.playing && pb.stopPtsSender) {
        for (auto it = g.held.begin(); it != g.held.end();) {
            if (!it->info.sync && it->haveInfo && it->info.ptsSender > pb.stopPtsSender) {
                retire(it->image);
                it = g.held.erase(it);
            } else
                ++it;
        }
    }
    updateAutoDelay(pb.playing);
    int64_t delay = g.settings.delayMs >= 0 ? (int64_t)g.settings.delayMs * 1000000 : g.autoDelayNs;
    g.decoder.setTiming(delay, g.scheduler.refreshPeriod());
    bool clockOk = g.net.clockValid();
    std::vector<vrltime::ScheduledItem> items;
    for (auto& h : g.held) {
        vrltime::ScheduledItem it;
        it.seq = h.info.seq;
        it.durationNs = h.info.durationNs;
        it.sync = h.info.sync || !clockOk;
        it.dueNs = it.sync ? 0 : g.net.senderToLocal(h.info.ptsSender) + delay;
        items.push_back(it);
    }
    int64_t pendingPts = g.decoder.earliestPendingPts();
    int64_t pendingDue = (pendingPts == INT64_MAX || !clockOk) ? INT64_MAX : g.net.senderToLocal(pendingPts) + delay;
    int idx = g.scheduler.pick(items, displayMono, pendingDue);
    if (idx >= 0) {
        HeldFrame next = g.held[(size_t)idx];
        AHardwareBuffer* ahb = nullptr;
        if (AImage_getHardwareBuffer(next.image, &ahb) == AMEDIA_OK && ahb && g.video.handle) {
            Gfx::Fisheye fe;
            fe.layout = (int)effectiveLayout();
            fe.fovDeg = g.settings.fisheyeFov;
            const Gfx::Fisheye* fep = effectiveProjection() == Projection::Fisheye180 ? &fe : nullptr;
            renderInto(g.video, [&](GLuint tex) { g.gfx.blitVideo(ahb, tex, g.video.width, g.video.height, fep); });
        }
        for (int i = 0; i < idx; i++) retire(g.held[(size_t)i].image);   // superseded before display
        retire(g.current.image);
        g.current = next;
        g.held.erase(g.held.begin(), g.held.begin() + idx + 1);
        if (!next.info.sync && clockOk) g.photonOffset.add((displayMono - g.net.senderToLocal(next.info.ptsSender)) / 1e6);
        if (next.info.timecode != 0xFFFFFFFF) g.lastTimecode = next.info.timecode;
        if (!next.info.sync) g.lastMotionNs = monoNs();
        g.framesSincePlay++;
    }
    // Resolve keeps its output clock running while parked, so "playing" alone doesn't mean steady cadence:
    // parked/scrub frames have no cadence to keep.
    if (g.current.image) g.scheduler.onRefresh(g.current.info.seq, g.current.info.durationNs, pb.playing && !g.current.info.sync);
}

// ------------------------------------------------------------------------------------------------ HUD

static void locateHead(XrTime time) {
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    g.headValid = false;
    if (XR_SUCCEEDED(xrLocateSpace(g.viewSpace, g.localSpace, time, &loc)) &&
        (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) &&
        (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
        g.head = loc.pose;
        g.headValid = true;
        XrVector3f f = qRotate(g.head.orientation, {0, 0, -1});
        g.headYaw = std::atan2(-f.x, -f.z);
        g.headPitch = std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
    }
}

// The strip and the unpinned panel follow the head in yaw lazily: they stay put while the head is within ~25 deg
// of them and glide over once it turns further, so they neither swim with every head movement nor get lost.
static void followHead() {
    if (!g.headValid) return;
    if (!g.stripYawInit) {
        g.stripYaw = g.headYaw;
        g.stripYawInit = true;
    }
    float d = wrapAngle(g.headYaw - g.stripYaw);
    static bool gliding = false;
    if (std::fabs(d) > 25.0f * (float)M_PI / 180) gliding = true;
    if (gliding) {
        g.stripYaw = wrapAngle(g.stripYaw + d * 0.12f);
        if (std::fabs(d) < 1.0f * (float)M_PI / 180) gliding = false;
    }
}

static void updateHud(int64_t now, bool playing) {
    HudInfo& in = g.hudInfo;
    in.connected = g.net.connected();
    in.usb = g.net.viaUsb();
    in.hasVideo = g.decoder.hasConfig() && g.video.hasContent;
    in.width = (int)g.cfg.msg.width;
    in.height = (int)g.cfg.msg.height;
    const char* lay[] = {"mono", "SBS", "TB"};
    const char* proj[] = {"flat", "180\xC2\xB0", "360\xC2\xB0"};
    in.layout = lay[(int)effectiveLayout() % 3];
    in.projection = proj[(int)effectiveProjection() % 3];
    in.layoutAuto = g.settings.layout < 0;
    in.projectionAuto = g.settings.projection < 0;
    in.fps = g.cfg.msg.fpsDen ? (double)g.cfg.msg.fpsNum / g.cfg.msg.fpsDen : 0.0;
    in.mbps = g.net.receivedMbps();
    in.timecode = g.lastTimecode;
    in.play = !playing ? HudInfo::Stopped : (g.current.image && g.current.info.sync) ? HudInfo::Parked : HudInfo::Playing;
    in.delayMs = (int)((g.settings.delayMs >= 0 ? g.settings.delayMs * 1000000LL : g.autoDelayNs) / 1000000);
    in.delayAuto = g.settings.delayMs < 0;
    in.arriveMs = g.lateness.count() ? g.lateness.percentile(50) : 0.0;
    in.decodeMs = g.decoder.decodeMsP50();
    in.rttMs = g.net.rttNs() / 1e6;
    in.hz = g.currentHz;
    const auto& st = g.scheduler.stats();
    in.shown = st.framesShown;
    in.dropped = st.framesSkipped;
    in.uneven = st.cadenceErrors;
    in.swapEyes = g.settings.swapEyes;
    in.hudOn = g.settings.strip;
    in.pinned = g.panelsPlaced;
    in.grid = g.settings.grid;
    in.outline = g.settings.outline;
    in.source = g.settings.source;
    in.serverName = g.net.serverName();
    in.fisheyeFov = g.settings.fisheyeFov;
    // Resolve setup preflight: what the bridge found in the project settings, then what only this end can see
    // (the raster and frame rate the driver is actually fed, and whether a picture arrives at all)
    {
        std::string setup;
        bool resolve = in.serverName.rfind("NDI", 0) != 0;
        if (g.edit.bridgeOk && resolve) {
            if (!g.edit.warnings.empty()) setup = g.edit.warnings[0];
            else if (!g.decoder.hasConfig() && g.bridgeSinceNs && now - g.bridgeSinceNs > 8000000000LL)
                setup = "No picture: DaVinci Resolve > Preferences > Video and Audio I/O > Kalyx (Quest)";
            else if (g.decoder.hasConfig() && g.edit.haveTimeline) {
                int rw = (int)g.cfg.msg.rasterWidth, rh = (int)g.cfg.msg.rasterHeight;
                int ow = g.edit.fmtOutWidth, oh = g.edit.fmtOutHeight;
                char b[200];
                if (rw > 0 && ow > 0 && (rw < ow || rh < oh) && !(rw == 7680 && rh == 4320)) {
                    snprintf(b, sizeof b, "Monitoring %dx%d, timeline %dx%d: Project Settings > Video Monitoring > 7680 x 4320",
                             rw, rh, ow, oh);
                    setup = b;
                } else if (in.fps > 0 && g.edit.fps > 0 && std::fabs(in.fps - g.edit.fps) > 0.02) {
                    snprintf(b, sizeof b, "Monitoring at %.3g fps, timeline %.3g: Project Settings > Video Monitoring > %.3g fps", in.fps, g.edit.fps, g.edit.fps);
                    setup = b;
                }
            }
        }
        if (setup != in.setup) {
            if (!setup.empty()) LOGI("setup: %s", setup.c_str());
            in.setup = setup;
            g.hudForce = true;
        }
    }
    in.hands = g.handsSeen;
    in.notice = now < g.noticeUntil ? g.notice : "";
    in.rendering = g.lastSetNs != 0 && now - g.lastSetNs < 15000000000LL;
    in.renderWaitMs = in.rendering ? (now - g.lastSetNs) / 1e6 : 0;
    in.renderMs = g.lastRenderMs;
    // scopes: wanted while the Scopes panel is docked on (the Mac measures nothing otherwise)
    {
        bool want = g.settings.panelOn[(int)PanelId::Scopes] && true;
        g.net.wantScopes(want, 10);
        if (want) {
            ScopesFrame sf;
            if (g.net.scopes(sf, in.scopesSeq)) {
                in.scopes = std::make_shared<const std::vector<uint8_t>>(std::move(sf.raster));
                in.scopesSeq = sf.seq;
                in.scopesTimecode = sf.timecodeBCD;
                g.scopesReceivedNs = sf.receivedNs;
            }
            in.scopesAgeMs = g.scopesReceivedNs ? (now - g.scopesReceivedNs) / 1e6 : 0;
        } else if (in.scopes) {
            in.scopes.reset();
            in.scopesSeq = 0;
        }
        in.scopesView = g.settings.scopesView;
    }
    // dropping frames lately?
    if (now - g.dropWindowNs > 5000000000LL) {
        uint64_t d = st.framesSkipped - g.droppedAtWindow;
        in.warning = (d > 8 && playing) ? (in.usb ? "Dropping frames" : "Dropping frames \xE2\x80\x93 try the USB cable") : "";
        g.droppedAtWindow = st.framesSkipped;
        g.dropWindowNs = now;
    }
    // bridge: follow the video link's host, drain its messages into the edit model
    {
        std::string host = g.net.connected() ? (g.net.viaUsb() ? "127.0.0.1" : g.net.host()) : "";
        if (host == "USB") host = "127.0.0.1";
        g.bridge.setHost(host);
        bool ok = g.bridge.connected();
        if (ok != g.edit.bridgeOk) {
            g.edit.bridgeOk = ok;
            g.bridgeSinceNs = ok ? now : 0;
            g.hudForce = true;
        }
        Json msg;
        while (g.bridge.pop(msg)) {
            std::string type = msg.get("type").stringOr("");
            if (type == "notice" || type == "error") LOGI("bridge %s: %s", type.c_str(), msg.get("msg").stringOr("").c_str());
            if (g.edit.apply(msg)) g.hudForce = true;
        }
    }
    // hover/press: whichever pointer is on a panel (pressing wins)
    g.hover = HudButton::None;
    g.pressed = HudButton::None;
    g.hoverPanel = g.pressedPanel = -1;
    for (auto& p : g.pointers) {
        if (!p.valid) continue;
        if (p.pressed && p.pressStart != HudButton::None) {
            g.pressed = p.pressStart;
            g.pressedArg = p.pressStartArg;
            g.pressedPanel = p.pressPanel;
        }
        if (p.onPanel && p.hover != HudButton::None && (g.hover == HudButton::None || p.pressed)) {
            g.hover = p.hover;
            g.hoverArg = p.hoverArg;
            g.hoverPanel = p.panel;
        }
    }
    UiState s;
    s.info = in;
    s.lookTab = g.lookTab;
    for (int i = 0; i < kPanelCount; i++) s.enabled[i] = g.settings.panelOn[i];
    s.hover = g.hover;
    s.hoverArg = g.hoverArg;
    s.hoverPanel = g.hoverPanel;
    s.pressed = g.pressed;
    s.pressedArg = g.pressedArg;
    s.pressedPanel = g.pressedPanel;
    bool anyVisible = false;
    for (int i = 0; i < kPanelCount; i++) {
        s.visible[i] = (g.panels[i].visible || g.panelOpen || g.panels[i].placed) && (g.settings.panelOn[i] || i == 0);
        s.rolled[i] = g.panels[i].rolled;
        anyVisible = anyVisible || s.visible[i];
    }
    char ik[128];
    snprintf(ik, sizeof ik, "%d.%d.%d|%d.%d.%d|%d|%d%d%d%d|%d%d%d%d", (int)s.hover, s.hoverArg, s.hoverPanel, (int)s.pressed,
             s.pressedArg, s.pressedPanel, s.lookTab, s.visible[0], s.visible[1], s.visible[2], s.visible[3], s.rolled[0],
             s.rolled[1], s.rolled[2], s.rolled[3]);
    // Highlights follow the pointer immediately; the numbers refresh a few times a second (panels take tens of
    // milliseconds to draw on the worker thread).
    bool interact = ik != g.lastUiKey;
    if (g.hudForce || interact || now - g.lastHudUpdate > (anyVisible ? 150000000LL : 250000000LL)) {
        if (monitorMode()) {
            // the Resolve bridge may still be up (Resolve open behind Premiere): show no controls for it
            EditModel m = g.edit;
            m.groups.clear();
            m.group = 0;
            s.lookTab = 0;
            s.edit = std::make_shared<const EditModel>(m);
        } else
            s.edit = std::make_shared<const EditModel>(g.edit);
        g.hud.update(s);
        g.lastHudUpdate = now;
        g.lastUiKey = ik;
        g.hudForce = false;
    }
    std::vector<uint8_t> px;
    if (g.hud.takeStrip(px)) renderInto(g.strip, [&](GLuint tex) { g.gfx.upload(tex, px.data(), kStripW, kStripH); });
    for (int i = 0; i < kPanelCount; i++) {
        std::vector<ButtonRect> rects;
        if (g.hud.takePanel((PanelId)i, px, rects)) {
            const PanelSpec& sp = panelSpec((PanelId)i);
            renderInto(g.panels[i].sc, [&](GLuint tex) { g.gfx.upload(tex, px.data(), sp.w, sp.h); });
            g.panels[i].buttons = rects;
        }
    }
}

static void sendStats(int64_t now) {
    if (now - g.lastStatsSent < 1000000000LL) return;
    g.lastStatsSent = now;
    const auto& st = g.scheduler.stats();
    StatsMsg s{};
    s.framesShown = (uint32_t)st.framesShown;
    s.framesSkipped = (uint32_t)st.framesSkipped;
    s.cadenceErrors = (uint32_t)st.cadenceErrors;
    s.holds = (uint32_t)st.holds;
    s.playoutDelayMs = (int32_t)((g.settings.delayMs >= 0 ? g.settings.delayMs * 1000000LL : g.autoDelayNs) / 1000000);
    s.latencyP50Ms = (int32_t)g.photonOffset.percentile(50);
    s.latencyP95Ms = (int32_t)g.photonOffset.percentile(95);
    s.clockRttUs = (int32_t)(g.net.rttNs() / 1000);
    s.refreshHz = g.currentHz;
    s.decodeMsP50 = (float)g.decoder.decodeMsP50();
    s.bitrateKbps = (uint32_t)(g.net.receivedMbps() * 1000);
    g.net.sendStats(s);
}

// ------------------------------------------------------------------------------------------------ frame loop

// Test hook: `adb shell "echo 'press TabEdit' > /sdcard/Android/data/ca.skylightproductions.vrlink/files/cmd.txt"`
// drives the UI without hands. Commands: press <Button> [arg], slider <row> <0..1>, scrub <0..1>, page edit|preview.
static void pollDebugCommands(int64_t now) {
    if (now - g.lastDebugPollNs < 500000000LL || !g.android->activity->externalDataPath) return;
    g.lastDebugPollNs = now;
    std::string path = std::string(g.android->activity->externalDataPath) + "/cmd.txt";
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(f, line))
        if (!line.empty()) lines.push_back(line);
    f.close();
    unlink(path.c_str());
    static const std::map<std::string, HudButton> names = {
        {"Hud", HudButton::Hud}, {"SwapEyes", HudButton::SwapEyes}, {"Layout", HudButton::Layout},
        {"Projection", HudButton::Projection}, {"DelayDown", HudButton::DelayDown}, {"DelayAuto", HudButton::DelayAuto},
        {"DelayUp", HudButton::DelayUp}, {"ResetView", HudButton::ResetView}, {"PinAll", HudButton::PinAll},
        {"Pin", HudButton::PinAll}, {"Tab", HudButton::Tab}, {"ClipPrev", HudButton::ClipPrev}, {"ClipNext", HudButton::ClipNext},
        {"StepBack", HudButton::StepBack}, {"StepFwd", HudButton::StepFwd}, {"Undo", HudButton::Undo},
        {"Refresh", HudButton::Refresh}, {"GroupPrev", HudButton::GroupPrev}, {"GroupNext", HudButton::GroupNext},
        {"PageUp", HudButton::PageUp}, {"PageDown", HudButton::PageDown}, {"Toggle", HudButton::Toggle},
        {"NudgeDown", HudButton::NudgeDown}, {"NudgeUp", HudButton::NudgeUp}, {"Key", HudButton::Key},
        {"Zoom", HudButton::Zoom}, {"Copy", HudButton::Copy}, {"Paste", HudButton::Paste}, {"PasteAll", HudButton::PasteAll},
        {"Dial", HudButton::Dial}, {"Grid", HudButton::Grid}, {"Outline", HudButton::Outline}, {"Source", HudButton::Source}, {"FovDown", HudButton::FovDown}, {"FovUp", HudButton::FovUp}, {"PanelToggle", HudButton::PanelToggle}, {"Layer", HudButton::Layer}, {"Lock", HudButton::Lock}, {"GroupPick", HudButton::GroupPick}, {"ScopesView", HudButton::ScopesView}, {"Jump", HudButton::Jump}, {"FovPreset", HudButton::FovPreset}};
    for (auto& l : lines) {
        std::istringstream ss(l);
        std::string cmd, a, b;
        ss >> cmd >> a >> b;
        LOGI("debug command: %s", l.c_str());
        if (cmd == "press") {
            auto it = names.find(a);
            if (it != names.end()) activate(it->second, b.empty() ? 0 : atoi(b.c_str()));
        } else if (cmd == "slider") {
            const ButtonRect* r = findRect(HudButton::Slider, atoi(a.c_str()));
            if (r) dragSlider(atoi(a.c_str()), r->x + 24 + (r->w - 48) * (float)atof(b.c_str()), true);
        } else if (cmd == "throttle") {
            const ButtonRect* r = findRect(HudButton::Throttle, atoi(a.c_str()));
            if (r) dragThrottle(atoi(a.c_str()), r->y + r->h - 26 - (r->h - 52) * (float)atof(b.c_str()), true);
        } else if (cmd == "ring") {   // ring <row> <yaw degrees>
            const ButtonRect* r = findRect(HudButton::Ring, atoi(a.c_str()));
            if (r) {
                float yaw = (float)atof(b.c_str()) * (float)M_PI / 180, cx = r->x + r->w / 2, cy = r->y + r->h / 2;
                dragRing(atoi(a.c_str()), cx - std::sin(yaw) * 100, cy - std::cos(yaw) * 100, true);
            }
        } else if (cmd == "scrub") {
            const ButtonRect* r = findRect(HudButton::Scrub, 0);
            if (r) scrubTo(r->x + r->w * (float)atof(a.c_str()), true);
        } else if (cmd == "roll") {
            int i = atoi(a.c_str());
            if (i >= 0 && i < kPanelCount) {
                g.panels[i].rolled = !g.panels[i].rolled;
                g.hudForce = true;
            }
        } else if (cmd == "tab") {
            activate(HudButton::Tab, atoi(a.c_str()));
        } else if (cmd == "open") {
            g.panelOpen = true;
            g.cockpitValid = false;
        } else if (cmd == "close") {
            g.panelOpen = false;
        } else if (cmd == "point") {   // point <panel> <u> <v> [press]: aim a fake hand at panel texels u,v
            int i = atoi(a.c_str());
            std::string pr;
            ss >> pr;
            if (i >= 0 && i < kPanelCount) {
                App::Panel& pn = g.panels[i];
                const PanelSpec& sp = panelSpec((PanelId)i);
                XrPosef pose;
                float hM;
                int texH;
                panelQuadGeom(pn, (PanelId)i, pose, hM, texH);
                XrVector3f local{((float)atof(b.c_str()) / sp.w - 0.5f) * pn.w, (0.5f - (float)atof(pr.c_str()) / texH) * hM, 0};
                std::string press;
                ss >> press;
                XrVector3f target = vAdd(pose.position, qRotate(pose.orientation, local));
                XrVector3f d = vSub(target, g.head.position);
                float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                if (len > 1e-3f) {
                    d = vScale(d, 1.0f / len);
                    float yaw = std::atan2(-d.x, -d.z), pitch = std::asin(std::max(-1.0f, std::min(1.0f, d.y)));
                    g.debugAim = {qMul(qYaw(yaw), qPitch(pitch)), g.head.position};
                    g.debugAimPressed = press == "press";
                    g.debugAimOn = true;
                }
            } else {
                g.debugAimOn = false;
            }
        } else if (cmd == "quit") {   // same path as the Quest menu's quit
            g.exitRequested = true;
        } else if (cmd == "status") {
            LOGI("status: tab %d placed %d open %d pitch %.1f buttons %zu/%zu/%zu/%zu groups %zu bridge %d", g.lookTab,
                 g.panelsPlaced, g.panelOpen, g.headPitch * 180 / (float)M_PI, g.panels[0].buttons.size(),
                 g.panels[1].buttons.size(), g.panels[2].buttons.size(), g.panels[3].buttons.size(), g.edit.groups.size(),
                 g.edit.bridgeOk);
        }
    }
}

static void renderFrame() {
    XrFrameWaitInfo fwi{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState fs{XR_TYPE_FRAME_STATE};
    if (XR_FAILED(xrWaitFrame(g.session, &fwi, &fs))) {
        usleep(5000);   // never spin on a runtime that is not handing out frames
        return;
    }
    XrFrameBeginInfo fbi{XR_TYPE_FRAME_BEGIN_INFO};
    XRC(xrBeginFrame(g.session, &fbi));
    int64_t displayMono = xrToMono(fs.predictedDisplayTime);
    if (fs.predictedDisplayPeriod > 0) g.scheduler.setRefreshPeriod(fs.predictedDisplayPeriod);

    locateHead(fs.predictedDisplayTime);
    followHead();
    // look down to open the panels, look up past the horizon to close them (hysteresis); placed panels stay
    const float deg = (float)M_PI / 180;
    if (g.headValid) {
        bool firstAnchor = !g.cockpitValid;
        if (!g.panelOpen && g.headPitch < -22 * deg) {
            g.panelOpen = true;
            g.cockpitPos = g.head.position;
            g.cockpitYaw = g.headYaw;
            g.cockpitValid = true;
        }
        if (g.panelOpen && g.headPitch > 8 * deg) g.panelOpen = false;
        if (!g.cockpitValid) {
            g.cockpitPos = g.head.position;
            g.cockpitYaw = g.headYaw;
            g.cockpitValid = true;
        }
        if (firstAnchor && g.cockpitValid) restorePanelLayout();   // last session's layout, around where you are now
    }
    // default cockpit around the anchor; a placed panel keeps its own pose
    struct Slot {
        float yawDeg, pitchDeg, dist;
    };
    static const Slot slots[kPanelCount] = {
        {0, -8, 1.55f},      // Instance strip: centre, just under the picture, furthest back
        {48, -18, 1.20f},    // Placement: right (positive yaw = left in XR, so negate below)
        {-48, -18, 1.20f},   // Look: left
        {0, -54, 1.40f},     // Deck: low centre
        {0, -30, 1.50f},     // Scopes: between the strip and the deck, so it is there whenever the cockpit is
    };
    // Monitor mode: an NDI source (Premiere, After Effects...) or no Resolve bridge means there is nothing to
    // edit; only the View panel shows, in the centre, so the headset reads as a clean monitor.
    bool monitor = monitorMode();
    for (int i = 0; i < kPanelCount; i++) {
        App::Panel& pn = g.panels[i];
        Slot s = slots[i];
        if (monitor && i == (int)PanelId::Look) s = {0, -18, 1.20f};
        if (pn.placed && !monitor) pn.pose = pn.placedPose;
        else if (g.cockpitValid) pn.pose = placeQuad(g.cockpitPos, g.cockpitYaw - s.yawDeg * deg, s.pitchDeg * deg, s.dist);
        bool monitorOk = i == (int)PanelId::Look || (i == (int)PanelId::Scopes && g.settings.panelOn[i]);
        pn.visible = (pn.placed || g.panelOpen) && pn.sc.hasContent && (!monitor || monitorOk) && (g.settings.panelOn[i] || (monitor && i == (int)PanelId::Look));
    }
    updatePointers(fs.predictedDisplayTime);
    syncReader();
    flushPendingParam();   // timeout path: a render that never showed up must not hold a drag hostage
    chooseFrame(displayMono);
    int64_t now = monoNs();
    pollDebugCommands(now);
    PlaybackState pb = g.net.playback();
    updateHud(now, pb.playing);
    sendStats(now);
    for (int i = 0; i < kPanelCount; i++)
        g.panels[i].visible = (g.panels[i].placed || g.panelOpen) && g.panels[i].sc.hasContent &&
                              (!monitor || i == (int)PanelId::Look || (i == (int)PanelId::Scopes && g.settings.panelOn[i])) &&
                              (g.settings.panelOn[i] || (monitor && i == (int)PanelId::Look));

    std::vector<XrCompositionLayerBaseHeader*> layers;
    XrCompositionLayerEquirect2KHR eq[2];
    XrCompositionLayerQuad stripQuad{XR_TYPE_COMPOSITION_LAYER_QUAD}, panelQuad[kPanelCount],
        cursorQuad[2] = {{XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD}},
        liftQuad[2] = {{XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD}};
    if (fs.shouldRender && g.video.hasContent) {
        Layout L = effectiveLayout();
        Projection P = effectiveProjection();
        int w = g.video.width, h = g.video.height;
        // GL swapchain images are bottom-up: the picture's top half (left eye in TB) is at y = h/2.
        XrRect2Di full{{0, 0}, {w, h}};
        XrRect2Di left = full, right = full;
        if (L == Layout::SideBySide) {
            left = {{0, 0}, {w / 2, h}};
            right = {{w / 2, 0}, {w / 2, h}};
        } else if (L == Layout::TopBottom) {
            left = {{0, h / 2}, {w, h / 2}};
            right = {{0, 0}, {w, h / 2}};
        }
        if (g.settings.swapEyes) std::swap(left, right);
        float hAngle = P == Projection::Equirect360 ? 2.0f * (float)M_PI : (float)M_PI;
        float vHalf = (float)M_PI / 2;
        if (P == Projection::Flat) {   // show flat content as a 90 deg x (90/aspect) deg window
            hAngle = (float)M_PI / 2;
            double eyeAspect = (double)left.extent.width / left.extent.height;
            vHalf = (float)(std::atan(std::tan(hAngle / 2) / eyeAspect));
        }
        bool stereo = L != Layout::Mono;
        for (int e = 0; e < (stereo ? 2 : 1); e++) {
            eq[e] = {XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR};
            eq[e].layerFlags = 0;
            eq[e].space = g.localSpace;
            eq[e].eyeVisibility = stereo ? (e == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT) : XR_EYE_VISIBILITY_BOTH;
            eq[e].subImage.swapchain = g.video.handle;
            eq[e].subImage.imageRect = e == 0 ? left : right;
            eq[e].subImage.imageArrayIndex = 0;
            eq[e].pose.orientation = qYaw(g.settings.yawDeg * deg);   // recentre offset
            eq[e].radius = 0;   // infinite sphere: no parallax from head translation
            eq[e].centralHorizontalAngle = hAngle;
            eq[e].upperVerticalAngle = vHalf;
            eq[e].lowerVerticalAngle = -vHalf;
            layers.push_back((XrCompositionLayerBaseHeader*)&eq[e]);
        }
    }
    // Level grid: a world-level equirect sphere drawn over the picture (same recentre yaw, so its 0 deg tick is
    // the footage's centre). The horizon line is true level in the room; a wonky horizon in the footage shows
    // against it, and the PanoMap dials bring the two into line.
    XrCompositionLayerEquirect2KHR gridLayer{XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR};
    if (fs.shouldRender && g.video.hasContent && g.settings.grid && g.grid.hasContent) {
        gridLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        gridLayer.space = g.localSpace;
        gridLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        gridLayer.subImage.swapchain = g.grid.handle;
        gridLayer.subImage.imageRect = {{0, 0}, {kGridW, kGridH}};
        gridLayer.subImage.imageArrayIndex = 0;
        gridLayer.pose.orientation = qYaw(g.settings.yawDeg * deg);
        gridLayer.radius = 0;
        gridLayer.centralHorizontalAngle = 2.0f * (float)M_PI;
        gridLayer.upperVerticalAngle = (float)M_PI / 2;
        gridLayer.lowerVerticalAngle = -(float)M_PI / 2;
        layers.push_back((XrCompositionLayerBaseHeader*)&gridLayer);
    }
    bool showStrip = (g.settings.strip || !g.video.hasContent) && g.strip.hasContent && g.headValid;
    if (fs.shouldRender && showStrip) {
        stripQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        stripQuad.space = g.localSpace;
        stripQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        stripQuad.subImage.swapchain = g.strip.handle;
        stripQuad.subImage.imageRect = {{0, 0}, {kStripW, kStripH}};
        // just below the horizon: out of the way, a glance down reads it
        stripQuad.pose = placeQuad(g.head.position, g.stripYaw, -21 * deg, 1.6f);
        stripQuad.size = {1.3f, 1.3f * kStripH / kStripW};
        layers.push_back((XrCompositionLayerBaseHeader*)&stripQuad);
    }
    int nc = 0;
    for (int i = 0; i < kPanelCount && fs.shouldRender; i++) {
        App::Panel& pn = g.panels[i];
        if (!pn.visible) continue;
        const PanelSpec& sp = panelSpec((PanelId)i);
        XrCompositionLayerQuad& q = panelQuad[i];
        q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        q.space = g.localSpace;
        q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        XrPosef qpose;
        float hM;
        int texH;
        panelQuadGeom(pn, (PanelId)i, qpose, hM, texH);
        q.subImage.swapchain = pn.sc.handle;
        q.subImage.imageRect = {{0, sp.h - texH}, {sp.w, texH}};   // GL rows are bottom-up; the canvas top is at sp.h
        q.pose = qpose;
        q.size = {pn.w, hM};
        layers.push_back((XrCompositionLayerBaseHeader*)&q);
        for (auto& p : g.pointers) {
            if (!p.valid || nc >= 2) continue;
            int on = p.pressed && p.pressPanel >= 0 ? p.pressPanel : p.panel;
            if (on != i) continue;
            // stereo elevation: the control under the pointer floats 2 mm off the glass, 4 mm while dragged
            HudButton lb = p.pressed && p.pressStart != HudButton::None ? p.pressStart : p.hover;
            int la = p.pressed && p.pressStart != HudButton::None ? p.pressStartArg : p.hoverArg;
            if (liftable(lb)) {
                for (auto& r : pn.buttons) {
                    if (r.id != lb || r.arg != la) continue;
                    XrCompositionLayerQuad& lq = liftQuad[nc];
                    lq.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                    lq.space = g.localSpace;
                    lq.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                    lq.subImage.swapchain = g.lift.handle;
                    lq.subImage.imageRect = {{0, 0}, {kLiftW, kLiftH}};
                    float mx = pn.w / sp.w;   // metres per texel
                    float cxp = r.x + r.w / 2, cyp = r.y + r.h / 2;
                    float lift = p.pressed ? 0.004f : 0.002f;
                    XrVector3f local{(cxp / sp.w - 0.5f) * pn.w, (0.5f - cyp / texH) * hM, lift};
                    lq.pose.orientation = qpose.orientation;
                    lq.pose.position = vAdd(qpose.position, qRotate(qpose.orientation, local));
                    // the lift texture holds a box with the control's aspect (re-drawn when it changes), so the
                    // quad scales uniformly and the corners keep their radius on wide chips
                    // 4 px of margin around the control (v3 chips are tight, radius 14), drawn as large as the texture
                    // allows so the sheen and corners stay crisp on wide chips
                    float rw = r.w + 8, rh = r.h + 8;
                    const float boxMax = kLiftW - 16;
                    float boxW = rw >= rh ? boxMax : boxMax * rw / rh, boxH = rw >= rh ? boxMax * rh / rw : boxMax;
                    bool round = lb == HudButton::Dial || lb == HudButton::Ring || lb == HudButton::Arc || lb == HudButton::Wheel;
                    float texPerPx = boxW / rw;
                    float rad = round ? std::min(boxW, boxH) / 2 : 18.0f * texPerPx;
                    char lk[48];
                    snprintf(lk, sizeof lk, "%.0f,%.0f,%.0f", boxW, boxH, rad);
                    if (g.liftKey != lk) {
                        Canvas lc;
                        drawLift(lc, boxW, boxH, rad, texPerPx);
                        std::vector<uint8_t> lpx;
                        lc.encode(lpx);
                        renderInto(g.lift, [&](GLuint tex) { g.gfx.upload(tex, lpx.data(), kLiftW, kLiftH); });
                        g.liftKey = lk;
                    }
                    lq.size = {rw * mx * kLiftW / boxW, rh * mx * kLiftH / boxH};
                    layers.push_back((XrCompositionLayerBaseHeader*)&lq);
                    break;
                }
            }
            XrCompositionLayerQuad& cq = cursorQuad[nc++];
            cq.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            cq.space = g.localSpace;
            cq.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            cq.subImage.swapchain = g.cursor.handle;
            cq.subImage.imageRect = {{p.pressed ? 64 : 0, 0}, {64, 64}};
            // hit point in panel space -> local space, lifted 3 mm off the panel
            XrVector3f local{(p.hitU / sp.w - 0.5f) * pn.w, (0.5f - p.hitV / texH) * hM, 0.003f};
            cq.pose.orientation = qpose.orientation;
            cq.pose.position = vAdd(qpose.position, qRotate(qpose.orientation, local));
            cq.size = {0.028f, 0.028f};
            layers.push_back((XrCompositionLayerBaseHeader*)&cq);
        }
    }
    // card outlines in the scene: the selected card always, a pointed-at card while hovering
    XrCompositionLayerQuad cardQuad[2];
    int ncq = 0;
    if (fs.shouldRender && g.video.hasContent && g.headValid && g.settings.outline && g.outline.hasContent) {
        int show[2] = {g.edit.group, -1};
        for (auto& p : g.pointers)
            if (p.valid && p.sceneHover >= 0 && p.sceneHover != g.edit.group) show[1] = p.sceneHover;
        for (int k = 0; k < 2; k++) {
            int gi = show[k];
            if (gi < 0 || gi >= (int)g.edit.groups.size()) continue;
            CardGeom c = cardGeom(g.edit.groups[gi]);
            if (!c.valid) continue;
            bool anyPanel = false;
            for (auto& pn : g.panels) anyPanel = anyPanel || pn.visible;
            if (k == 0 && !anyPanel) continue;   // outline only while the panels are up
            XrVector3f d = cardDir(c.pan, c.tilt);
            float dist = std::max(0.9f, std::min(3.5f, c.dist));
            XrCompositionLayerQuad& q = cardQuad[ncq++];
            q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            q.space = g.localSpace;
            q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            q.subImage.swapchain = g.outline.handle;
            q.subImage.imageRect = {{0, 0}, {kLiftW, kLiftH}};
            // face the viewer: orientation from the direction
            float yaw = std::atan2(-d.x, -d.z), pitch = std::asin(std::max(-1.0f, std::min(1.0f, d.y)));
            q.pose.orientation = qMul(qYaw(yaw), qPitch(pitch));
            q.pose.position = vAdd(g.head.position, vScale(d, dist));
            float w = c.scale * dist / c.dist, h = w * 0.5625f;   // apparent size at the outline's distance (16:9 guess)
            q.size = {w * kLiftW / 160.0f * 1.03f, h * kLiftH / 160.0f * 1.03f};
            layers.push_back((XrCompositionLayerBaseHeader*)&q);
        }
    }
    XrFrameEndInfo fei{XR_TYPE_FRAME_END_INFO};
    fei.displayTime = fs.predictedDisplayTime;
    fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    // Never hand the compositor a pose or size that isn't finite: one NaN in one quad fails the whole frame
    // (XR_ERROR_POSE_INVALID, black screen). Drop the offender and say which one it was.
    {
        auto finite = [](float v) { return std::isfinite(v); };
        static int64_t lastWarnNs = 0;
        for (size_t i = 0; i < layers.size();) {
            bool ok = true;
            const char* what = "layer";
            // a quaternion that is nearly unit length is made exactly unit length here: the compositor's own check is
            // stricter than ours, and a pose restored from text or built from a few products can drift a little
            auto unit = [&](XrQuaternionf& o) {
                float n2 = o.x * o.x + o.y * o.y + o.z * o.z + o.w * o.w;
                if (!finite(n2) || std::fabs(n2 - 1.0f) >= 0.02f) return false;
                float inv = 1.0f / std::sqrt(n2);
                o.x *= inv; o.y *= inv; o.z *= inv; o.w *= inv;
                return true;
            };
            if (layers[i]->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR) {
                auto* e = (XrCompositionLayerEquirect2KHR*)layers[i];
                XrPosef& p = e->pose;
                ok = finite(p.position.x) && finite(p.position.y) && finite(p.position.z) && unit(p.orientation) && finite(e->radius) &&
                     finite(e->centralHorizontalAngle) && finite(e->upperVerticalAngle) && finite(e->lowerVerticalAngle);
                what = "equirect";
                if (!ok) LOGE("bad equirect: pos %.3f %.3f %.3f ori %.3f %.3f %.3f %.3f radius %.3f angles %.3f %.3f %.3f", p.position.x, p.position.y,
                              p.position.z, p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w, e->radius, e->centralHorizontalAngle,
                              e->upperVerticalAngle, e->lowerVerticalAngle);
            } else if (layers[i]->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
                auto* q = (XrCompositionLayerQuad*)layers[i];
                XrPosef& p = q->pose;
                ok = finite(p.position.x) && finite(p.position.y) && finite(p.position.z) && unit(p.orientation) &&
                     finite(q->size.width) && finite(q->size.height) && q->size.width > 0 && q->size.height > 0;
                if (!ok) LOGE("bad quad: pos %.3f %.3f %.3f ori %.3f %.3f %.3f %.3f size %.3f x %.3f", p.position.x, p.position.y, p.position.z,
                              p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w, q->size.width, q->size.height);
                what = q->subImage.swapchain == g.lift.handle ? "lift" : q->subImage.swapchain == g.cursor.handle ? "cursor"
                     : q->subImage.swapchain == g.outline.handle ? "card outline" : q->subImage.swapchain == g.strip.handle ? "strip" : "panel";
            }
            if (ok) { i++; continue; }
            int64_t now = monoNs();
            if (now - lastWarnNs > 1000000000LL) {
                LOGE("dropping %s quad with a non-finite pose or size", what);
                lastWarnNs = now;
            }
            layers.erase(layers.begin() + i);
        }
    }
    fei.layerCount = (uint32_t)layers.size();
    fei.layers = layers.data();
    {
        // a failed EndFrame is reported once a second with its layer count, not 70 times a second
        XrResult r = xrEndFrame(g.session, &fei);
        static int64_t lastFailNs = 0;
        static int failCount = 0;
        if (XR_FAILED(r)) {
            failCount++;
            int64_t now = monoNs();
            if (now - lastFailNs > 1000000000LL) {
                LOGE("xrEndFrame failed: %d (%d times, %u layers)", (int)r, failCount, (unsigned)layers.size());
                // say exactly what was handed over, so a rejected frame can be read from the log
                for (size_t i = 0; i < layers.size(); i++) {
                    if (layers[i]->type == XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR) {
                        auto* e = (const XrCompositionLayerEquirect2KHR*)layers[i];
                        LOGE("  layer %u equirect space %p pos %.4f %.4f %.4f ori %.5f %.5f %.5f %.5f radius %.3f angles %.3f %.3f %.3f flags %u", (unsigned)i,
                             (void*)e->space, e->pose.position.x, e->pose.position.y, e->pose.position.z, e->pose.orientation.x, e->pose.orientation.y,
                             e->pose.orientation.z, e->pose.orientation.w, e->radius, e->centralHorizontalAngle, e->upperVerticalAngle, e->lowerVerticalAngle,
                             (unsigned)e->layerFlags);
                    } else if (layers[i]->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
                        auto* q = (const XrCompositionLayerQuad*)layers[i];
                        const char* what = q->subImage.swapchain == g.lift.handle ? "lift" : q->subImage.swapchain == g.cursor.handle ? "cursor"
                                         : q->subImage.swapchain == g.outline.handle ? "outline" : q->subImage.swapchain == g.strip.handle ? "strip" : "panel";
                        const XrQuaternionf& o = q->pose.orientation;
                        LOGE("  layer %u quad %s space %p next %p pos %.4f %.4f %.4f ori %.5f %.5f %.5f %.5f n2 %.8f size %.4f x %.4f rect %d,%d %dx%d eye %d flags %u",
                             (unsigned)i, what, (void*)q->space, q->next, q->pose.position.x, q->pose.position.y, q->pose.position.z, o.x, o.y, o.z, o.w,
                             (double)(o.x * o.x + o.y * o.y + o.z * o.z + o.w * o.w), q->size.width, q->size.height,
                             q->subImage.imageRect.offset.x, q->subImage.imageRect.offset.y, q->subImage.imageRect.extent.width,
                             q->subImage.imageRect.extent.height, (int)q->eyeVisibility, (unsigned)q->layerFlags);
                    } else {
                        LOGE("  layer %u type %d", (unsigned)i, (int)layers[i]->type);
                    }
                }
                LOGE("  localSpace %p head pos %.3f %.3f %.3f valid %d cockpit valid %d panelOpen %d", (void*)g.localSpace, g.head.position.x,
                     g.head.position.y, g.head.position.z, g.headValid ? 1 : 0, g.cockpitValid ? 1 : 0, g.panelOpen ? 1 : 0);
                lastFailNs = now;
                failCount = 0;
            }
        }
    }
}

static void handleXrEvents() {
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(g.instance, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto* s = (XrEventDataSessionStateChanged*)&ev;
            g.state = s->state;
            LOGI("session state %d", s->state);
            if (s->state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (XR_SUCCEEDED(xrBeginSession(g.session, &bi))) {
                    g.sessionRunning = true;
                    if (g.setPerf) {
                        g.setPerf(g.session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
                        g.setPerf(g.session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
                    }
                    if (g.setColorSpace) g.setColorSpace(g.session, XR_COLOR_SPACE_REC709_FB);
                    if (g.configId) applyRefreshRate(g.cfg.msg.fpsDen ? (double)g.cfg.msg.fpsNum / g.cfg.msg.fpsDen : 0);
                }
            } else if (s->state == XR_SESSION_STATE_STOPPING) {
                xrEndSession(g.session);
                g.sessionRunning = false;
            } else if (s->state == XR_SESSION_STATE_EXITING || s->state == XR_SESSION_STATE_LOSS_PENDING) {
                g.exitRequested = true;
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            g.exitRequested = true;
        } else if (ev.type == XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB) {
            auto* r = (XrEventDataDisplayRefreshRateChangedFB*)&ev;
            g.currentHz = r->toDisplayRefreshRate;
            LOGI("display refresh now %.0f Hz", g.currentHz);
            // The runtime can apply its own default after our request (e.g. right after the session starts):
            // ask again so 29.97/59.94 content keeps an even cadence.
            if (g.extRefresh && g.wantHz > 0 && std::fabs(g.wantHz - g.currentHz) > 0.5f && g.refreshRetries < 3) {
                g.refreshRetries++;
                LOGI("re-requesting %.0f Hz", g.wantHz);
                if (XR_SUCCEEDED(g.requestRate(g.session, g.wantHz))) g.currentHz = g.wantHz;
            }
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

static void onAppCmd(android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_RESUME: g.resumed = true; break;
        case APP_CMD_PAUSE: g.resumed = false; break;
        case APP_CMD_DESTROY: g.exitRequested = true; break;
        default: break;
    }
}

void android_main(android_app* app) {
    JNIEnv* env = nullptr;
    app->activity->vm->AttachCurrentThread(&env, nullptr);
    g.android = app;
    app->onAppCmd = onAppCmd;
    g.settings.load(app->activity->internalDataPath ? app->activity->internalDataPath : "/data/local/tmp");
    g.edit.locked = g.settings.locked;
    // Start from the last settled value, but never from an outlier: it re-grows within a second if really needed.
    g.autoDelayNs = (int64_t)std::max(0, std::min(120, g.settings.autoDelayMs)) * 1000000;
    LOGI("VR Link starting (layout %d projection %d swap %d delay %d auto %d)", g.settings.layout,
         g.settings.projection, g.settings.swapEyes, g.settings.delayMs, g.settings.autoDelayMs);

    if (!initXr()) {
        LOGE("OpenXR init failed");
        ANativeActivity_finish(app->activity);
    } else {
        g.hud.start();
        g.bridge.start();
        g.net.setSource(g.settings.source);
        g.net.start(g.settings.lastHost == "-" ? "" : g.settings.lastHost, "Quest");
        g.decoder.start();
    }
    std::string savedHost = g.settings.lastHost;
    bool wasUsb = false;
    while (!app->destroyRequested && !g.exitRequested) {
        int events;
        android_poll_source* source;
        int timeout = (!g.resumed && !g.sessionRunning) ? 100 : 0;
        while (ALooper_pollOnce(timeout, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) break;
            timeout = 0;
        }
        if (g.instance == XR_NULL_HANDLE) continue;
        handleXrEvents();
        if (!g.sessionRunning) {
            usleep(10000);
            continue;
        }
        renderFrame();
        // remember a Wi-Fi host for next launch
        std::string h = g.net.host();
        if (!g.net.viaUsb() && !h.empty() && h != "USB" && h != savedHost) {
            savedHost = h;
            g.settings.lastHost = h;
            g.settings.save();
        }
        bool usb = g.net.viaUsb() && g.net.connected();
        if (usb != wasUsb) {
            if (usb) showNotice("Switched to USB");
            wasUsb = usb;
        }
    }
    LOGI("VR Link exiting");
    g.decoder.stop();
    g.net.stop();
    g.bridge.stop();
    g.hud.stop();
    if (g.destroyHandTracker)
        for (auto& t : g.handTracker)
            if (t != XR_NULL_HANDLE) g.destroyHandTracker(t);
    if (g.session) xrDestroySession(g.session);
    if (g.instance) xrDestroyInstance(g.instance);
    g.gfx.shutdown();
    app->activity->vm->DetachCurrentThread();
    // End the process here. Android keeps a finished NativeActivity's process around and runs the next launch
    // in it, on a new thread, over this file's globals: the second run found a half-torn-down instance and
    // exited within 70 ms, leaving the Quest on its loading screen until a reboot (v43 and every build before).
    LOGI("VR Link process ending");
    std::_Exit(0);
}
