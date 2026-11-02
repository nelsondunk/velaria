// VR Link NDI source: takes an NDI video stream on this Mac (Premiere / After Effects through the NDI Tools
// Mercury Transmit plug-in, or anything else that sends NDI) and streams it to the headset exactly as the
// virtual DeckLink does from Resolve: cropped to the active picture, HEVC Main10 via VideoToolbox, same wire
// protocol, own TCP port (47813) and the same UDP discovery, so the headset can pick either source.
//
// NDI stays on this machine. The Quest never sees NDI: a 5.6K SpeedHQ stream is 1.5-2.5 Gbit/s of software
// decode, which is why NDI players on headsets stutter. Here it is a loopback hop into the same hardware
// encoder the DeckLink path uses.
//
// libndi is loaded at runtime (dlopen) from NDI Tools / the NDI runtime, which the user needs anyway for the
// Adobe plug-in; nothing from the NDI SDK is linked or shipped. The handful of structs and entry points used
// below follow the NDI SDK 5/6 ABI.
//
//   vrlink_ndi [--source <substring>] [--list] [--port N]
#include <dlfcn.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ActiveArea.h"
#include "Scopes.h"
#include "Common.h"
#include "Config.h"
#include "Encoder.h"
#include "Server.h"
#include "protocol.h"
#include "timing.h"

using namespace vrl;
using namespace vrlproto;

// ------------------------------------------------------------------------------------------------ NDI ABI

namespace ndi {

struct Source {
    const char* name;
    const char* url;
};
struct FindCreate {
    bool showLocal;
    const char* groups;
    const char* extraIps;
};
struct RecvCreate {
    Source source;
    int colourFormat;   // 1 = UYVY (BGRA when the source has alpha)
    int bandwidth;      // 100 = highest
    bool allowFields;
    const char* recvName;
};
struct VideoFrame {
    int xres, yres;
    uint32_t fourcc;
    int frameRateN, frameRateD;
    float aspect;
    int frameFormat;    // 1 = progressive
    int64_t timecode;   // 100 ns units
    uint8_t* data;
    int lineStride;
    const char* metadata;
    int64_t timestamp;  // 100 ns units, sender clock
};

constexpr uint32_t kUYVY = 0x59565955;   // 'UYVY'
constexpr uint32_t kBGRA = 0x41524742;   // 'BGRA'
constexpr uint32_t kBGRX = 0x58524742;   // 'BGRX'

struct Lib {
    void* h = nullptr;
    bool (*initialize)() = nullptr;
    void* (*findCreate)(const FindCreate*) = nullptr;
    void (*findDestroy)(void*) = nullptr;
    bool (*findWait)(void*, uint32_t) = nullptr;
    const Source* (*findSources)(void*, uint32_t*) = nullptr;
    void* (*recvCreate)(const RecvCreate*) = nullptr;
    void (*recvDestroy)(void*) = nullptr;
    void (*recvConnect)(void*, const Source*) = nullptr;
    int (*recvCapture)(void*, VideoFrame*, void*, void*, uint32_t) = nullptr;
    void (*recvFreeVideo)(void*, VideoFrame*) = nullptr;
    int (*recvConnections)(void*) = nullptr;
    std::string path;

