#include "Server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#include "Common.h"

namespace vrl {

using namespace vrlproto;

// ------------------------------------------------------------------------------------------------ Client

class StreamServer::Client : public std::enable_shared_from_this<Client> {
public:
    Client(int fd, std::string peer, StreamServer* server) : m_fd(fd), m_peer(std::move(peer)), m_server(server) {}
    ~Client() {
        close();
        if (m_reader.joinable()) m_reader.join();
        if (m_writer.joinable()) m_writer.join();
    }

    void start() {
        auto self = shared_from_this();
        m_reader = std::thread([self] { self->readLoop(); });
        m_writer = std::thread([self] { self->writeLoop(); });
    }

    void close() {
        bool was = m_alive.exchange(false);
        if (was) {
            ::shutdown(m_fd, SHUT_RDWR);
            m_cv.notify_all();
        }
    }

    bool alive() const { return m_alive.load(); }
    bool ready() const { return m_helloDone.load(); }

    void sendControl(const MsgPtr& m) {
        if (!m) return;
        std::lock_guard<std::mutex> lk(m_mutex);
        m_control.push_back(m);
        m_cv.notify_one();
    }

    // Returns true if this client needs a keyframe (queue was dropped).
    bool sendVideo(const MsgPtr& m, bool keyframe, int64_t bitrateBps) {
        if (!m_helloDone) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_needKey && !keyframe) return false;   // waiting for a decodable entry point
        // Allow ~1 s of video in flight (min 16 MB); beyond that the link can't keep up: drop to the next keyframe.
        size_t limit = (size_t)std::max<int64_t>(16 << 20, bitrateBps / 8);
        if (m_videoBytes + m->size() > limit && !keyframe) {
            m_dropEvents++;
            size_t dropped = m_video.size();
            m_video.clear();
            m_videoBytes = 0;
            m_needKey = true;
            VLOGW("client %s: send queue over %zu MB, dropped %zu frames, waiting for keyframe", m_peer.c_str(),
                  limit >> 20, dropped);
            return true;
        }
        if (keyframe) m_needKey = false;
        m_video.push_back(m);
        m_videoBytes += m->size();
        m_cv.notify_one();
        return false;
    }

