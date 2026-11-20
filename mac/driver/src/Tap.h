// Velaria Sound receiver: the VST3 insert (mac/sound) copies a Fairlight bus's channels, with the host's timeline
// position, to UDP 127.0.0.1:47813. This keeps the last few seconds indexed by timeline sample, so onAudio can
// replace the Decklink channels in the headset's ambisonic message with the bus's own channels, aligned to the
// picture by timecode. Nothing here touches Resolve; if no plugin is running the receiver just idles.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace vrl {

constexpr uint16_t kTapPort = 47813;
constexpr uint32_t kTapMagic = 0x50415456;   // 'VTAP'
constexpr int kTapMaxChannels = 16;
constexpr uint32_t kTapMaxFrames = 512;        // the plugin splits its blocks at 512 frames (mac/sound/src/tap.cpp)

struct TapHeader {                           // == velaria::TapPacket in mac/sound/src/tap.h
    uint32_t magic;
    uint16_t version;
    uint16_t channels;
    uint32_t sampleRate;
    uint32_t flags;                          // 1 playing, 2 project time valid, 4 ambisonic (ACN)
    int64_t projectTime;                     // first frame of the payload, in samples on the host timeline
    uint32_t frames;
    uint32_t seq;
};

class TapReceiver {
public:
    TapReceiver() = default;
    ~TapReceiver() { stop(); }
    void start();
    void stop();

    // A packet arrived within the last 400 ms. channels/sampleRate are those of the latest packet.
    bool live(int64_t nowNs) const;
    int channels() const { return m_channels.load(); }
    uint32_t sampleRate() const { return m_rate.load(); }
    bool ambisonic() const { return m_ambi.load(); }
    bool hostPlaying() const { return m_playing.load(); }
    // Two live inserts of the same arrangement and channel count: the ring follows the first, but the choice is a
    // coin toss the user should hear about.
    bool ambiguous() const { return m_ambiguous.load(); }
    uint64_t rejected() const { return m_rejected.load(); }
    // Header checks a datagram must pass before anything in it is used (public so the test can hit it directly).
    static bool validHeader(const TapHeader& h, size_t datagramBytes);
    // What loop() does with a datagram that passed validHeader, for tests (no socket): choose() then store().
    void inject(uint32_t sourceKey, const TapHeader& h, const float* pcm, int64_t nowNs);
    void startForTest(int64_t capacityFrames);   // ring without the socket thread
    uint64_t packets() const { return m_packets.load(); }
    // Copy `frames` frames starting at timeline sample `at` into out (interleaved s16, outChannels wide; channels the
    // tap lacks are zero). Returns the number of frames that were actually present in the ring; the rest are zeroed.
    uint32_t read(int64_t at, uint32_t frames, int16_t* out, uint32_t outChannels) const;
    // The timeline sample range currently held: [first, end). Both -1 when empty.
    void held(int64_t& first, int64_t& end) const;

private:
    void loop();
    void store(const TapHeader& h, const float* pcm);
    void invalidate();                       // forget every held sample (source change)

    std::thread m_thread;
    std::atomic<bool> m_run{false};
    int m_sock = -1;
    mutable std::mutex m_mutex;
    // ring indexed by timeline sample: slot (t % capacity) holds frame t if m_stamp[slot] == t / capacity
    std::vector<float> m_ring;               // capacity * kTapMaxChannels
    std::vector<int32_t> m_stamp;            // per slot: which "lap" of the ring wrote it, -1 never
    int64_t m_capacity = 0;
    int64_t m_first = -1, m_end = -1;        // contiguous-ish range written, for diagnostics
    std::atomic<int> m_channels{0};
    std::atomic<uint32_t> m_rate{48000};
    std::atomic<bool> m_ambi{false}, m_playing{false};
    std::atomic<int64_t> m_lastNs{0};
    std::atomic<uint64_t> m_packets{0};
    std::atomic<uint64_t> m_rejected{0};
    std::atomic<bool> m_ambiguous{false};
    uint32_t m_lastSeq = 0;
    uint64_t m_lost = 0;
    // More than one Velaria Sound insert can be running (one per bus). Each has its own socket, so its source port
    // tells them apart; the ring follows one of them: an ambisonic bus over a speaker bus, else the one with more
    // channels, else whichever came first. Others are counted and ignored.
    struct Source { uint32_t key; int channels; bool ambi; int64_t lastNs; };
    std::vector<Source> m_sources;
    uint32_t m_chosen = 0;
    bool choose(uint32_t key, const TapHeader& h, int64_t now);
};

}  // namespace vrl