    bool load() {
        const char* candidates[] = {
            "/usr/local/lib/libndi.dylib",
            "/Library/NDI SDK for Apple/lib/macOS/libndi.dylib",
            "/Applications/NDI Video Monitor.app/Contents/Frameworks/libndi_advanced.dylib",
            "/Applications/NDI Tools/NDI Video Monitor.app/Contents/Frameworks/libndi_advanced.dylib",
            "/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/NDI_Transmit_AdobeCC.bundle/Contents/Frameworks/libndi.dylib",
        };
        if (const char* e = getenv("VRLINK_NDI_LIB")) {
            h = dlopen(e, RTLD_NOW | RTLD_LOCAL);
            if (h) path = e;
        }
        for (const char* c : candidates) {
            if (h) break;
            h = dlopen(c, RTLD_NOW | RTLD_LOCAL);
            if (h) path = c;
        }
        if (!h) return false;
        auto sym = [&](const char* n) { return dlsym(h, n); };
        initialize = (decltype(initialize))sym("NDIlib_initialize");
        findCreate = (decltype(findCreate))sym("NDIlib_find_create_v2");
        findDestroy = (decltype(findDestroy))sym("NDIlib_find_destroy");
        findWait = (decltype(findWait))sym("NDIlib_find_wait_for_sources");
        findSources = (decltype(findSources))sym("NDIlib_find_get_current_sources");
        recvCreate = (decltype(recvCreate))sym("NDIlib_recv_create_v3");
        recvDestroy = (decltype(recvDestroy))sym("NDIlib_recv_destroy");
        recvConnect = (decltype(recvConnect))sym("NDIlib_recv_connect");
        recvCapture = (decltype(recvCapture))sym("NDIlib_recv_capture_v2");
        recvFreeVideo = (decltype(recvFreeVideo))sym("NDIlib_recv_free_video_v2");
        recvConnections = (decltype(recvConnections))sym("NDIlib_recv_get_no_connections");
        return initialize && findCreate && findWait && findSources && recvCreate && recvCapture && recvFreeVideo;
    }
};

}  // namespace ndi

// ------------------------------------------------------------------------------------------------ pipeline

namespace {

std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop = true; }

struct StreamParams {
    int encW = 0, encH = 0;
    double fps = 0;
    Layout layout = Layout::Mono;
    Projection projection = Projection::Flat;
    int bitrateMbps = 0;
    int rasterW = 0, rasterH = 0;
    bool operator!=(const StreamParams& o) const {
        return encW != o.encW || encH != o.encH || std::fabs(fps - o.fps) > 1e-6 || layout != o.layout ||
               projection != o.projection || bitrateMbps != o.bitrateMbps || rasterW != o.rasterW || rasterH != o.rasterH;
    }
};

// Same rules as the DeckLink pipeline: 1:1 raster = top/bottom 360, 2:1 or 4:1 = side by side; per-eye aspect
// 1:1 = 180, 2:1 = 360, else flat.
void pickLayout(const Config& cfg, int w, int h, Layout& layout, Projection& proj) {
    double aspect = (double)w / h;
    if (cfg.layout == "sbs") layout = Layout::SideBySide;
    else if (cfg.layout == "tb") layout = Layout::TopBottom;
    else if (cfg.layout == "mono") layout = Layout::Mono;
    else if (std::fabs(aspect - 1.0) < 0.05) layout = Layout::TopBottom;
    else if (std::fabs(aspect - 2.0) < 0.08 || std::fabs(aspect - 4.0) < 0.15) layout = Layout::SideBySide;
    else layout = Layout::Mono;
    if (cfg.projection == "180") proj = Projection::Equirect180;
    else if (cfg.projection == "360") proj = Projection::Equirect360;
    else if (cfg.projection == "flat") proj = Projection::Flat;
    else {
        double eyeAspect = layout == Layout::SideBySide ? aspect / 2 : layout == Layout::TopBottom ? aspect * 2 : aspect;
        if (std::fabs(eyeAspect - 1.0) < 0.08) proj = Projection::Equirect180;
        else if (std::fabs(eyeAspect - 2.0) < 0.12) proj = Projection::Equirect360;
        else proj = Projection::Flat;
    }
}

struct FrameMeta {
    uint64_t seq = 0;
    int64_t ptsNs = 0;
    int64_t durationNs = 0;
    uint32_t configId = 0;
    uint32_t encodeIndex = 0;
};

class NdiPipeline {
public:
    NdiPipeline(const Config& cfg, int port, std::string sourceName) : m_cfg(cfg), m_port(port), m_sourceName(std::move(sourceName)) {
        m_area.configure(cfg.crop, 2.0);
        StreamServer::Callbacks cb;
        cb.keyframeNeeded = [this] { m_forceKey = true; };
        cb.clientCountChanged = [](int n) { VLOGI("headsets connected: %d", n); };
        cb.currentConfig = [this] {
            std::lock_guard<std::mutex> lk(m_mutex);
            return m_configMsg;
        };
        cb.currentPlayback = [this] { return playbackMsg(); };
        cb.scopesWanted = [this](bool on, int hz) { m_scopesHz = on ? std::max(1, std::min(15, hz > 0 ? hz : 10)) : 0; };
        // Discovery name: "NDI · <source>", so the headset's source list reads naturally next to "Resolve".
        std::string label = "NDI \xC2\xB7 " + m_sourceName;
        if (label.size() > 46) label.resize(46);
        m_server.start(m_port, label, cb);
    }
    ~NdiPipeline() { m_server.stop(); }

