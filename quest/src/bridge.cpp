#include "bridge.h"

#include <android/log.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VRLink", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "VRLink", __VA_ARGS__)

namespace vrq {

static const int kBridgePort = 47812;

void BridgeClient::start() {
    m_running = true;
    m_thread = std::thread([this] { threadMain(); });
}

void BridgeClient::stop() {
    m_running = false;
    int fd = m_fd.exchange(-1);
    if (fd >= 0) ::shutdown(fd, SHUT_RDWR);
    if (m_thread.joinable()) m_thread.join();
    if (fd >= 0) ::close(fd);
}

void BridgeClient::setHost(const std::string& host) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (host == m_host) return;
    m_host = host;
    int fd = m_fd.load();
    if (fd >= 0) ::shutdown(fd, SHUT_RDWR);   // reconnect to the new host
}

bool BridgeClient::pop(Json& out) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_inbox.empty()) return false;
    out = std::move(m_inbox.front());
    m_inbox.pop_front();
    return true;
}

void BridgeClient::send(const Json& msg) {
    int fd = m_fd.load();
    if (fd < 0) return;
    std::string line = msg.dump() + "\n";
    size_t off = 0;
    while (off < line.size()) {
        ssize_t w = ::send(fd, line.data() + off, line.size() - off, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        off += (size_t)w;
    }
}

void BridgeClient::threadMain() {
    pthread_setname_np(pthread_self(), "VRLink.bridge");
    while (m_running) {
        std::string host;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            host = m_host;
        }
        if (host.empty() || !session(host)) {
            for (int i = 0; i < 20 && m_running; i++) usleep(100000);
        }
    }
}

bool BridgeClient::session(const std::string& host) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(kBridgePort);
    if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) {
        ::close(fd);
        return false;
    }
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int r = ::connect(fd, (sockaddr*)&a, sizeof a);
    if (r != 0 && errno != EINPROGRESS) {
        ::close(fd);
        return false;
    }
    if (r != 0) {
        pollfd p{fd, POLLOUT, 0};
        if (::poll(&p, 1, 1500) <= 0) {
            ::close(fd);
            return false;
        }
        int err = 0;
        socklen_t el = sizeof err;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
        if (err != 0) {
            ::close(fd);
            return false;
        }
    }
    fcntl(fd, F_SETFL, fl);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    m_fd = fd;
    {
        Json hello = Json::object();
        hello["cmd"] = "hello";
        send(hello);
    }
    std::string buf;
    char chunk[65536];
    bool gotAny = false;
    int64_t connectedAt = 0;
    while (m_running) {
        pollfd p{fd, POLLIN, 0};
        int pr = ::poll(&p, 1, 250);
        if (pr < 0 && errno != EINTR) break;
        if (pr <= 0) {
            connectedAt += 250;
            if (!gotAny && connectedAt > 4000) break;   // adb tunnel with nobody behind it
            continue;
        }
        ssize_t n = ::recv(fd, chunk, sizeof chunk, 0);
        if (n <= 0) break;
        if (!gotAny) {
            gotAny = true;
            m_connected = true;
            LOGI("bridge connected (%s)", host.c_str());
        }
        buf.append(chunk, (size_t)n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (line.empty()) continue;
            Json j;
            if (!Json::parse(line, j)) {
                LOGW("bridge: bad json (%zu bytes)", line.size());
                continue;
            }
            std::lock_guard<std::mutex> lk(m_mutex);
            m_inbox.push_back(std::move(j));
            if (m_inbox.size() > 200) m_inbox.pop_front();
        }
    }
    if (gotAny) LOGI("bridge disconnected");
    m_connected = false;
    int old = m_fd.exchange(-1);
    if (old >= 0) ::close(old);
    return gotAny;
}

}  // namespace vrq
