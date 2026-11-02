// The virtual card's "scan-out": frames Resolve plays out are cropped to the active picture, encoded to HEVC
// Main10 and streamed to connected headsets with their exact scan-out times.
//
// Threading:
//   Resolve's thread  -> onFrame(): queue a job (or, for a paused/scrub frame, wait until it is converted,
//                        because Resolve may reuse that buffer as soon as DisplayVideoFrameSync returns)
//   worker thread     -> crop/convert (reads Resolve's buffer), then hand the IOSurface to VideoToolbox
//   device clock      -> onFrameDisplayed(): the frame is about to be returned to Resolve; make sure we are done
//                        reading it (cancel if not started, wait if converting)
//   VideoToolbox      -> onEncoded(): stamp scan-out time, packetize, broadcast
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "ActiveArea.h"
#include "Scopes.h"
#include "Common.h"
#include "Config.h"
#include "Encoder.h"
#include "FrameSink.h"
#include "Server.h"
#include "protocol.h"
#include "timing.h"

namespace vrl {

using namespace vrlproto;

namespace {

struct FrameMeta {
    uint64_t seq = 0;
    int64_t streamNs = -1;
    int64_t durationNs = 0;
    bool sync = false;
    uint32_t run = 0;          // playback run the frame belongs to
    int64_t syncHostNs = 0;    // for sync frames: when Resolve displayed it
    uint32_t timecode = 0xFFFFFFFF;
    uint32_t configId = 0;
    uint32_t encodeIndex = 0;
};

struct Job {
    enum State { Queued, Converting, Done, Cancelled };
    IDeckLinkVideoFrame* frame = nullptr;
    const ModeInfo* mode = nullptr;
    FrameMeta meta;
    State state = Queued;
    // When Resolve needs its buffer back before the worker got to this frame (encoder still opening, a hiccup),
    // the picture is copied here and Resolve's frame is returned at once: no drop, no stall on Resolve's side.
    std::shared_ptr<std::vector<uint8_t>> copy;
    int w = 0, h = 0;
    long rowBytes = 0;
    BMDPixelFormat pf = 0;
    ColourInfo colour;
};

struct StreamParams {
    int encW = 0, encH = 0;
    double fps = 0;
    ColourInfo colour;
    Layout layout = Layout::SideBySide;
    Projection projection = Projection::Equirect180;
    int bitrateMbps = 0;
    int rasterW = 0, rasterH = 0;
    std::string modeName;
    bool operator!=(const StreamParams& o) const {
        return encW != o.encW || encH != o.encH || std::fabs(fps - o.fps) > 1e-6 || !(colour == o.colour) ||
               layout != o.layout || projection != o.projection || bitrateMbps != o.bitrateMbps ||
               rasterW != o.rasterW || rasterH != o.rasterH;
    }
};

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
        // per-eye aspect: 1:1 -> 180, 2:1 -> 360
        double eyeAspect = layout == Layout::SideBySide ? aspect / 2 : layout == Layout::TopBottom ? aspect * 2 : aspect;
        if (std::fabs(eyeAspect - 1.0) < 0.08) proj = Projection::Equirect180;
        else if (std::fabs(eyeAspect - 2.0) < 0.12) proj = Projection::Equirect360;
        else proj = Projection::Flat;
    }
}

ColourInfo colourFromFrame(IDeckLinkVideoFrame* f) {
    ColourInfo c;
    IDeckLinkVideoFrameMetadataExtensions* md = nullptr;
    if (f->QueryInterface(IID_IDeckLinkVideoFrameMetadataExtensions, (void**)&md) == S_OK && md) {
        int64_t cs = 0, eotf = 0;
        if (md->GetInt(bmdDeckLinkFrameMetadataColorspace, &cs) == S_OK && cs == bmdColorspaceRec2020) {
            c.primaries = 9;
            c.matrix = 9;
        }
        if ((f->GetFlags() & bmdFrameContainsHDRMetadata) &&
            md->GetInt(bmdDeckLinkFrameMetadataHDRElectroOpticalTransferFunc, &eotf) == S_OK) {
            if (eotf == 2) c.transfer = 16;
            else if (eotf == 3) c.transfer = 18;
        }
        md->Release();
    }
    return c;
}