    const std::string& peer() const { return m_peer; }

private:
    bool writeAll(const uint8_t* p, size_t n) {
        while (n > 0) {
            ssize_t w = ::send(m_fd, p, n, 0);
            if (w < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            p += w;
            n -= (size_t)w;
        }
        return true;
    }

    void writeLoop() {
        pthread_setname_np("VRLink.send");
        while (m_alive) {
            MsgPtr msg;
            bool isPong = false;
            PongMsg pong{};
            bool isVideo = false;
            {
                std::unique_lock<std::mutex> lk(m_mutex);
                m_cv.wait(lk, [&] { return !m_alive || !m_control.empty() || !m_pongs.empty() || !m_video.empty(); });
                if (!m_alive) break;
                if (!m_pongs.empty()) {
                    pong = m_pongs.front();
                    m_pongs.pop_front();
                    isPong = true;
                } else if (!m_control.empty()) {
                    msg = m_control.front();
                    m_control.pop_front();
                } else {
                    msg = m_video.front();
                    m_video.pop_front();
                    m_videoBytes -= msg->size();
                    isVideo = true;
                }
            }
            bool ok;
            if (isPong) {
                pong.t3 = nowNs();   // stamp as late as possible for an accurate clock exchange
                auto bytes = makeMessage(MsgType::Pong, pong);
                ok = writeAll(bytes.data(), bytes.size());
            } else {
                if (isVideo && msg->size() >= sizeof(MsgHeader) + sizeof(VideoMsg)) {
                    // Patch sentNs in a private copy-free way: the header is shared, so write it in two parts.
                    VideoMsg vm;
                    std::memcpy(&vm, msg->data() + sizeof(MsgHeader), sizeof vm);
                    vm.sentNs = nowNs();
                    ok = writeAll(msg->data(), sizeof(MsgHeader)) &&
                         writeAll(reinterpret_cast<const uint8_t*>(&vm), sizeof vm) &&
                         writeAll(msg->data() + sizeof(MsgHeader) + sizeof vm,
                                  msg->size() - sizeof(MsgHeader) - sizeof vm);
                    m_bytesSent += msg->size();
                } else {
                    ok = writeAll(msg->data(), msg->size());
                }
            }
            if (!ok) {
                VLOGI("client %s: send failed (%s)", m_peer.c_str(), strerror(errno));
                close();
                break;
            }
        }
    }

    void readLoop() {
        pthread_setname_np("VRLink.recv");
        StreamParser parser;
        std::vector<uint8_t> buf(64 * 1024);
        while (m_alive) {
            ssize_t r = ::recv(m_fd, buf.data(), buf.size(), 0);
            int64_t rxNs = nowNs();
            if (r <= 0) {
                if (r < 0 && errno == EINTR) continue;
                break;
            }
            parser.feed(buf.data(), (size_t)r);
            StreamParser::Message m;
            while (parser.next(m)) handle(m, rxNs);
            if (parser.error()) {
                VLOGW("client %s: protocol error", m_peer.c_str());
                break;
            }
        }
        VLOGI("client %s disconnected (sent %.1f MB, %llu queue drops)", m_peer.c_str(), m_bytesSent / 1e6,
              (unsigned long long)m_dropEvents);
        close();
    }

    void handle(const StreamParser::Message& m, int64_t rxNs) {
        switch (m.type) {
            case MsgType::Hello: {
                HelloMsg h;
                if (!readBody(m.payload, h) || h.magic != kMagic) {
                    VLOGW("client %s: bad hello", m_peer.c_str());
                    close();
                    return;
                }
                char name[33] = {0};
                std::memcpy(name, h.name, 32);
                VLOGI("client %s hello: '%s' v%u max %ux%u", m_peer.c_str(), name, h.version, h.maxWidth, h.maxHeight);
                if (m_server->m_cb.currentConfig) sendControl(m_server->m_cb.currentConfig());
                if (m_server->m_cb.currentPlayback) sendControl(m_server->m_cb.currentPlayback());
                {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    m_needKey = true;
                }
                m_helloDone = true;
                if (m_server->m_cb.keyframeNeeded) m_server->m_cb.keyframeNeeded();
                break;
            }
            case MsgType::Ping: {
                PingMsg p;
                if (!readBody(m.payload, p)) return;
                PongMsg pong{};
                pong.id = p.id;
                pong.t1 = p.t1;
                pong.t2 = rxNs;
                std::lock_guard<std::mutex> lk(m_mutex);
                m_pongs.push_back(pong);
                m_cv.notify_one();
                break;
            }
            case MsgType::KeyframeReq: {
                {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    m_needKey = true;
                }
                if (m_server->m_cb.keyframeNeeded) m_server->m_cb.keyframeNeeded();
                break;
            }
            case MsgType::Stats: {
                StatsMsg s;
                if (!readBody(m.payload, s)) return;
                if (rxNs - m_lastStatsLog > 10LL * 1000000000LL) {
                    m_lastStatsLog = rxNs;
                    VLOGI("headset %s: shown=%u skipped=%u cadenceErr=%u holds=%u delay=%dms latency p50=%d p95=%d ms "
                          "rtt=%dus %.0fHz decode=%.1fms %ukbps",
                          m_peer.c_str(), s.framesShown, s.framesSkipped, s.cadenceErrors, s.holds, s.playoutDelayMs,
                          s.latencyP50Ms, s.latencyP95Ms, s.clockRttUs, s.refreshHz, s.decodeMsP50, s.bitrateKbps);
                }
                break;
            }
            case MsgType::ScopesReq: {
                ScopesReqMsg r;
                if (!readBody(m.payload, r)) return;
                VLOGI("client %s scopes %s (%u Hz)", m_peer.c_str(), r.enable ? "on" : "off", r.hz);
                if (m_server->m_cb.scopesWanted) m_server->m_cb.scopesWanted(r.enable != 0, (int)r.hz);
                break;
            }
            case MsgType::Bye:
                close();
                break;
            default:
                break;
        }
    }

    int m_fd;
    std::string m_peer;
    StreamServer* m_server;
    std::atomic<bool> m_alive{true};
    std::atomic<bool> m_helloDone{false};
    std::thread m_reader, m_writer;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<MsgPtr> m_control;
    std::deque<PongMsg> m_pongs;
    std::deque<MsgPtr> m_video;
    size_t m_videoBytes = 0;
    bool m_needKey = true;
    uint64_t m_dropEvents = 0;
    uint64_t m_bytesSent = 0;
    int64_t m_lastStatsLog = 0;
};

// ------------------------------------------------------------------------------------------------ server

void StreamServer::start(int port, const std::string& name, Callbacks cb) {
    if (m_running) return;
    m_port = port;
    m_name = name;
    m_cb = std::move(cb);
    m_running = true;
    m_acceptThread = std::thread([this] { acceptLoop(); });
    m_discoveryThread = std::thread([this] { discoveryLoop(); });
}

void StreamServer::stop() {
    if (!m_running.exchange(false)) return;
    if (m_listenFd >= 0) ::shutdown(m_listenFd, SHUT_RDWR);
    if (m_acceptThread.joinable()) m_acceptThread.join();
    if (m_discoveryThread.joinable()) m_discoveryThread.join();
    std::vector<std::shared_ptr<Client>> cs;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        cs.swap(m_clients);
    }
    for (auto& c : cs) c->close();
    cs.clear();
}

