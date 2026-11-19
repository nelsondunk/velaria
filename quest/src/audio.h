// Audio out (+65): Resolve's monitoring mix, as the Mac forwards it (AudioMsg), played through AAudio in step
// with the picture. Each block arrives with the local time its first sample is due (the sender clock mapping the
// video already uses, plus the same presentation delay the frames get), and the render callback lines the FIFO up
// with the stream's own output timestamp: silence until a block is due, a drop when sound has fallen behind.
// Stereo only; an ambisonic bus would need head-tracked decoding on top of this (see the roadmap).
#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

struct AAudioStreamStruct;

namespace vrq {

struct AudioBlock {
    int64_t dueLocalNs = 0;       // when the first sample should be heard, this device's monotonic clock
    bool discontinuity = false;   // does not follow the previous block (seek / stop / flush)
    uint32_t frames = 0;
    std::vector<int16_t> pcm;     // interleaved stereo s16
};

struct AudioStats {
    uint64_t blocks = 0, played = 0, silenceFrames = 0, droppedFrames = 0, underruns = 0, resets = 0;
    double skewMs = 0;            // last measured: + means sound was due before we could play it (behind)
    bool running = false;
};

class AudioOut {
public:
    bool start();                  // opens the AAudio stream; safe to call again (no-op while open)
    void stop();
    void push(AudioBlock&& b);     // any thread; blocks with a due time more than 2 s off are ignored
    void flush();                  // drop everything queued (stop, seek)
    AudioStats stats();
    bool running() const { return m_stream != nullptr; }

    // AAudio data callback entry (public for the C trampoline)
    int onRender(int16_t* out, int32_t frames);
    void onError();

private:
    void pullBlocksLocked();
    int64_t playTimeNs(int64_t framesAhead);

    AAudioStreamStruct* m_stream = nullptr;
    std::mutex m_mutex;
    std::deque<AudioBlock> m_queue;        // from the network thread
    // the FIFO the callback plays from: samples are contiguous from fifoDueNs
    std::vector<int16_t> m_fifo;
    size_t m_fifoPos = 0;
    int64_t m_fifoDueNs = 0;
    bool m_fifoValid = false;
    AudioStats m_stats;
    std::atomic<bool> m_errored{false};
};

}  // namespace vrq