uint32_t timecodeOf(IDeckLinkVideoFrame* f) {
    IDeckLinkTimecode* tc = nullptr;
    uint32_t bcd = 0xFFFFFFFF;
    if (f->GetTimecode(bmdTimecodeRP188Any, &tc) == S_OK && tc) {
        bcd = tc->GetBCD();
        tc->Release();
    }
    return bcd;
}

class StreamPipeline final : public FrameSink {
public:
    StreamPipeline() : m_cfg(Config::get()) {
        m_area.configure(m_cfg.crop, 2.0);
        StreamServer::Callbacks cb;
        cb.keyframeNeeded = [this] { m_forceKey = true; };
        cb.clientCountChanged = [this](int n) {
            VLOGI("headsets connected: %d", n);
            m_cv.notify_all();
        };
        cb.currentConfig = [this] {
            std::lock_guard<std::mutex> lk(m_mutex);
            return m_configMsg;
        };
        cb.currentPlayback = [this] {
            std::lock_guard<std::mutex> lk(m_mutex);
            return playbackMsgLocked(0);
        };
        cb.scopesWanted = [this](bool on, int hz) { m_scopesHz = on ? std::max(1, std::min(15, hz > 0 ? hz : 10)) : 0; };
        m_serverCb = cb;
        // The server is NOT started here. Any app that scans for Blackmagic hardware (Zoom's camera helper, OBS,
        // Skype...) loads this framework and creates the device; if each of them opened port 47810, whichever
        // launched first would own it and Resolve's headsets would connect to an app with no picture. The port
        // is opened by the first EnableVideoOutput, i.e. only in the process that actually drives the card.
        VLOGI("VR Link driver loaded into '%s' (pid %d); headset server starts with the first video output",
              getprogname(), (int)getpid());
        m_worker = std::thread([this] { workerMain(); });
        m_worker.detach();   // lives for the process (Resolve) lifetime
    }

    // ---------------------------------------------------------------- FrameSink

    void onOutputEnabled(const ModeInfo* mode) override {
        // Keep the server up once started: Resolve toggles the output while settings change, and the headset
        // should not drop for that.
        m_server.start(m_cfg.port, m_cfg.name, m_serverCb);
        std::lock_guard<std::mutex> lk(m_mutex);
        m_mode = mode;
        m_resetArea = true;
        VLOGI("output enabled: %s (%dx%d)", mode->name, mode->width, mode->height);
    }

    void onOutputDisabled() override {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_mode = nullptr;
        m_running = false;
        m_held.clear();
        VLOGI("output disabled");
    }

