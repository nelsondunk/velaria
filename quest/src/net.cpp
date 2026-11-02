#include "net.h"

#include <android/log.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <algorithm>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VRLink", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "VRLink", __VA_ARGS__)

namespace vrq {

using namespace vrlproto;

int64_t monoNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void NetClient::start(const std::string& lastHost, const std::string& deviceName) {
    m_lastHost = lastHost;
    m_deviceName = deviceName;
    m_running = true;
    m_thread = std::thread([this] { threadMain(); });
}

void NetClient::stop() {
    m_running = false;
    int fd = m_fd.exchange(-1);
    if (fd >= 0) ::shutdown(fd, SHUT_RDWR);
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
    if (fd >= 0) ::close(fd);
}

void NetClient::setStatus(const std::string& s) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (s != m_status) LOGI("status: %s", s.c_str());
    m_status = s;
}

std::string NetClient::statusLine() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_status;
}

std::string NetClient::host() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_host;
}

std::string NetClient::serverName() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_serverName;
}

void NetClient::setSource(const std::string& pref) {
    {
        std::lock_guard<std::mutex> lk(m_prefMutex);
        if (pref == m_source) return;
        m_source = pref;
    }
    LOGI("source preference: %s", pref.c_str());
    // drop the current session (if it doesn't match any more) and let threadMain reconnect
    int port = m_connectedPort;
    if (port && !portAllowed(port)) {
        int fd = m_fd.load();
        if (fd >= 0) ::shutdown(fd, SHUT_RDWR);
    }
}

bool NetClient::portAllowed(int port) {
    std::lock_guard<std::mutex> lk(m_prefMutex);
    if (m_source == "resolve") return port == kTcpPort;
    if (m_source == "ndi") return port == kNdiTcpPort;
    return true;
}

// Ports to try on the USB tether (adb reverse maps both), in order of preference.
std::vector<int> NetClient::usbPorts() {
    std::lock_guard<std::mutex> lk(m_prefMutex);
    if (m_source == "resolve") return {kTcpPort};
    if (m_source == "ndi") return {kNdiTcpPort};
    return {kTcpPort, kNdiTcpPort};
}

PlaybackState NetClient::playback() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_playback;
}

double NetClient::receivedMbps() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_mbps;
}

bool NetClient::clockValid() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_clock.valid();
}

int64_t NetClient::senderToLocal(int64_t senderNs) {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_clock.senderToReceiver(senderNs, monoNs());
}

int64_t NetClient::rttNs() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_clock.lastRttNs();
}

bool NetClient::popConfig(StreamConfig& out) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_configs.empty()) return false;
    out = std::move(m_configs.front());
    m_configs.pop_front();
    return true;
}

bool NetClient::waitVideo(VideoUnit& out, int timeoutMs) {
    std::unique_lock<std::mutex> lk(m_mutex);
    if (m_video.empty())
        m_cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [this] { return !m_video.empty() || !m_configs.empty() || !m_running; });
    if (m_video.empty()) return false;
    out = std::move(m_video.front());
    m_video.pop_front();
    return true;
}

bool NetClient::sendBytes(const std::vector<uint8_t>& b) {
    std::lock_guard<std::mutex> lk(m_sendMutex);
    int fd = m_fd.load();
    if (fd < 0) return false;
    size_t off = 0;
    while (off < b.size()) {
        ssize_t w = ::send(fd, b.data() + off, b.size() - off, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += (size_t)w;
    }
    return true;
}

void NetClient::requestKeyframe() {
    if (!connected()) return;
    uint8_t dummy = 0;
    std::vector<uint8_t> m;
    appendMessage(m, MsgType::KeyframeReq, &dummy, 0);
    sendBytes(m);
}

void NetClient::sendStats(const StatsMsg& s) {
    if (!connected()) return;
    sendBytes(makeMessage(MsgType::Stats, s));
}

void NetClient::wantScopes(bool on, int hz) {
    bool was = m_scopesWanted.exchange(on);
    m_scopesHz = hz;
    if (!connected() || was == on) return;   // only a change goes on the wire (a new session re-sends it)
    ScopesReqMsg r{};
    r.enable = on ? 1 : 0;
    r.hz = (uint32_t)hz;
    sendBytes(makeMessage(MsgType::ScopesReq, r));
}

bool NetClient::scopes(ScopesFrame& out, uint32_t haveSeq) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_scopes.raster.empty() || m_scopes.seq == haveSeq) return false;
    out = m_scopes;
    return true;
}

