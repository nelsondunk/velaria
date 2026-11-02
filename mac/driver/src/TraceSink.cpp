// Probe sink: logs what Resolve sends to the virtual card. Replaced by the streaming pipeline later.
#include "Common.h"
#include "FrameSink.h"
#include "Config.h"

namespace vrl {

namespace {
class TraceSink final : public FrameSink {
public:
    void onFrame(const ScheduledFrame& f) override {
        m_frames++;
        int64_t now = nowNs();
        if (m_frames <= 5 || m_frames % 240 == 0) {
            void* bytes = nullptr;
            f.frame->GetBytes(&bytes);
            IDeckLinkTimecode* tc = nullptr;
            char tcs[32] = "none";
            if (f.frame->GetTimecode(bmdTimecodeRP188Any, &tc) == S_OK && tc) {
                uint8_t h, m, s, fr;
                tc->GetComponents(&h, &m, &s, &fr);
                snprintf(tcs, sizeof tcs, "%02u:%02u:%02u:%02u", h, m, s, fr);
                tc->Release();
            }
            IDeckLinkVideoFrameMetadataExtensions* md = nullptr;
            int64_t cs = 0, eotf = -1;
            if (f.frame->QueryInterface(IID_IDeckLinkVideoFrameMetadataExtensions, (void**)&md) == S_OK && md) {
                md->GetInt(bmdDeckLinkFrameMetadataColorspace, &cs);
                md->GetInt(bmdDeckLinkFrameMetadataHDRElectroOpticalTransferFunc, &eotf);
                md->Release();
            }
            IDeckLinkVideoFrame3DExtensions* x3d = nullptr;
            bool has3d = f.frame->QueryInterface(IID_IDeckLinkVideoFrame3DExtensions, (void**)&x3d) == S_OK && x3d;
            if (x3d) x3d->Release();
            VLOGI("frame #%llu seq=%llu %ldx%ld rb=%ld pf=%s flags=0x%x bytes=%p tc=%s cs=%s eotf=%lld 3d=%d "
                  "stream=%.3fs dur=%.2fms sync=%d",
                  (unsigned long long)m_frames, (unsigned long long)f.seq, f.frame->GetWidth(), f.frame->GetHeight(),
                  f.frame->GetRowBytes(), fourcc(f.frame->GetPixelFormat()).c_str(), f.frame->GetFlags(), bytes, tcs,
                  fourcc((uint32_t)cs).c_str(), (long long)eotf, has3d, f.streamTimeNs / 1e9, f.durationNs / 1e6,
                  f.sync);
        }
        if (now - m_windowStart > 2000000000LL) {
            if (m_windowStart) VLOGI("rate: %.2f frames/s handed to card", (m_frames - m_windowFrames) * 1e9 / (now - m_windowStart));
            m_windowStart = now;
            m_windowFrames = m_frames;
        }
    }
    void onPlaybackStarted(int64_t anchorHostNs, int64_t anchorStreamNs, double speed, uint32_t) override {
        VLOGI("sink: playback started anchorStream=%.3fs speed=%.2f", anchorStreamNs / 1e9, speed);
    }
    void onPlaybackStopped(uint32_t gen) override { VLOGI("sink: playback stopped (run %u)", gen); }
    void onFrameDisplayed(uint64_t, int64_t, bool late) override {
        if (late) m_late++;
    }
    void onOutputEnabled(const ModeInfo* mode) override { VLOGI("sink: output enabled %s", mode->name); }
    void onOutputDisabled() override { VLOGI("sink: output disabled (late frames so far %llu)", (unsigned long long)m_late); }
    void onAudio(const void*, uint32_t frames, uint32_t channels, uint32_t bits, int64_t streamNs) override {
        m_audioCalls++;
        if (m_audioCalls <= 3 || m_audioCalls % 500 == 0)
            VLOGI("audio block %u frames x %u ch x %u bit at %.3fs", frames, channels, bits, streamNs / 1e9);
    }

private:
    uint64_t m_frames = 0, m_late = 0, m_audioCalls = 0, m_windowFrames = 0;
    int64_t m_windowStart = 0;
};
}  // namespace

FrameSink* createStreamPipeline();

FrameSink* createDefaultSink() {
    if (Config::get().trace) {
        VLOGI("trace mode: logging frames, not streaming");
        return new TraceSink();
    }
    return createStreamPipeline();
}

}  // namespace vrl