    MsgPtr playbackMsg() {
        // NDI frames are timed against this Mac's clock as they arrive; the headset plays them like a running
        // timeline (its own delay logic absorbs jitter). Nothing arrives while Premiere is parked, and the
        // last frame simply stays up.
        PlaybackMsg p{};
        p.state = 1;
        p.speed = 1.0;
        p.stopPtsNs = 0;
        return std::make_shared<std::vector<uint8_t>>(makeMessage(MsgType::Playback, p));
    }

    // Called on the receive thread for every NDI video frame.
    void onFrame(const ndi::VideoFrame& f) {
        BMDPixelFormat pf;
        if (f.fourcc == ndi::kUYVY) pf = bmdFormat8BitYUV;
        else if (f.fourcc == ndi::kBGRA || f.fourcc == ndi::kBGRX) pf = bmdFormat8BitBGRA;
        else {
            if (++m_unsupported % 100 == 1) VLOGW("NDI pixel format %s not supported", fourcc(f.fourcc).c_str());
            return;
        }
        // Source rate, headset or not: this is what the sender actually delivers (Premiere's playback rate at
        // 5.6K shows up here), logged every 10 s.
        int64_t now = nowNs();
        m_rateCount++;
        if (!m_rateStart) m_rateStart = now;
        if (now - m_rateStart >= 10LL * 1000000000LL) {
            VLOGI("NDI source %dx%d %s: %.1f fps received over %.0f s%s", f.xres, f.yres, fourcc(f.fourcc).c_str(),
                  m_rateCount * 1e9 / (double)(now - m_rateStart), (now - m_rateStart) / 1e9,
                  m_server.clientCount() ? "" : " (no headset: not encoded)");
            m_rateCount = 0;
            m_rateStart = now;
        }
        if (m_server.clientCount() == 0 && !m_cfg.streamWhenNoClient) {
            m_frames = 0;
            return;
        }
        uint64_t seq = ++m_seq;
        bool analyse = m_frames < 12 || m_frames % 12 == 0;
        m_frames++;
        CropRect crop = m_area.update(f.data, f.xres, f.yres, f.lineStride, pf, analyse, false);
        if (crop.empty()) return;
        if (int hz = m_scopesHz.load()) {
            if (now - m_lastScopesNs >= 1000000000LL / hz) {
                ScopesResult sr;
                if (computeScopes(f.data, f.xres, f.yres, f.lineStride, pf, crop, sr)) {
                    m_lastScopesNs = now;
                    ScopesMsg sm{};
                    sm.width = kScopesW;
                    sm.height = kScopesH;
                    sm.layout = kScopesLayoutV1;
                    sm.timecodeBCD = 0xFFFFFFFF;
                    sm.ptsNs = now;
                    sm.sampleCount = sr.samples;
                    m_server.broadcastControl(std::make_shared<const std::vector<uint8_t>>(
                        makeMessage(MsgType::Scopes, sm, sr.raster.data(), sr.raster.size())));
                }
            }
        }

        StreamParams p;
        p.rasterW = f.xres;
        p.rasterH = f.yres;
        p.fps = f.frameRateD > 0 ? (double)f.frameRateN / f.frameRateD : 30.0;
        pickLayout(m_cfg, crop.w, crop.h, p.layout, p.projection);
        double budgetPixels = std::min((double)m_cfg.maxEncodePixels, 0.8 * 850e6 / std::max(1.0, p.fps));
        double s = std::min({1.0, (double)m_cfg.maxEncodeWidth / crop.w, std::sqrt(budgetPixels / ((double)crop.w * crop.h))});
        double ew = crop.w * s, eh = crop.h * s;
        if (ew > 3840 && eh > 3840) {
            double s2 = 3840.0 / std::max(ew, eh);
            ew *= s2;
            eh *= s2;
        }
        p.encW = std::max(16, (int)std::lround(ew / 8) * 8);
        p.encH = std::max(16, (int)std::lround(eh / 8) * 8);
        double pixRate = (double)p.encW * p.encH * p.fps;
        p.bitrateMbps = m_cfg.bitrateMbps > 0 ? m_cfg.bitrateMbps
                                              : (int)std::max(20.0, std::min(150.0, std::round(pixRate * 0.2 / 1e6)));
        ColourInfo colour;   // NDI is BT.709 video range; HDR NDI (P216 etc.) is not handled yet
        int64_t t0 = nowNs();
        CVPixelBufferRef pb = m_conv.convert(f.data, f.lineStride, pf, crop, p.encW, p.encH, colour);
        m_convertMs.add((nowNs() - t0) / 1e6);
        if (!pb) return;
        bool reopened = false;
        if (!m_enc.isOpen() || p != m_params) {
            m_enc.flush();
            int gop = std::max(1, (int)std::lround(p.fps * m_cfg.keyframeIntervalSec));
            if (!m_enc.open(p.encW, p.encH, p.fps, p.bitrateMbps, gop, colour,
                            [this](EncodedPacket&& pkt) { onEncoded(std::move(pkt)); })) {
                CVPixelBufferRelease(pb);
                return;
            }
            m_params = p;
            m_bitrateMbps = p.bitrateMbps;
            m_configId++;
            reopened = true;
            VLOGI("stream %u: %dx%d (active %dx%d+%d+%d of %dx%d %s) %s %s @ %.3f fps %d Mbit/s", m_configId, p.encW, p.encH,
                  crop.w, crop.h, crop.x, crop.y, f.xres, f.yres, fourcc(f.fourcc).c_str(), layoutName((uint32_t)p.layout),
                  projectionName((uint32_t)p.projection), p.fps, p.bitrateMbps);
        }
        auto* meta = new FrameMeta;
        meta->seq = seq;
        meta->configId = m_configId;
        meta->durationNs = (int64_t)(1e9 / p.fps);
        // Present a little after arrival so the headset's scheduler has a stable target; its automatic delay
        // takes care of the rest.
        meta->ptsNs = nowNs() + 40000000LL;
        bool key = reopened || m_forceKey.exchange(false);
        if (!m_enc.encode(pb, seq, key, meta)) delete meta;
        CVPixelBufferRelease(pb);
        if (m_frames % 300 == 0)
            VLOGI("received %llu NDI frames; convert p50 %.1f ms, encode p50 %.1f / p95 %.1f ms, %d headset(s)",
                  (unsigned long long)m_frames, m_convertMs.percentile(50), m_encodeMs.percentile(50),
                  m_encodeMs.percentile(95), m_server.clientCount());
    }