bool NetClient::tryConnect(const std::string& ip, int port, int timeoutMs) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) {
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
        if (::poll(&p, 1, timeoutMs) <= 0) {
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
    // adb reverse accepts the local connection even when nothing listens on the Mac; prove the peer is alive by
    // completing the hello exchange in session() (a dead tunnel closes immediately).
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    int rcv = 8 << 20;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof rcv);
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
    m_fd = fd;
    return true;
}

bool NetClient::discover(std::string& ip, int& port, std::string& name, int timeoutMs) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    DiscoveryRequest req{kMagic, 1};
    std::vector<in_addr_t> targets = {htonl(INADDR_BROADCAST)};
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) == 0) {
        for (ifaddrs* i = ifs; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
            if (!(i->ifa_flags & IFF_BROADCAST) || !(i->ifa_flags & IFF_UP) || !i->ifa_broadaddr) continue;
            targets.push_back(((sockaddr_in*)i->ifa_broadaddr)->sin_addr.s_addr);
        }
        freeifaddrs(ifs);
    }
    for (auto t : targets) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(kDiscoveryPort);
        a.sin_addr.s_addr = t;
        ::sendto(fd, &req, sizeof req, 0, (sockaddr*)&a, sizeof a);
    }
    // Collect every answer (Resolve's card and the NDI helper both reply) and pick by preference: in "auto"
    // the virtual card wins if it answers within the window, otherwise whatever came first.
    struct Found { std::string ip, name; int port; };
    std::vector<Found> found;
    int64_t deadline = monoNs() + (int64_t)timeoutMs * 1000000;
    while (monoNs() < deadline && m_running) {
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 100) <= 0) continue;
        DiscoveryReply rep{};
        sockaddr_in from{};
        socklen_t fl = sizeof from;
        ssize_t r = ::recvfrom(fd, &rep, sizeof rep, 0, (sockaddr*)&from, &fl);
        if (r != (ssize_t)sizeof rep || rep.magic != kMagic || rep.kind != 2) continue;
        if (!portAllowed(rep.tcpPort)) continue;
        char buf[64];
        inet_ntop(AF_INET, &from.sin_addr, buf, sizeof buf);
        Found f{buf, std::string(rep.name, strnlen(rep.name, sizeof rep.name)), rep.tcpPort};
        bool dup = false;
        for (auto& g : found) dup = dup || (g.ip == f.ip && g.port == f.port);
        if (!dup) found.push_back(f);
        // a preferred/exclusive answer ends the wait early; otherwise give the other source 300 ms to show up
        if (f.port == kTcpPort || usbPorts().size() == 1) break;
        deadline = std::min<int64_t>(deadline, monoNs() + 300000000LL);
    }
    ::close(fd);
    if (found.empty()) return false;
    const Found* best = &found[0];
    for (auto& f : found)
        if (f.port == kTcpPort) best = &f;
    ip = best->ip;
    port = best->port;
    name = best->name;
    return true;
}

void NetClient::threadMain() {
    while (m_running) {
        bool ok = false;
        // 1) USB tether: `adb reverse` makes the Mac's ports reachable on localhost (47810 = Resolve's virtual
        //    card, 47813 = the NDI helper). A dead tunnel accepts the connection and closes at once; session()
        //    reports that as a failure and the next port / path is tried.
        for (int port : usbPorts()) {
            if (!m_running || ok) break;
            if (tryConnect("127.0.0.1", port, 300)) {
                m_usb = true;
                m_connectedPort = port;
                {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    m_host = "USB";
                    m_serverName = port == kNdiTcpPort ? "NDI" : "Resolve";
                }
                ok = session();
            }
        }
        if (!m_running) break;
        // 2) Wi-Fi: last known host (on the preferred port), then broadcast discovery.
        if (!ok && !m_lastHost.empty()) {
            for (int port : usbPorts()) {
                if (!m_running || ok) break;
                if (tryConnect(m_lastHost, port, 500)) {
                    m_usb = false;
                    m_connectedPort = port;
                    {
                        std::lock_guard<std::mutex> lk(m_mutex);
                        m_host = m_lastHost;
                        m_serverName = port == kNdiTcpPort ? "NDI" : "Resolve";
                    }
                    ok = session();
                }
            }
        }
        if (!m_running) break;
        if (!ok) {
            setStatus("Looking for the Mac (USB or Wi-Fi)...");
            std::string ip, name;
            int port = 0;
            if (discover(ip, port, name, 900) && tryConnect(ip, port, 1000)) {
                m_usb = false;
                m_lastHost = ip;
                m_connectedPort = port;
                {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    m_host = ip;
                    m_serverName = name;
                }
                ok = session();
            }
        }
        m_connectedPort = 0;
        if (!m_running) break;
        if (!ok) usleep(300000);
        else usleep(200000);
    }
}

