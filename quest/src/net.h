// Headset side of the VR Link protocol: finds the Mac (USB tunnel first, then Wi-Fi discovery), keeps the
// clock in sync, and queues stream configs and encoded frames for the decoder.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "protocol.h"
#include "timing.h"

namespace vrq {

struct VideoUnit {
    uint32_t configId = 0;
    uint64_t seq = 0;
    int64_t ptsSender = 0;
    int64_t durationNs = 0;
    bool key = false;
    bool sync = false;
    bool noDisplay = false;
    uint32_t encodeIndex = 0;
    uint32_t timecode = 0xFFFFFFFF;
    int64_t rxNs = 0;          // local CLOCK_MONOTONIC when fully received
    std::vector<uint8_t> au;
};

struct StreamConfig {
    vrlproto::StreamConfigMsg msg{};
    std::vector<uint8_t> csd;
};

struct PlaybackState {
    bool playing = false;
    int64_t stopPtsSender = 0;   // drop timed frames after this sender time (0 = none)
    uint32_t generation = 0;
};

// The latest scope raster from the Mac (layout in protocol.h): measured on the uncompressed frame.
struct ScopesFrame {
    std::vector<uint8_t> raster;   // kScopesW * kScopesH, empty until the first one arrives
    uint32_t seq = 0;              // bumps per raster
    uint32_t timecodeBCD = 0xFFFFFFFF;
    uint32_t samples = 0;
    int64_t receivedNs = 0;
};

int64_t monoNs();

class NetClient {
public:
    void start(const std::string& lastHost, const std::string& deviceName);
    void stop();
    // Which Mac-side source to follow: "auto" (Resolve's virtual card if it answers, else any), "resolve"
    // (TCP 47810 only) or "ndi" (the NDI helper on TCP 47813: Premiere, After Effects...). Changing it drops
    // the current session so the next connect honours it.
    void setSource(const std::string& pref);
    std::string serverName();

    // consumer side (decoder thread)
    bool popConfig(StreamConfig& out);
    bool waitVideo(VideoUnit& out, int timeoutMs);
    void requestKeyframe();
    void sendStats(const vrlproto::StatsMsg& s);
    // Scopes: ask the sender to measure (sent again on every connect while wanted); read the latest raster.
    void wantScopes(bool on, int hz = 10);
    bool scopes(ScopesFrame& out, uint32_t haveSeq);   // true if newer than haveSeq

    // clock (any thread)
    bool clockValid();
    int64_t senderToLocal(int64_t senderNs);
    int64_t rttNs();

    // status (any thread)
    std::string statusLine();
    std::string host();
    bool connected() const { return m_fd.load() >= 0 && m_helloSent.load(); }
    bool viaUsb() const { return m_usb.load(); }
    PlaybackState playback();
    double receivedMbps();

private:
    void threadMain();
    bool tryConnect(const std::string& ip, int port, int timeoutMs);
    bool discover(std::string& ip, int& port, std::string& name, int timeoutMs);
    bool portAllowed(int port);
    std::vector<int> usbPorts();
    bool session();   // true if the peer actually spoke (a dead adb tunnel accepts then closes)
    bool sendBytes(const std::vector<uint8_t>& b);
    void handle(const vrlproto::StreamParser::Message& m, int64_t rx);
    void setStatus(const std::string& s);

    std::atomic<bool> m_running{false};
    std::thread m_thread;
    std::atomic<int> m_fd{-1};
    std::atomic<bool> m_helloSent{false};
    std::atomic<bool> m_usb{false};
    std::string m_lastHost, m_deviceName;
    std::mutex m_prefMutex;
    std::string m_source = "auto";
    int m_connectedPort = 0;

    std::mutex m_sendMutex;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<StreamConfig> m_configs;
    std::deque<VideoUnit> m_video;
    ScopesFrame m_scopes;
    std::atomic<bool> m_scopesWanted{false};
    std::atomic<int> m_scopesHz{10};
    vrltime::ClockSync m_clock;
    std::string m_status = "Starting";
    std::string m_host, m_serverName;
    PlaybackState m_playback;
    uint64_t m_bytes = 0;
    int64_t m_rateStart = 0;
    double m_mbps = 0;
    uint32_t m_pingId = 0;
};

}  // namespace vrq