    void idle() {
        if (m_enc.isOpen() && m_server.clientCount() == 0) {
            m_enc.close();
            m_params = StreamParams();
            VLOGI("encoder closed (idle, no headsets)");
        }
    }

private:
    void onEncoded(EncodedPacket&& pkt) {
        std::unique_ptr<FrameMeta> meta(static_cast<FrameMeta*>(pkt.user));
        if (!meta || pkt.annexB.empty()) return;
        m_encodeMs.add(pkt.encodeNs / 1e6);
        if (pkt.keyframe && meta->configId != m_announcedConfig) {
            MsgPtr cfg = buildConfig(meta->configId);
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_configMsg = cfg;
            }
            m_announcedConfig = meta->configId;
            m_server.broadcastControl(cfg);
            m_server.broadcastControl(playbackMsg());
        }
        if (meta->configId != m_announcedConfig) return;
        if (meta->configId != m_indexConfig) {
            m_encodeIndex = 0;
            m_indexConfig = meta->configId;
        }
        VideoMsg v{};
        v.configId = meta->configId;
        v.encodeIndex = ++m_encodeIndex;
        v.flags = pkt.keyframe ? kVideoKeyframe : 0;
        v.frameSeq = meta->seq;
        v.durationNs = meta->durationNs;
        v.timecodeBCD = 0xFFFFFFFF;
        v.ptsNs = meta->ptsNs;
        auto msg = std::make_shared<std::vector<uint8_t>>(makeMessage(MsgType::Video, v, pkt.annexB.data(), pkt.annexB.size()));
        m_server.broadcastVideo(msg, pkt.keyframe, (int64_t)m_bitrateMbps.load() * 1000000);
    }

    MsgPtr buildConfig(uint32_t configId) {
        StreamConfigMsg c{};
        c.configId = configId;
        c.codec = 0x31637668;   // 'hvc1'
        c.width = (uint32_t)m_params.encW;
        c.height = (uint32_t)m_params.encH;
        c.layout = (uint32_t)m_params.layout;
        c.projection = (uint32_t)m_params.projection;
        c.fpsNum = m_params.fps > 0 ? (uint32_t)std::lround(m_params.fps * 1001) : 0;
        c.fpsDen = 1001;
        c.colorPrimaries = 1;
        c.transfer = 1;
        c.matrix = 1;
        c.bitDepth = 10;
        c.rasterWidth = (uint32_t)m_params.rasterW;
        c.rasterHeight = (uint32_t)m_params.rasterH;
        std::string mode = "NDI " + std::to_string(m_params.rasterW) + "x" + std::to_string(m_params.rasterH);
        strncpy(c.modeName, mode.c_str(), sizeof c.modeName - 1);
        std::vector<uint8_t> ps = m_enc.parameterSets();
        c.csdLength = (uint32_t)ps.size();
        return std::make_shared<std::vector<uint8_t>>(makeMessage(MsgType::StreamConfig, c, ps.data(), ps.size()));
    }

    const Config& m_cfg;
    int m_port;
    std::string m_sourceName;
    StreamServer m_server;
    std::mutex m_mutex;
    MsgPtr m_configMsg;
    FrameConverter m_conv;
    std::atomic<int> m_scopesHz{0};
    int64_t m_lastScopesNs = 0;
    HevcEncoder m_enc;
    ActiveAreaTracker m_area;
    StreamParams m_params;
    std::atomic<bool> m_forceKey{true};
    std::atomic<int> m_bitrateMbps{20};
    uint64_t m_seq = 0, m_frames = 0, m_unsupported = 0, m_rateCount = 0;
    int64_t m_rateStart = 0;
    uint32_t m_configId = 0, m_announcedConfig = 0, m_indexConfig = 0, m_encodeIndex = 0;
    vrltime::RollingStat m_convertMs{300}, m_encodeMs{300};
};

