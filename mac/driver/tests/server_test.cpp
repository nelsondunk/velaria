// The headset server (src/Server.cpp) against well-behaved and hostile peers. Runs on Linux and on a Mac; it needs
// no Resolve, no VideoToolbox and no headset. What it covers is the socket handling only:
//   a normal session (HELLO -> config + playback, PING -> PONG, video after a keyframe)
//   an oversized message, a peer that never says HELLO, more connections than the limit, a ping flood,
//   fast connect/disconnect churn (no leaked descriptors, no crash when a client object dies on its own thread),
//   and the USB-only (loopback) listener.
#include <pthread.h>
#include <sys/socket.h>
#ifndef __APPLE__
#define pthread_setname_np(n) pthread_setname_np(pthread_self(), n)
#define SO_NOSIGPIPE SO_KEEPALIVE
#endif
#ifndef VRL_HELLO_DEADLINE_NS
#define VRL_HELLO_DEADLINE_NS 600000000LL
#endif
#include "../src/Server.cpp"

#include <dirent.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace vrl {
void log(LogLevel lvl, const char* fmt, ...) {
    if (!getenv("SERVER_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
int64_t nowNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
}  // namespace vrl

using namespace vrl;
using namespace vrlproto;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, what)                                   \
    do {                                                    \
        if (cond) {                                         \
            g_pass++;                                       \
            printf("  ok   %s\n", what);                    \
        } else {                                            \
            g_fail++;                                       \
            printf("  FAIL %s  (%s:%d)\n", what, __FILE__, __LINE__); \
        }                                                   \
    } while (0)

static int openFds() {
    int n = 0;
#ifdef __APPLE__
    DIR* d = opendir("/dev/fd");
#else
    DIR* d = opendir("/proc/self/fd");
#endif
    if (!d) return -1;
    while (readdir(d)) n++;
    closedir(d);
    return n;
}

static int dial(int port, const char* ip = "127.0.0.1", int timeoutMs = 1000) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, ip, &a.sin_addr);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (::connect(fd, (sockaddr*)&a, sizeof a) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

static bool sendAll(int fd, const std::vector<uint8_t>& b) {
    size_t off = 0;
    while (off < b.size()) {
        ssize_t w = ::send(fd, b.data() + off, b.size() - off, MSG_NOSIGNAL);
        if (w <= 0) return false;
        off += (size_t)w;
    }
    return true;
}

static std::vector<uint8_t> hello() {
    HelloMsg h{};
    h.magic = kMagic;
    h.version = kVersion;
    strncpy(h.name, "test headset", sizeof h.name - 1);
    return makeMessage(MsgType::Hello, h);
}

// Reads messages until `want` arrives or the socket times out / closes. Returns 1 found, 0 closed by peer, -1 timeout.
static int readUntil(int fd, MsgType want, StreamParser::Message* out = nullptr) {
    StreamParser p;
    uint8_t buf[65536];
    for (;;) {
        ssize_t r = ::recv(fd, buf, sizeof buf, 0);
        if (r == 0) return 0;
        if (r < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? -1 : 0;
        p.feed(buf, (size_t)r);
        StreamParser::Message m;
        while (p.next(m))
            if (m.type == want) {
                if (out) *out = m;
                return 1;
            }
    }
}

static bool waitCount(StreamServer& s, int n, int ms = 3000) {
    for (int i = 0; i < ms / 10; i++) {
        if (s.clientCount() == n) return true;
        usleep(10000);
    }
    return s.clientCount() == n;
}

static int freePort() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, (sockaddr*)&a, sizeof a);
    socklen_t l = sizeof a;
    getsockname(fd, (sockaddr*)&a, &l);
    ::close(fd);
    return ntohs(a.sin_port);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    std::atomic<int> keyReqs{0};
    StreamServer::Callbacks cb;
    cb.keyframeNeeded = [&] { keyReqs++; };
    cb.currentConfig = [] {
        StreamConfigMsg c{};
        c.configId = 7;
        c.width = 3840;
        c.height = 1920;
        return std::make_shared<const std::vector<uint8_t>>(makeMessage(MsgType::StreamConfig, c));
    };
    cb.currentPlayback = [] {
        PlaybackMsg p{};
        return std::make_shared<const std::vector<uint8_t>>(makeMessage(MsgType::Playback, p));
    };

    int port = freePort();
    {
        StreamServer srv;
        srv.start(port, "test mac", cb);
        usleep(200000);

        printf("normal session\n");
        int fd = dial(port);
        CHECK(fd >= 0, "connects");
        sendAll(fd, hello());
        StreamParser::Message m;
        CHECK(readUntil(fd, MsgType::Playback, &m) == 1, "HELLO is answered with config and playback");
        CHECK(waitCount(srv, 1), "client counted");
        CHECK(keyReqs.load() == 1, "HELLO asks the encoder for a keyframe");
        sendAll(fd, makeMessage(MsgType::Ping, PingMsg{42, 1234}));
        CHECK(readUntil(fd, MsgType::Pong, &m) == 1, "PING is answered");
        PongMsg pong{};
        readBody(m.payload, pong);
        CHECK(pong.id == 42 && pong.t1 == 1234 && pong.t3 >= pong.t2, "PONG echoes id and t1, t3 >= t2");
        VideoMsg v{};
        v.configId = 7;
        std::vector<uint8_t> au(300000, 0xAB);
        auto delta = std::make_shared<const std::vector<uint8_t>>(makeMessage(MsgType::Video, v, au.data(), au.size()));
        srv.broadcastVideo(delta, false, 20000000);
        v.flags = kVideoKeyframe;
        v.frameSeq = 99;
        auto key = std::make_shared<const std::vector<uint8_t>>(makeMessage(MsgType::Video, v, au.data(), au.size()));
        srv.broadcastVideo(key, true, 20000000);
        CHECK(readUntil(fd, MsgType::Video, &m) == 1, "video arrives");
        VideoMsg got{};
        readBody(m.payload, got);
        CHECK(got.frameSeq == 99 && m.payload.size() == sizeof(VideoMsg) + au.size() && got.sentNs > 0,
              "the first video a client gets is the keyframe, whole, with sentNs stamped");
        ::close(fd);
        CHECK(waitCount(srv, 0), "disconnect is noticed");

        printf("oversized message\n");
        fd = dial(port);
        MsgHeader big{};
        big.length = 1024 * 1024;   // legal for the headset's parser, far beyond anything a headset sends
        big.type = (uint8_t)MsgType::Stats;
        std::vector<uint8_t> hb((uint8_t*)&big, (uint8_t*)&big + sizeof big);
        sendAll(fd, hb);
        CHECK(readUntil(fd, MsgType::Bye) == 0, "a 1 MB message from a 'headset' closes the connection");
        ::close(fd);

        printf("peer that never says HELLO\n");
        fd = dial(port, "127.0.0.1", 3000);
        CHECK(waitCount(srv, 1), "silent peer is connected");
        CHECK(readUntil(fd, MsgType::Bye) == 0, "and is closed after the HELLO deadline");
        ::close(fd);
        CHECK(waitCount(srv, 0), "and is gone");

        printf("a ping-only probe (the headset's USB check) is still answered before the deadline\n");
        fd = dial(port);
        sendAll(fd, makeMessage(MsgType::Ping, PingMsg{0x7fffffffu, 5}));
        CHECK(readUntil(fd, MsgType::Pong) == 1, "PONG without HELLO");
        ::close(fd);
        CHECK(waitCount(srv, 0), "probe gone");

        printf("connection limit\n");
        std::vector<int> fds;
        for (int i = 0; i < 14; i++) {
            int c = dial(port, "127.0.0.1", 300);
            if (c >= 0) {
                sendAll(c, hello());
                fds.push_back(c);
            }
            usleep(20000);
        }
        usleep(300000);
        CHECK(srv.clientCount() == 8, "no more than 8 connections are kept");
        int answered = 0;
        for (int c : fds)
            if (readUntil(c, MsgType::Playback) == 1) answered++;
        CHECK(answered == 8, "the first 8 are served, the rest refused");
        for (int c : fds) ::close(c);
        CHECK(waitCount(srv, 0), "all gone");

        printf("ping flood from a peer that never reads\n");
        fd = dial(port, "127.0.0.1", 200);
        sendAll(fd, hello());
        {
            std::vector<uint8_t> pings;
            PingMsg p{1, 1};
            for (int i = 0; i < 20000; i++) appendMessage(pings, MsgType::Ping, &p, sizeof p);
            for (int i = 0; i < 20; i++)
                if (!sendAll(fd, pings)) break;   // 400 000 pings, or until the server stops reading
        }
        int fd2 = dial(port);
        sendAll(fd2, hello());
        CHECK(readUntil(fd2, MsgType::Playback) == 1, "the server still serves others during a flood");
        ::close(fd);
        ::close(fd2);
        CHECK(waitCount(srv, 0), "flood peer gone");

        printf("connect / disconnect churn\n");
        usleep(400000);
        int before = openFds();
        for (int i = 0; i < 3000; i++) {
            int c = dial(port, "127.0.0.1", 200);
            if (c < 0) {
                usleep(2000);
                continue;
            }
            if (i % 3 == 0) sendAll(c, hello());
            if (i % 3 == 1) sendAll(c, makeMessage(MsgType::Ping, PingMsg{1, 1}));
            ::close(c);
        }
        CHECK(waitCount(srv, 0, 5000), "3000 short-lived connections all cleaned up");
        usleep(600000);
        int after = openFds();
        printf("       open descriptors: %d before, %d after\n", before, after);
        CHECK(before > 0 && after <= before + 2, "no descriptors leaked");
        srv.stop();
    }

    printf("USB-only listener\n");
    {
        StreamServer srv;
        int p2 = freePort();
        srv.start(p2, "test mac", cb, true);
        usleep(200000);
        int fd = dial(p2);
        CHECK(fd >= 0, "localhost connects");
        if (fd >= 0) ::close(fd);
        std::string lan;
        ifaddrs* ifs = nullptr;
        if (getifaddrs(&ifs) == 0) {
            for (ifaddrs* i = ifs; i; i = i->ifa_next) {
                if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK)) continue;
                char b[64];
                inet_ntop(AF_INET, &((sockaddr_in*)i->ifa_addr)->sin_addr, b, sizeof b);
                lan = b;
                break;
            }
            freeifaddrs(ifs);
        }
        if (lan.empty()) {
            printf("  skip the machine has no other IPv4 address to try\n");
        } else {
            int f = dial(p2, lan.c_str(), 500);
            CHECK(f < 0, ("the machine's network address (" + lan + ") is refused").c_str());
            if (f >= 0) ::close(f);
        }
        srv.stop();
    }
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