    void onPlaybackStarted(int64_t anchorHostNs, int64_t anchorStreamNs, double speed, uint32_t runGen) override {
        MsgPtr pb;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_run = runGen;
            m_running = true;
            m_anchorHost = anchorHostNs;
            m_anchorStream = anchorStreamNs;
            m_speed = speed == 0 ? 1.0 : speed;
            pb = playbackMsgLocked(0);
        }
        m_server.broadcastControl(pb);
        std::lock_guard<std::mutex> elk(m_emitMutex);
        flushHeldEmitLocked();
    }

    // Emits prerolled frames in encode order once their run has started. Caller holds m_emitMutex.
    void flushHeldEmitLocked() {
        std::vector<Held> release;
        uint32_t run;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (!m_running || m_held.empty()) return;
            release.swap(m_held);
            run = m_run;
        }
        for (auto& h : release) emit(h.meta, std::move(h.au), h.key, h.meta.run != run);
    }

    void onPlaybackStopped(uint32_t runGen) override {
        MsgPtr pb;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (runGen != m_run) {   // stale: a newer run has already started
                VLOGI("ignoring stop for run %u (current run %u)", runGen, m_run);
                return;
            }
            m_running = false;
            pb = playbackMsgLocked(nowNs());
            logLeadLocked();
        }
        m_server.broadcastControl(pb);
    }

    void onFrame(const ScheduledFrame& f) override {
        if (!f.frame || !f.mode) return;
        if (m_server.clientCount() == 0 && !m_cfg.streamWhenNoClient) return;
        auto job = std::make_shared<Job>();
        job->frame = f.frame;
        job->mode = f.mode;
        job->meta.seq = f.seq;
        job->meta.streamNs = f.streamTimeNs;
        job->meta.durationNs = f.durationNs;
        job->meta.sync = f.sync;
        job->meta.timecode = timecodeOf(f.frame);
        job->meta.syncHostNs = nowNs();
        f.frame->AddRef();
        std::unique_lock<std::mutex> lk(m_mutex);
        job->meta.run = m_running ? m_run : m_run + 1;
        if (!f.sync && m_running) {
            int64_t lead = hostForStreamLocked(f.streamTimeNs) - nowNs();
            m_lead.add(lead / 1e6);
        }
        m_jobs[f.seq] = job;
        m_queue.push_back(job);
        m_cv.notify_all();
        if (f.sync) {
            // Resolve may overwrite this buffer once DisplayVideoFrameSync returns: wait for the copy-out.
            m_cv.wait_for(lk, std::chrono::milliseconds(250), [&] { return job->state == Job::Done || job->state == Job::Cancelled; });
        }
    }

    void onFrameDisplayed(uint64_t seq, int64_t hostNs, bool late) override {
        IDeckLinkVideoFrame* toRelease = nullptr;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            auto it = m_jobs.find(seq);
            if (it == m_jobs.end()) return;
            auto job = it->second;
            // Not started yet: give the worker a moment (it may be finishing the previous frame).
            if (job->state == Job::Queued && !job->copy)
                m_cv.wait_for(lk, std::chrono::milliseconds(6), [&] { return job->state != Job::Queued; });
            if (job->state == Job::Converting)
                m_cv.wait_for(lk, std::chrono::milliseconds(200), [&] { return job->state != Job::Converting; });
            if (job->state == Job::Queued && job->frame && m_detached < 3) {
                // Worker is busy (typically opening the encoder at a stream start): take a private copy so
                // Resolve gets its buffer back on time and the frame still reaches the headset.
                void* bytes = nullptr;
                if (job->frame->GetBytes(&bytes) == S_OK && bytes) {
                    job->w = (int)job->frame->GetWidth();
                    job->h = (int)job->frame->GetHeight();
                    job->rowBytes = job->frame->GetRowBytes();
                    job->pf = job->frame->GetPixelFormat();
                    job->colour = colourFromFrame(job->frame);
                    size_t n = (size_t)job->rowBytes * job->h;
                    job->copy = takeBufferLocked(n);
                    memcpy(job->copy->data(), bytes, n);
                    toRelease = job->frame;
                    job->frame = nullptr;
                    m_detached++;
                    if (++m_copied % 50 == 1)
                        VLOGI("worker busy at scan-out: copied frame instead of dropping (%llu so far)",
                              (unsigned long long)m_copied);
                }
            }
            if (job->state == Job::Queued && job->frame) {
                job->state = Job::Cancelled;   // persistently behind: drop from the stream, never from Resolve
                toRelease = job->frame;
                job->frame = nullptr;
                m_dropped++;
                if (m_dropped % 50 == 1) VLOGW("encoder behind: %llu frames skipped so far", (unsigned long long)m_dropped);
            } else if (job->state == Job::Converting) {
                m_cv.wait_for(lk, std::chrono::milliseconds(200), [&] { return job->state != Job::Converting; });
            }
            m_jobs.erase(it);
        }
        if (toRelease) toRelease->Release();
    }

    void onAudio(const void*, uint32_t, uint32_t, uint32_t, int64_t) override {
        // Audio is not streamed in v1 (headset preview is picture-only; Resolve keeps playing audio locally).
    }

