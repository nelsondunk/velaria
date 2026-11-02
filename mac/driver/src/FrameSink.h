#pragma once
#include "Common.h"
#include "DisplayModes.h"

namespace vrl {

// What the virtual card hands to the streaming pipeline.
struct ScheduledFrame {
    IDeckLinkVideoFrame* frame;    // valid for the duration of the call; AddRef to keep
    uint64_t seq;                  // monotonically increasing per device
    const ModeInfo* mode;          // active output mode
    int64_t streamTimeNs;          // scheduled display time in stream time (ns); -1 for sync frames
    int64_t durationNs;            // display duration (ns)
    bool sync;                     // came from DisplayVideoFrameSync (paused / scrubbing)
};

class FrameSink {
public:
    virtual ~FrameSink() = default;
    // Called on the client's thread when a frame is handed to the card (scheduled or sync-displayed).
    virtual void onFrame(const ScheduledFrame& f) = 0;
    // Stream-time -> host-time mapping changed (playback start). hostNs = anchorHostNs + (streamNs - anchorStreamNs)/speed
    // runGen increments on every StartScheduledPlayback; a stop carries the generation it stopped, so a late
    // stop notification can never cancel a newer start (Resolve restarts the stream right after stopping it).
    virtual void onPlaybackStarted(int64_t anchorHostNs, int64_t anchorStreamNs, double speed, uint32_t runGen) = 0;
    virtual void onPlaybackStopped(uint32_t runGen) = 0;
    // A scheduled frame reached its display time (virtual scan-out).
    virtual void onFrameDisplayed(uint64_t seq, int64_t hostNs, bool late) = 0;
    virtual void onOutputEnabled(const ModeInfo* mode) = 0;
    virtual void onOutputDisabled() = 0;
    virtual void onAudio(const void* samples, uint32_t frames, uint32_t channels, uint32_t bitsPerSample,
                         int64_t streamTimeNs) = 0;
};

FrameSink* createDefaultSink();   // provided by the pipeline (or the trace sink)

}  // namespace vrl