void StreamServer::acceptLoop() {
    pthread_setname_np("VRLink.accept");
    while (m_running) {
        if (m_listenFd < 0) {
            int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            fcntl(fd, F_SETFD, FD_CLOEXEC);   // don't leak into Resolve's helper processes (fuscript)
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons((uint16_t)m_port);
            a.sin_addr.s_addr = htonl(INADDR_ANY);
            if (::bind(fd, (sockaddr*)&a, sizeof a) != 0 || ::listen(fd, 8) != 0) {
                int err = errno;
                int64_t now = nowNs();
                if (!m_lastBindWarnNs || now - m_lastBindWarnNs > 30LL * 1000000000LL) {
                    m_lastBindWarnNs = now;
                    VLOGW("cannot listen on port %d (%s)%s; retrying every 2 s", m_port, strerror(err),
                          err == EADDRINUSE ? " - another app is holding it (`lsof -nP -iTCP:47810` shows which)" : "");
                }
                ::close(fd);
                for (int i = 0; i < 20 && m_running; i++) usleep(100000);
                continue;
            }
            m_listenFd = fd;
            m_lastBindWarnNs = 0;
            VLOGI("listening for headsets on TCP %d", m_port);
        }
        pollfd p{m_listenFd, POLLIN, 0};
        int pr = ::poll(&p, 1, 250);
        reap();
        if (pr <= 0) continue;
        sockaddr_in peer{};
        socklen_t len = sizeof peer;
        int cfd = ::accept(m_listenFd, (sockaddr*)&peer, &len);
        if (cfd < 0) continue;
        fcntl(cfd, F_SETFD, FD_CLOEXEC);
        int one = 1;
        setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        setsockopt(cfd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
        setsockopt(cfd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
        int sndbuf = 8 << 20;
        setsockopt(cfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof sndbuf);
        char ip[64];
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
        std::string peerName = std::string(ip) + ":" + std::to_string(ntohs(peer.sin_port));
        VLOGI("headset connected from %s", peerName.c_str());
        auto c = std::make_shared<Client>(cfd, peerName, this);
        c->start();
        int n;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_clients.push_back(c);
            n = (int)m_clients.size();
        }
        m_clientCount = n;
        if (m_cb.clientCountChanged) m_cb.clientCountChanged(n);
    }
    if (m_listenFd >= 0) ::close(m_listenFd);
    m_listenFd = -1;
}

void StreamServer::reap() {
    std::vector<std::shared_ptr<Client>> dead;
    int n;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto it = m_clients.begin(); it != m_clients.end();) {
            if (!(*it)->alive()) {
                dead.push_back(*it);
                it = m_clients.erase(it);
            } else
                ++it;
        }
        n = (int)m_clients.size();
    }
    if (!dead.empty()) {
        dead.clear();   // joins threads outside the lock
        m_clientCount = n;
        if (m_cb.clientCountChanged) m_cb.clientCountChanged(n);
    }
}

void StreamServer::discoveryLoop() {
    pthread_setname_np("VRLink.discovery");
    while (m_running) {
        if (m_udpFd < 0) {
            int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
            fcntl(fd, F_SETFD, FD_CLOEXEC);
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(kDiscoveryPort);
            a.sin_addr.s_addr = htonl(INADDR_ANY);
            if (::bind(fd, (sockaddr*)&a, sizeof a) != 0) {
                VLOGW("discovery bind failed (%s)", strerror(errno));
                ::close(fd);
                for (int i = 0; i < 50 && m_running; i++) usleep(100000);
                continue;
            }
            m_udpFd = fd;
        }
        pollfd p{m_udpFd, POLLIN, 0};
        if (::poll(&p, 1, 250) <= 0) continue;
        DiscoveryRequest req{};
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        ssize_t r = ::recvfrom(m_udpFd, &req, sizeof req, 0, (sockaddr*)&from, &fl);
        if (r != (ssize_t)sizeof req || req.magic != kMagic || req.kind != 1) continue;
        if (m_listenFd < 0) continue;   // not actually accepting headsets: let the instance that is answer
        DiscoveryReply rep{};
        rep.magic = kMagic;
        rep.kind = 2;
        rep.tcpPort = (uint16_t)m_port;
        rep.version = kVersion;
        strncpy(rep.name, m_name.c_str(), sizeof rep.name - 1);
        ::sendto(m_udpFd, &rep, sizeof rep, 0, (sockaddr*)&from, fl);
        char ip[64];
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
        VLOGD("discovery reply to %s", ip);
    }
    if (m_udpFd >= 0) ::close(m_udpFd);
    m_udpFd = -1;
}

void StreamServer::broadcastVideo(const MsgPtr& msg, bool keyframe, int64_t bitrateBps) {
    std::vector<std::shared_ptr<Client>> cs;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        cs = m_clients;
    }
    bool needKey = false;
    for (auto& c : cs)
        if (c->alive()) needKey |= c->sendVideo(msg, keyframe, bitrateBps);
    if (needKey && m_cb.keyframeNeeded) m_cb.keyframeNeeded();
}

void StreamServer::broadcastControl(const MsgPtr& msg) {
    std::vector<std::shared_ptr<Client>> cs;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        cs = m_clients;
    }
    for (auto& c : cs)
        if (c->alive() && c->ready()) c->sendControl(msg);
}

}  // namespace vrl
