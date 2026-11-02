#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "protocol.h"

namespace vrl {

using MsgPtr = std::shared_ptr<const std::vector<uint8_t>>;

// TCP server for headsets + UDP discovery responder. Runs inside the host app (Resolve) for its lifetime.
class StreamServer {
public:
    struct Callbacks {
        std::function<void()> keyframeNeeded;            // a client needs an IDR
        std::function<void(int)> clientCountChanged;
        std::function<MsgPtr()> currentConfig;           // STREAM_CONFIG for a newly connected client (may be null)
        std::function<MsgPtr()> currentPlayback;         // PLAYBACK state
        std::function<void(bool, int)> scopesWanted;     // a client turned scopes on (hz) / off
    };

    ~StreamServer() { stop(); }
    void start(int port, const std::string& name, Callbacks cb);
    void stop();
    void broadcastVideo(const MsgPtr& msg, bool keyframe, int64_t bitrateBps);
    void broadcastControl(const MsgPtr& msg);
    int clientCount() const { return m_clientCount.load(); }

private:
    class Client;
    void acceptLoop();
    void discoveryLoop();
    int64_t m_lastBindWarnNs = 0;
    void reap();

    int m_port = 0;
    std::string m_name;
    Callbacks m_cb;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_listenFd{-1};
    int m_udpFd = -1;
    std::thread m_acceptThread, m_discoveryThread;
    std::mutex m_mutex;
    std::vector<std::shared_ptr<Client>> m_clients;
    std::atomic<int> m_clientCount{0};
};

}  // namespace vrl