private:
    struct Held {
        FrameMeta meta;
        std::vector<uint8_t> au;
        bool key;
    };

    int64_t hostForStreamLocked(int64_t streamNs) const {
        return m_anchorHost + (int64_t)((double)(streamNs - m_anchorStream) / m_speed);
    }

    MsgPtr playbackMsgLocked(int64_t stopPts) const {
        PlaybackMsg p{};
        p.state = m_running ? 1 : 0;
        p.speed = m_speed;
        p.stopPtsNs = stopPts;
        return std::make_shared<std::vector<uint8_t>>(makeMessage(MsgType::Playback, p));
    }

    void logLeadLocked() {
        if (m_lead.count() > 10)
            VLOGI("schedule lead (frame handed to card -> its scan-out): p5 %.1f ms, p50 %.1f ms", m_lead.percentile(5),
                  m_lead.percentile(50));
        m_lead.clear();
    }

    // ---------------------------------------------------------------- worker

    void workerMain() {
        pthread_setname_np("VRLink.encode");
        int64_t idleSince = 0;
        while (true) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock<std::mutex> lk(m_mutex);
                m_cv.wait_for(lk, std::chrono::seconds(1), [this] { return !m_queue.empty(); });
                while (!m_queue.empty() && !job) {
                    auto j = m_queue.front();
                    m_queue.pop_front();
                    if (j->state == Job::Queued) {
                        j->state = Job::Converting;
                        job = j;
                    }
                }
                if (m_resetArea) {
                    m_area.reset();
                    m_framesAnalysed = 0;
                    m_resetArea = false;
                }
            }
            if (job) m_cv.notify_all();   // onFrameDisplayed may be waiting for this job to start
            if (!job) {
                // Release the hardware encoder after a while with nobody watching.
                if (m_enc.isOpen() && m_server.clientCount() == 0) {
                    if (!idleSince) idleSince = nowNs();
                    if (nowNs() - idleSince > 10LL * 1000000000LL) {
                        m_enc.close();
                        m_params = StreamParams();
                        VLOGI("encoder closed (idle, no headsets)");
                    }
                } else
                    idleSince = 0;
                continue;
            }
            idleSince = 0;
            process(job);
        }
    }

    // Caller holds m_mutex.
    std::shared_ptr<std::vector<uint8_t>> takeBufferLocked(size_t n) {
        for (size_t i = 0; i < m_spare.size(); i++) {
            if (m_spare[i]->size() >= n) {
                auto b = m_spare[i];
                m_spare.erase(m_spare.begin() + (long)i);
                return b;
            }
        }
        return std::make_shared<std::vector<uint8_t>>(n);
    }

    void finishJob(const std::shared_ptr<Job>& job) {
        IDeckLinkVideoFrame* f;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            job->state = Job::Done;
            f = job->frame;
            job->frame = nullptr;
            if (job->copy) {
                if (m_spare.size() < 3) m_spare.push_back(job->copy);
                job->copy.reset();
                m_detached--;
            }
        }
        m_cv.notify_all();
        if (f) f->Release();
    }

    void process(const std::shared_ptr<Job>& job) {
        void* bytes = nullptr;
        int W, H;
        long rb;
        BMDPixelFormat pf;
        ColourInfo colour;
        std::shared_ptr<std::vector<uint8_t>> copy;
        IDeckLinkVideoFrame* fr = nullptr;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            copy = job->copy;
            fr = job->frame;
        }
        if (copy) {
            bytes = copy->data();
            W = job->w;
            H = job->h;
            rb = job->rowBytes;
            pf = job->pf;
            colour = job->colour;
        } else {
            if (!fr || fr->GetBytes(&bytes) != S_OK || !bytes) {
                finishJob(job);
                return;
            }
            W = (int)fr->GetWidth();
            H = (int)fr->GetHeight();
            rb = fr->GetRowBytes();
            pf = fr->GetPixelFormat();
            colour = colourFromFrame(fr);
        }
        // Parked/scrub frames are rare and often follow a settings change: always look at them.
        bool analyse = job->meta.sync || m_framesAnalysed < 12 || m_framesAnalysed % 12 == 0;
        m_framesAnalysed++;
        CropRect crop = m_area.update((const uint8_t*)bytes, W, H, rb, pf, analyse, job->meta.sync);
        // Scopes, measured on this uncompressed frame while we still hold it (a parked frame always gets them).
        if (int hz = m_scopesHz.load()) {
            int64_t now = nowNs();
            if (job->meta.sync || now - m_lastScopesNs >= 1000000000LL / hz) {
                ScopesResult sr;
                if (computeScopes((const uint8_t*)bytes, W, H, rb, pf, crop, sr)) {
                    m_lastScopesNs = now;
                    ScopesMsg sm{};
                    sm.width = kScopesW;
                    sm.height = kScopesH;
                    sm.layout = kScopesLayoutV1;
                    sm.timecodeBCD = job->meta.timecode;
                    sm.ptsNs = job->meta.streamNs;
                    sm.sampleCount = sr.samples;
                    m_server.broadcastControl(std::make_shared<const std::vector<uint8_t>>(
                        makeMessage(MsgType::Scopes, sm, sr.raster.data(), sr.raster.size())));
                }
            }
        }

        StreamParams p;
        p.rasterW = W;
        p.rasterH = H;
        p.modeName = job->mode->name;
        p.fps = (double)job->mode->timeScale / (double)job->mode->frameDuration;
        p.colour = colour;
        pickLayout(m_cfg, crop.w, crop.h, p.layout, p.projection);
        // Encoder budget, measured on an M4 Max (HEVC Main10, VideoToolbox): ~850 Mpixel/s when at least one
        // dimension is <= 3840, but throughput collapses when both exceed it (4320x4320: 32 fps vs 3840x3840:
        // 60 fps). Keep 20% headroom so the encoder never falls behind Resolve.
        double budgetPixels = std::min((double)m_cfg.maxEncodePixels, 0.8 * 850e6 / std::max(1.0, p.fps));
        double s = std::min({1.0, (double)m_cfg.maxEncodeWidth / crop.w,
                             std::sqrt(budgetPixels / ((double)crop.w * crop.h))});
        double ew = crop.w * s, eh = crop.h * s;
        if (ew > 3840 && eh > 3840) {
            double s2 = 3840.0 / std::max(ew, eh);
            ew *= s2;
            eh *= s2;
        }
        // Nearest multiple of 8 (HEVC minimum coding block; keeps SBS/TB halves on even lines). Never force
        // coarser alignment: rounding 1080 to 1024 or 2160 to 2112 would squeeze the picture by 2-5%.
        p.encW = std::max(16, (int)std::lround(ew / 8) * 8);
        p.encH = std::max(16, (int)std::lround(eh / 8) * 8);
        double pixRate = (double)p.encW * p.encH * p.fps;
        p.bitrateMbps = m_cfg.bitrateMbps > 0 ? m_cfg.bitrateMbps
                                              : (int)std::max(20.0, std::min(150.0, std::round(pixRate * 0.2 / 1e6)));

        // Convert first so Resolve's buffer is released before any (slow, ~100 ms) encoder re-open.
        int64_t t0 = nowNs();
        CVPixelBufferRef pb = m_conv.convert((const uint8_t*)bytes, rb, pf, crop, p.encW, p.encH, p.colour);
        m_convertMs.add((nowNs() - t0) / 1e6);
        finishJob(job);   // done reading Resolve's buffer (or our copy of it)
        if (!pb) return;
        bool reopened = false;
        if (!m_enc.isOpen() || p != m_params) {
            m_enc.flush();
            int gop = std::max(1, (int)std::lround(p.fps * m_cfg.keyframeIntervalSec));
            if (!m_enc.open(p.encW, p.encH, p.fps, p.bitrateMbps, gop, p.colour,
                            [this](EncodedPacket&& pkt) { onEncoded(std::move(pkt)); })) {
                CVPixelBufferRelease(pb);
                return;
            }
            m_params = p;
            m_bitrateMbps = p.bitrateMbps;
            m_configId++;
            reopened = true;
            VLOGI("stream %u: %dx%d (active %dx%d+%d+%d of %dx%d %s) %s %s @ %.3f fps %d Mbit/s colour %u/%u/%u",
                  m_configId, p.encW, p.encH, crop.w, crop.h, crop.x, crop.y, W, H, fourcc(pf).c_str(),
                  layoutName((uint32_t)p.layout), projectionName((uint32_t)p.projection), p.fps, p.bitrateMbps,
                  p.colour.primaries, p.colour.transfer, p.colour.matrix);
        }
        auto* meta = new FrameMeta(job->meta);
        meta->configId = m_configId;
        bool key = reopened || m_forceKey.exchange(false);
        if (!m_enc.encode(pb, meta->seq, key, meta)) delete meta;
        CVPixelBufferRelease(pb);
    }

    // ---------------------------------------------------------------- output

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
        }
        if (meta->configId != m_announcedConfig) return;   // not decodable without its config
        std::lock_guard<std::mutex> elk(m_emitMutex);
        if (pkt.keyframe || meta->configId != m_indexConfig) {
            if (meta->configId != m_indexConfig) m_encodeIndex = 0;
            m_indexConfig = meta->configId;
        }
        meta->encodeIndex = ++m_encodeIndex;
        bool noDisplay = false;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (!meta->sync) {
                if (meta->run == m_run + 1 && !m_running) {
                    m_held.push_back({*meta, std::move(pkt.annexB), pkt.keyframe});   // preroll: wait for start
                    return;
                }
                // A frame from a run Resolve already stopped was flushed before scan-out: still send it so the
                // headset decoder keeps its reference chain, but never display it.
                if (meta->run != m_run) noDisplay = true;
            }
        }
        flushHeldEmitLocked();
        emit(*meta, std::move(pkt.annexB), pkt.keyframe, noDisplay);
        if (++m_emitted % 300 == 0) {
            VLOGI("streamed %llu frames; convert p50 %.1f ms, encode p50 %.1f / p95 %.1f ms, %d headset(s)",
                  (unsigned long long)m_emitted, m_convertMs.percentile(50), m_encodeMs.percentile(50),
                  m_encodeMs.percentile(95), m_server.clientCount());
        }
    }

    void emit(const FrameMeta& meta, std::vector<uint8_t>&& au, bool key, bool noDisplay) {
        VideoMsg v{};
        v.configId = meta.configId;
        v.encodeIndex = meta.encodeIndex;
        v.flags = (key ? kVideoKeyframe : 0) | (meta.sync ? kVideoSync : 0) | (noDisplay ? kVideoNoDisplay : 0);
        v.frameSeq = meta.seq;
        v.durationNs = meta.durationNs;
        v.timecodeBCD = meta.timecode;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            v.ptsNs = meta.sync ? meta.syncHostNs : hostForStreamLocked(meta.streamNs);
        }
        auto msg = std::make_shared<std::vector<uint8_t>>(makeMessage(MsgType::Video, v, au.data(), au.size()));
        m_server.broadcastVideo(msg, key, (int64_t)m_bitrateMbps.load() * 1000000);
    }

    MsgPtr buildConfig(uint32_t configId) {
        StreamConfigMsg c{};
        c.configId = configId;
        c.codec = 0x31637668;   // 'hvc1'
        c.width = (uint32_t)m_params.encW;
        c.height = (uint32_t)m_params.encH;
        c.layout = (uint32_t)m_params.layout;
        c.projection = (uint32_t)m_params.projection;
        // exact rational rate from the mode
        c.fpsNum = m_params.fps > 0 ? (uint32_t)std::lround(m_params.fps * 1001) : 0;
        c.fpsDen = 1001;
        c.colorPrimaries = m_params.colour.primaries;
        c.transfer = m_params.colour.transfer;
        c.matrix = m_params.colour.matrix;
        c.bitDepth = 10;
        c.rasterWidth = (uint32_t)m_params.rasterW;
        c.rasterHeight = (uint32_t)m_params.rasterH;
        strncpy(c.modeName, m_params.modeName.c_str(), sizeof c.modeName - 1);
        std::vector<uint8_t> ps = m_enc.parameterSets();
        c.csdLength = (uint32_t)ps.size();
        return std::make_shared<std::vector<uint8_t>>(makeMessage(MsgType::StreamConfig, c, ps.data(), ps.size()));
    }

    const Config& m_cfg;
    StreamServer m_server;
    std::mutex m_mutex;
    std::mutex m_emitMutex;   // serializes emission so encodeIndex order == wire order
    std::condition_variable m_cv;
    std::deque<std::shared_ptr<Job>> m_queue;
    std::map<uint64_t, std::shared_ptr<Job>> m_jobs;
    std::thread m_worker;
    std::atomic<bool> m_forceKey{true};
    std::atomic<int> m_bitrateMbps{20};

    // worker-owned
    FrameConverter m_conv;
    std::atomic<int> m_scopesHz{0};
    int64_t m_lastScopesNs = 0;
    HevcEncoder m_enc;
    ActiveAreaTracker m_area;
    StreamServer::Callbacks m_serverCb;
    StreamParams m_params;
    uint32_t m_configId = 0;
    uint64_t m_framesAnalysed = 0;
    vrltime::RollingStat m_convertMs{300};

    // encoder-callback-owned
    uint32_t m_announcedConfig = 0;
    uint32_t m_indexConfig = 0;
    uint32_t m_encodeIndex = 0;
    uint64_t m_emitted = 0;
    vrltime::RollingStat m_encodeMs{300};

    // guarded by m_mutex
    const ModeInfo* m_mode = nullptr;
    bool m_resetArea = true;
    bool m_running = false;
    uint32_t m_run = 0;
    int64_t m_anchorHost = 0, m_anchorStream = 0;
    double m_speed = 1.0;
    std::vector<Held> m_held;
    MsgPtr m_configMsg;
    uint64_t m_dropped = 0, m_copied = 0;
    int m_detached = 0;                                        // jobs running from a private copy
    std::vector<std::shared_ptr<std::vector<uint8_t>>> m_spare; // reusable copy buffers
    vrltime::RollingStat m_lead{600};
};

}  // namespace

FrameSink* createStreamPipeline() { return new StreamPipeline(); }

}  // namespace vrl
