// Audio out (+65): Resolve's monitoring mix, as the Mac forwards it (AudioMsg), played through AAudio in step
// with the picture. Each block arrives with the local time its first sample is due (the sender clock mapping the
// video already uses, plus the same presentation delay the frames get), and the render callback lines the FIFO up
// with the stream's own output timestamp: silence until a block is due, a drop when sound has fallen behind.
// Stereo, or (+74) a first-order AmbiX bus (W Y Z X, ACN/SN3D) decoded to stereo in the render callback with the
// head's current orientation: the sound field is rotated by the inverse head pose, then a pair of cardioids at
// +-45 degrees makes the stereo pair, so a source stays put in the room as the head turns. Proper binaural (HRTF,
// higher orders) is the next step; this one proves the routing and the tracking.
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
    int64_t rxNs = 0;             // when it arrived (this device's clock); with ptsLocalNs it gives the arrival lateness
    int64_t ptsLocalNs = 0;       // the sender's pts mapped to this device's clock, before the presentation delay
    bool discontinuity = false;   // does not follow the previous block (seek / stop / flush)
    bool fromTap = false;         // the channels came from the Velaria Sound plugin (a Fairlight bus), not Decklink 1-4
    uint32_t frames = 0;
    uint16_t channels = 2;        // 2 stereo, 4 first-order AmbiX
    std::vector<int16_t> pcm;     // interleaved s16
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
    // The head's orientation in the room (OpenXR: x right, y up, -z forward), from the render thread each frame;
    // used only for 4-channel blocks.
    void setHead(float qx, float qy, float qz, float qw);

    // AAudio data callback entry (public for the C trampoline)
    int onRender(int16_t* out, int32_t frames);
    void onError();

private:
    void pullBlocksLocked();
    int64_t playTimeNs(int64_t framesAhead);
    void decodeGains(float g[2][4]);   // stereo gains for W Y Z X at the current head orientation

    AAudioStreamStruct* m_stream = nullptr;
    std::mutex m_mutex;
    std::deque<AudioBlock> m_queue;        // from the network thread
    // the FIFO the callback plays from: samples are contiguous from fifoDueNs
    std::vector<int16_t> m_fifo;
    int m_fifoChannels = 2;                // the stride of the FIFO's frames
    size_t m_fifoPos = 0;
    float m_head[4] = {0, 0, 0, 1};
    float m_lastGain[2][4] = {{0.5f, 0.3536f, 0, 0.3536f}, {0.5f, -0.3536f, 0, 0.3536f}};
    bool m_lastGainValid = false;
    int64_t m_fifoDueNs = 0;
    bool m_fifoValid = false;
    AudioStats m_stats;
    std::atomic<bool> m_errored{false};
};

}  // namespace vrq