// Picks the source to follow: an explicit substring match first, else Premiere / After Effects, else the first.
int chooseSource(const ndi::Source* srcs, uint32_t n, const std::string& want) {
    if (!n) return -1;
    auto lower = [](std::string s) {
        for (auto& ch : s) ch = (char)tolower((unsigned char)ch);
        return s;
    };
    if (!want.empty()) {
        std::string w = lower(want);
        for (uint32_t i = 0; i < n; i++)
            if (lower(srcs[i].name ? srcs[i].name : "").find(w) != std::string::npos) return (int)i;
        return -1;
    }
    for (uint32_t i = 0; i < n; i++) {
        std::string nm = lower(srcs[i].name ? srcs[i].name : "");
        if (nm.find("premiere") != std::string::npos || nm.find("after effects") != std::string::npos) return (int)i;
    }
    return 0;
}

// The source's own label without the machine name: "BRUCE (Adobe Premiere Pro)" -> "Adobe Premiere Pro".
std::string shortSourceName(const char* full) {
    std::string s = full ? full : "";
    size_t a = s.find('('), b = s.rfind(')');
    if (a != std::string::npos && b != std::string::npos && b > a + 1) return s.substr(a + 1, b - a - 1);
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    // own log file next to the driver's
    if (!getenv("VRLINK_LOGFILE")) {
        std::string dir = std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/Library/Logs/Velaria";
        mkdir(dir.c_str(), 0755);
        setenv("VRLINK_LOGFILE", (dir + "/ndi.log").c_str(), 1);
    }
    std::string want = getenv("VRLINK_NDI_SOURCE") ? getenv("VRLINK_NDI_SOURCE") : "";
    int port = getenv("VRLINK_NDI_PORT") ? atoi(getenv("VRLINK_NDI_PORT")) : kNdiTcpPort;
    bool list = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--source" && i + 1 < argc) want = argv[++i];
        else if (a == "--port" && i + 1 < argc) port = atoi(argv[++i]);
        else if (a == "--list") list = true;
        else {
            fprintf(stderr, "usage: vrlink_ndi [--source <substring>] [--port N] [--list]\n");
            return 2;
        }
    }
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    ndi::Lib lib;
    if (!lib.load()) {
        VLOGE("NDI runtime not found: install NDI Tools (free, ndi.video/tools) - it also provides the Adobe plug-in");
        fprintf(stderr, "NDI runtime not found. Install NDI Tools from ndi.video/tools.\n");
        return 1;
    }
    VLOGI("NDI runtime: %s", lib.path.c_str());
    if (!lib.initialize()) {
        VLOGE("NDIlib_initialize failed (unsupported CPU?)");
        return 1;
    }
    ndi::FindCreate fc{true, nullptr, nullptr};
    void* finder = lib.findCreate(&fc);
    if (!finder) {
        VLOGE("NDI finder failed");
        return 1;
    }
    const Config& cfg = Config::get();
    std::unique_ptr<NdiPipeline> pipe;
    void* recv = nullptr;
    std::string current;   // full name of the source we follow
    int64_t lastFrameNs = 0, lastLog = 0;

    while (!g_stop) {
        if (!recv) {
            lib.findWait(finder, 1000);
            uint32_t n = 0;
            const ndi::Source* srcs = lib.findSources(finder, &n);
            if (list) {
                printf("%u NDI source(s):\n", n);
                for (uint32_t i = 0; i < n; i++) printf("  %s  (%s)\n", srcs[i].name, srcs[i].url ? srcs[i].url : "");
                return 0;
            }
            int idx = chooseSource(srcs, n, want);
            if (idx < 0) {
                int64_t now = nowNs();
                if (now - lastLog > 10LL * 1000000000LL) {
                    lastLog = now;
                    VLOGI("waiting for an NDI source%s%s (%u visible)", want.empty() ? "" : " matching ", want.c_str(), n);
                }
                continue;
            }
            current = srcs[idx].name;
            ndi::RecvCreate rc{srcs[idx], 1 /* UYVY, BGRA with alpha */, 100 /* highest */, false, "Kalyx"};
            recv = lib.recvCreate(&rc);
            if (!recv) {
                VLOGE("NDI receiver failed for %s", current.c_str());
                usleep(1000000);
                continue;
            }
            if (lib.recvConnect) lib.recvConnect(recv, &srcs[idx]);
            VLOGI("following NDI source '%s'", current.c_str());
            pipe = std::make_unique<NdiPipeline>(cfg, port, shortSourceName(current.c_str()));
            lastFrameNs = nowNs();
        }
        ndi::VideoFrame vf{};
        int r = lib.recvCapture(recv, &vf, nullptr, nullptr, 250);
        if (r == 1) {
            lastFrameNs = nowNs();
            pipe->onFrame(vf);
            lib.recvFreeVideo(recv, &vf);
        } else {
            int64_t now = nowNs();
            if (now - lastFrameNs > 10LL * 1000000000LL) {
                pipe->idle();
                lastFrameNs = now;   // keep the receiver: Premiere sends nothing while parked, that is normal
                // ... unless the source has gone away entirely: re-discover and follow a new one
                uint32_t n = 0;
                const ndi::Source* srcs = lib.findSources(finder, &n);
                bool still = false;
                for (uint32_t i = 0; i < n; i++)
                    if (srcs[i].name && current == srcs[i].name) still = true;
                if (!still) {
                    VLOGI("NDI source '%s' is gone", current.c_str());
                    if (lib.recvDestroy) lib.recvDestroy(recv);
                    recv = nullptr;
                    pipe.reset();
                }
            }
        }
    }
    if (recv && lib.recvDestroy) lib.recvDestroy(recv);
    if (lib.findDestroy) lib.findDestroy(finder);
    return 0;
}
