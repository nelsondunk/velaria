#include "Tap.h"
#include "Common.h"

#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace vrl {

namespace {
constexpr int64_t kCapacityFrames = 48000 * 6;    // six seconds at 48 kHz; Fairlight's plugin runs well under that ahead
constexpr int64_t kLiveNs = 400000000LL;          // no packet for this long = the plugin is gone or the bus is idle
}

void TapReceiver::start() {
    if (m_run.exchange(true)) return;
    m_capacity = kCapacityFrames;
    m_ring.assign((size_t)(m_capacity * kTapMaxChannels), 0.0f);
    m_stamp.assign((size_t)m_capacity, -1);
    m_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_sock < 0) { VLOGW("tap: no socket (%s)", strerror(errno)); m_run = false; return; }
    int one = 1;
    setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    int rcv = 4 << 20;
    setsockopt(m_sock, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof rcv);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(kTapPort);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(m_sock, (sockaddr*)&a, sizeof a) != 0) {
        // another Resolve-hosted process (or an old driver) has the port: not fatal, the Decklink path still works
        VLOGW("tap: cannot listen on 127.0.0.1:%u (%s); the Velaria Sound plugin will not reach this process", (unsigned)kTapPort, strerror(errno));
        close(m_sock);
        m_sock = -1;
        m_run = false;
        return;
    }
    timeval tv{0, 250000};
    setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    m_thread = std::thread([this] { loop(); });
    VLOGI("tap: listening on 127.0.0.1:%u for the Velaria Sound plugin", (unsigned)kTapPort);
}

void TapReceiver::stop() {
    if (!m_run.exchange(false)) return;
    if (m_thread.joinable()) m_thread.join();
    if (m_sock >= 0) { close(m_sock); m_sock = -1; }
}

void TapReceiver::loop() {
    std::vector<uint8_t> buf(70000);
    while (m_run) {
        ssize_t n = recv(m_sock, buf.data(), buf.size(), 0);
        if (n < (ssize_t)sizeof(TapHeader)) continue;
        TapHeader h;
        memcpy(&h, buf.data(), sizeof h);
        if (h.magic != kTapMagic || h.version != 1) continue;
        if (h.channels < 1 || h.channels > kTapMaxChannels || h.frames == 0) continue;
        size_t need = sizeof h + (size_t)h.frames * h.channels * sizeof(float);
        if ((size_t)n < need) continue;
        store(h, reinterpret_cast<const float*>(buf.data() + sizeof h));
    }
}

void TapReceiver::store(const TapHeader& h, const float* pcm) {
    bool first = m_packets.fetch_add(1) == 0;
    int64_t now = nowNs();
    bool wasLive = now - m_lastNs.load() < kLiveNs;
    m_lastNs = now;
    m_channels = h.channels;
    m_rate = h.sampleRate ? h.sampleRate : 48000;
    m_ambi = (h.flags & 4) != 0;
    m_playing = (h.flags & 1) != 0;
    if (first || !wasLive)
        VLOGI("tap: Velaria Sound plugin %s: %u channel(s) at %u Hz, %s, project time %s", first ? "found" : "back",
              h.channels, h.sampleRate, (h.flags & 4) ? "ambisonic" : "speakers", (h.flags & 2) ? "valid" : "MISSING");
    if (!(h.flags & 2) || h.projectTime < 0) return;     // without a timeline position we cannot align it
    if (h.seq != m_lastSeq + 1 && m_packets > 1) m_lost += (uint32_t)(h.seq - m_lastSeq - 1);
    m_lastSeq = h.seq;
    std::lock_guard<std::mutex> lk(m_mutex);
    for (uint32_t i = 0; i < h.frames; i++) {
        int64_t t = h.projectTime + i;
        int64_t slot = t % m_capacity;
        float* dst = &m_ring[(size_t)slot * kTapMaxChannels];
        const float* src = pcm + (size_t)i * h.channels;
        for (int c = 0; c < kTapMaxChannels; c++) dst[c] = c < h.channels ? src[c] : 0.0f;
        m_stamp[(size_t)slot] = (int32_t)(t / m_capacity);
    }
    int64_t end = h.projectTime + h.frames;
    if (m_first < 0 || std::llabs(h.projectTime - m_end) > (int64_t)m_rate) {   // a seek (either way) starts a new range
        m_first = h.projectTime;
        m_end = end;
    } else {
        if (h.projectTime < m_first) m_first = h.projectTime;
        if (end > m_end) m_end = end;
    }
    if (m_end - m_first > m_capacity) m_first = m_end - m_capacity;
}

bool TapReceiver::live(int64_t now) const {
    return m_run && m_packets > 0 && now - m_lastNs.load() < kLiveNs;
}

void TapReceiver::held(int64_t& first, int64_t& end) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    first = m_first;
    end = m_end;
}

uint32_t TapReceiver::read(int64_t at, uint32_t frames, int16_t* out, uint32_t outChannels) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    uint32_t have = 0;
    int ch = m_channels.load();
    for (uint32_t i = 0; i < frames; i++) {
        int64_t t = at + i;
        int16_t* o = out + (size_t)i * outChannels;
        if (t < 0 || m_capacity == 0 || m_stamp[(size_t)(t % m_capacity)] != (int32_t)(t / m_capacity)) {
            for (uint32_t c = 0; c < outChannels; c++) o[c] = 0;
            continue;
        }
        have++;
        const float* s = &m_ring[(size_t)(t % m_capacity) * kTapMaxChannels];
        for (uint32_t c = 0; c < outChannels; c++) {
            float v = (int)c < ch ? s[c] : (ch == 1 ? s[0] : 0.0f);   // a mono bus goes to both ears
            v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
            o[c] = (int16_t)(v * 32767.0f);
        }
    }
    return have;
}

}  // namespace vrl