// While streaming over Wi-Fi, keep checking whether the USB path has come up (cable plugged in and the Mac's
// tether helper has run `adb reverse`). A probe only counts if the server itself answers a ping: adb accepts the
// local connection even when nothing listens on the Mac side.
struct UsbProbe {
    int fd = -1;
    bool connecting = false;
    int64_t startedNs = 0;
    void reset() {
        if (fd >= 0) ::close(fd);
        fd = -1;
        connecting = false;
    }
    void start(int port) {
        reset();
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons((uint16_t)port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int r = ::connect(fd, (sockaddr*)&a, sizeof a);
        if (r != 0 && errno != EINPROGRESS) {
            reset();
            return;
        }
        connecting = true;
        startedNs = monoNs();
    }
    // returns true once the Mac answered over USB
    bool step(short revents) {
        if (fd < 0) return false;
        if (monoNs() - startedNs > 1500000000LL) {
            reset();
            return false;
        }
        if (connecting && (revents & (POLLOUT | POLLERR | POLLHUP))) {
            int err = 0;
            socklen_t el = sizeof err;
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el);
            if (err != 0) {
                reset();
                return false;
            }
            connecting = false;
            auto ping = makeMessage(MsgType::Ping, PingMsg{0x7fffffffu, monoNs()});
            if (::send(fd, ping.data(), ping.size(), MSG_NOSIGNAL) != (ssize_t)ping.size()) reset();
            return false;
        }
        if (!connecting && (revents & (POLLIN | POLLERR | POLLHUP))) {
            uint8_t b[64];
            ssize_t r = ::recv(fd, b, sizeof b, 0);
            reset();
            return r > 0;
        }
        return false;
    }
};

bool NetClient::session() {
    int fd = m_fd.load();
    if (fd < 0) return false;
    HelloMsg h{};
    h.magic = kMagic;
    h.version = kVersion;
    h.maxWidth = 8192;
    h.maxHeight = 8192;
    strncpy(h.name, m_deviceName.c_str(), sizeof h.name - 1);
    if (!sendBytes(makeMessage(MsgType::Hello, h))) {
        m_fd = -1;
        ::close(fd);
        return false;
    }
    m_helloSent = true;
    if (m_scopesWanted.load()) {
        ScopesReqMsg r{};
        r.enable = 1;
        r.hz = (uint32_t)m_scopesHz.load();
        sendBytes(makeMessage(MsgType::ScopesReq, r));
    }
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_clock.reset();
        m_rateStart = monoNs();
        m_bytes = 0;
    }
    StreamParser parser;
    std::vector<uint8_t> buf(256 * 1024);
    UsbProbe probe;
    int64_t nextProbe = monoNs() + 2000000000LL;
    int64_t nextPing = 0;
    int pingsSent = 0;
    bool gotAny = false;
    int64_t connectedAt = monoNs();
    while (m_running) {
        int64_t now = monoNs();
        if (now >= nextPing) {
            PingMsg p{++m_pingId, monoNs()};
            sendBytes(makeMessage(MsgType::Ping, p));
            pingsSent++;
            nextPing = now + (pingsSent < 12 ? 50000000LL : 250000000LL);   // converge fast, then 4 Hz
        }
        if (!m_usb && gotAny && probe.fd < 0 && now >= nextProbe) {
            probe.start(m_connectedPort ? m_connectedPort : kTcpPort);
            nextProbe = now + 2000000000LL;
        }
        pollfd p[2] = {{fd, POLLIN, 0}, {probe.fd, (short)(probe.connecting ? POLLOUT : POLLIN), 0}};
        int pr = ::poll(p, probe.fd >= 0 ? 2 : 1, 20);
        if (pr < 0 && errno != EINTR) break;
        if (probe.fd >= 0 && probe.step(pr > 0 ? p[1].revents : 0)) {
            LOGI("USB link to the Mac is up: switching from Wi-Fi");
            setStatus("Switching to USB...");
            break;   // threadMain tries USB first
        }
        if (pr <= 0 || !(p[0].revents & (POLLIN | POLLERR | POLLHUP))) {
            if (!gotAny && monoNs() - connectedAt > 3000000000LL) break;   // dead adb tunnel / wrong host
            continue;
        }
        ssize_t r = ::recv(fd, buf.data(), buf.size(), 0);
        int64_t rx = monoNs();
        if (r <= 0) break;
        if (!gotAny) setStatus("Connected to " + serverName() + (m_usb ? " (USB)" : " (Wi-Fi)"));
        gotAny = true;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_bytes += (uint64_t)r;
            if (rx - m_rateStart > 1000000000LL) {
                m_mbps = m_bytes * 8.0 / ((rx - m_rateStart) / 1e9) / 1e6;
                m_bytes = 0;
                m_rateStart = rx;
            }
        }
        parser.feed(buf.data(), (size_t)r);
        StreamParser::Message m;
        while (parser.next(m)) handle(m, rx);
        if (parser.error()) {
            LOGW("protocol error");
            break;
        }
    }
    probe.reset();
    m_helloSent = false;
    int old = m_fd.exchange(-1);
    if (old >= 0) ::close(old);
    if (gotAny) {
        setStatus("Disconnected - reconnecting...");
        LOGI("session ended");
    }
    return gotAny;
}

