// Hardware HEVC decode (Qualcomm c2.qti.hevc.decoder) into an AImageReader, so the renderer can pick exactly
// which decoded frame to show on each display refresh.
#pragma once
#include <android/hardware_buffer.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>

#include <atomic>
#include <map>
#include <mutex>
#include <thread>

#include "net.h"

namespace vrq {

struct FrameInfo {
    uint64_t seq = 0;
    uint32_t configId = 0;
    int64_t ptsSender = 0;
    int64_t durationNs = 0;
    bool sync = false;
    bool noDisplay = false;
    uint32_t timecode = 0xFFFFFFFF;
    int64_t rxNs = 0;
    int64_t queuedNs = 0;     // handed to the codec
    int64_t decodedNs = 0;    // released to the image reader
};

class Decoder {
public:
    explicit Decoder(NetClient& net) : m_net(net) {}
    void start();
    void stop();

    // Render thread: current reader + generation (changes on reconfiguration).
    AImageReader* reader(uint32_t& generation);
    // Render thread: look up (and remove) metadata for a decoded frame.
    bool takeInfo(uint64_t seq, FrameInfo& out);
    // Render thread: earliest sender pts among frames received but not decoded yet (INT64_MAX if none).
    int64_t earliestPendingPts();
    StreamConfig config();
    bool hasConfig() const { return m_hasConfig.load(); }
    double decodeMsP50();
    double decodeMsP95();
    // Old readers retired by reconfiguration; the render thread deletes them after dropping its images.
    void collectRetired(std::vector<AImageReader*>& out);
    // Render thread tells the decoder when frames are due, so decoding runs just ahead of display instead of
    // racing ~200 ms ahead (Resolve schedules early) and overflowing the image queue.
    void setTiming(int64_t playoutDelayNs, int64_t refreshNs) {
        m_delayNs = playoutDelayNs;
        m_refreshNs = refreshNs;
    }

private:
    void threadMain();
    bool configure(const StreamConfig& cfg);
    void releaseCodec();
    void drainOutput();

    NetClient& m_net;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    std::mutex m_mutex;
    AMediaCodec* m_codec = nullptr;
    AImageReader* m_reader = nullptr;
    std::vector<AImageReader*> m_retired;
    uint32_t m_generation = 0;
    StreamConfig m_cfg;
    std::atomic<bool> m_hasConfig{false};
    std::map<uint64_t, FrameInfo> m_info;
    bool m_needKey = true;
    uint32_t m_lastIndex = 0;
    uint32_t m_lastConfig = 0;
    vrltime::RollingStat m_decodeMs{240};
    std::atomic<int64_t> m_delayNs{0};
    std::atomic<int64_t> m_refreshNs{13888889};
};

}  // namespace vrq
