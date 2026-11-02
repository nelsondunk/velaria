// Connection to the VR Link bridge on the Mac (newline-delimited JSON, TCP 47812): timeline, playhead and the
// Fusion macro controls of the clips under it.
#pragma once
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "json.h"

namespace vrq {

class BridgeClient {
public:
    void start();
    void stop();
    // Host to use (changes when the video link moves between USB and Wi-Fi). Empty = don't connect.
    void setHost(const std::string& host);
    bool connected() const { return m_connected.load(); }
    bool pop(Json& out);
    void send(const Json& msg);

private:
    void threadMain();
    bool session(const std::string& host);
    std::thread m_thread;
    std::atomic<bool> m_running{false}, m_connected{false};
    std::atomic<int> m_fd{-1};
    std::mutex m_mutex;
    std::string m_host;
    std::deque<Json> m_inbox;
};

}  // namespace vrq