void NetClient::handle(const StreamParser::Message& m, int64_t rx) {
    switch (m.type) {
        case MsgType::Pong: {
            PongMsg p;
            if (!readBody(m.payload, p)) return;
            std::lock_guard<std::mutex> lk(m_mutex);
            m_clock.addSample(p.t1, p.t2, p.t3, rx);
            break;
        }
        case MsgType::StreamConfig: {
            StreamConfig c;
            const uint8_t* extra;
            size_t len;
            if (!readBody(m.payload, c.msg, &extra, &len)) return;
            c.csd.assign(extra, extra + std::min<size_t>(len, c.msg.csdLength));
            LOGI("stream config %u: %ux%u layout %u proj %u %.3f fps raster %ux%u", c.msg.configId, c.msg.width,
                 c.msg.height, c.msg.layout, c.msg.projection, (double)c.msg.fpsNum / c.msg.fpsDen, c.msg.rasterWidth,
                 c.msg.rasterHeight);
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_configs.push_back(std::move(c));
            }
            m_cv.notify_all();
            break;
        }
        case MsgType::Video: {
            VideoMsg v;
            const uint8_t* au;
            size_t len;
            if (!readBody(m.payload, v, &au, &len)) return;
            VideoUnit u;
            u.configId = v.configId;
            u.seq = v.frameSeq;
            u.ptsSender = v.ptsNs;
            u.durationNs = v.durationNs;
            u.key = (v.flags & kVideoKeyframe) != 0;
            u.sync = (v.flags & kVideoSync) != 0;
            u.noDisplay = (v.flags & kVideoNoDisplay) != 0;
            u.encodeIndex = v.encodeIndex;
            u.timecode = v.timecodeBCD;
            u.rxNs = rx;
            u.au.assign(au, au + len);
            bool overflow = false;
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                if (m_video.size() > 90) {   // decoder stalled: keep memory bounded, resync on a keyframe
                    m_video.clear();
                    overflow = true;
                }
                m_video.push_back(std::move(u));
            }
            m_cv.notify_all();
            if (overflow) {
                LOGW("video queue overflow; dropped backlog, requesting keyframe");
                requestKeyframe();
            }
            break;
        }
        case MsgType::Scopes: {
            ScopesMsg s;
            if (!readBody(m.payload, s)) return;
            size_t need = sizeof(ScopesMsg) + (size_t)s.width * s.height;
            if (s.width != kScopesW || s.height != kScopesH || s.layout != kScopesLayoutV1 || m.payload.size() < need) return;
            std::lock_guard<std::mutex> lk(m_mutex);
            m_scopes.raster.assign(m.payload.begin() + sizeof(ScopesMsg), m.payload.begin() + need);
            m_scopes.seq++;
            m_scopes.timecodeBCD = s.timecodeBCD;
            m_scopes.samples = s.sampleCount;
            m_scopes.receivedNs = rx;
            break;
        }
        case MsgType::Playback: {
            PlaybackMsg p;
            if (!readBody(m.payload, p)) return;
            std::lock_guard<std::mutex> lk(m_mutex);
            m_playback.playing = p.state == 1;
            if (!m_playback.playing && p.stopPtsNs) m_playback.stopPtsSender = p.stopPtsNs;
            if (m_playback.playing) m_playback.stopPtsSender = 0;
            m_playback.generation++;
            LOGI("playback %s", m_playback.playing ? "playing" : "stopped");
            break;
        }
        default:
            break;
    }
}

}  // namespace vrq
