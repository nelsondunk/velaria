// VR Link wire protocol (TCP, little-endian). Shared by the Mac virtual card and the Quest receiver.
//
//   Every message: MsgHeader (8 bytes) + payload.
//   Sender = Mac (virtual DeckLink inside Resolve). Receiver = headset.
//
//   r->s HELLO            first message from the receiver
//   s->r STREAM_CONFIG    codec parameters + raster/layout; sent on connect and whenever it changes
//   s->r VIDEO            one encoded access unit (Annex-B HEVC), stamped with its scan-out time on the sender clock
//   s->r PLAYBACK         transport state (playing / stopped), for delay adaptation and the overlay
//   r->s PING / s->r PONG NTP-style clock exchange (receiver initiates)
//   r->s KEYFRAME_REQ     decoder needs an IDR (after loss, reset, or app resume)
//   r->s STATS            receiver telemetry, logged on the Mac
//   s->r SCOPES           waveform / parade / vectorscope / histogram rasters computed from the UNCOMPRESSED frame
//   r->s SCOPES_REQ       receiver asks for scopes (and at what rate); nothing is computed until it does
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace vrlproto {

constexpr uint32_t kMagic = 0x4B4C5256;     // 'VRLK' little-endian bytes "VRLK"
constexpr uint16_t kVersion = 1;
constexpr uint16_t kTcpPort = 47810;        // video from the virtual DeckLink (inside Resolve)
constexpr uint16_t kNdiTcpPort = 47813;     // video from the NDI helper (Premiere, After Effects, anything NDI)
constexpr uint16_t kDiscoveryPort = 47811;
constexpr uint32_t kMaxPayload = 64u * 1024u * 1024u;

enum class MsgType : uint8_t {
    Hello = 1,
    StreamConfig = 2,
    Video = 3,
    Audio = 4,
    Ping = 5,
    Pong = 6,
    KeyframeReq = 7,
    Stats = 8,
    Playback = 9,
    Bye = 10,
    Scopes = 11,
    ScopesReq = 12,
};

// SCOPES raster: one 8-bit intensity map, kScopesW x kScopesH, top row first. Fixed tiles (x, y, w, h):
//   waveform (luma)   0,   0, 256, 128   columns = picture left..right, rows = 100% (top) .. 0% (bottom)
//   parade R        256,   0, 128, 128   same vertical scale, columns = picture left..right
//   parade G        384,   0, 128, 128
//   parade B        512,   0, 128, 128
//   vectorscope     640,   0, 128, 128   x = Cb, y = Cr (up = +Cr), centre = neutral
//   histogram Y       0, 128, 256,  64   bins left = black .. right = white, filled to height
//   histogram R     256, 128, 128,  64
//   histogram G     384, 128, 128,  64
//   histogram B     512, 128, 128,  64
// Levels are video range on a 0..1023 scale (64 = 0%, 940 = 100%); the waveform/parade rows span 0..1023 so
// super-blacks and super-whites still show, and the receiver draws the 0% / 100% lines.
constexpr uint32_t kScopesW = 768, kScopesH = 192, kScopesLayoutV1 = 1;

enum class Layout : uint32_t { Mono = 0, SideBySide = 1, TopBottom = 2 };
enum class Projection : uint32_t { Flat = 0, Equirect180 = 1, Equirect360 = 2, Fisheye180 = 3 };   // Fisheye180: dual circular fisheye (Canon dual fisheye, Apple Immersive source), unwrapped on the headset

enum VideoFlags : uint32_t {
    kVideoKeyframe = 1u << 0,
    kVideoSync = 1u << 1,         // shown immediately (paused / scrubbing), not part of timed playback
    kVideoNoDisplay = 1u << 2,    // decode for reference only (Resolve flushed it before scan-out)
};

#pragma pack(push, 1)
struct MsgHeader {
    uint32_t length;   // payload bytes
    uint8_t type;
    uint8_t flags;
    uint16_t reserved;
};

struct HelloMsg {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t maxWidth;
    uint32_t maxHeight;
    uint32_t capabilities;   // bit0: wants audio
    char name[32];
};

struct StreamConfigMsg {
    uint32_t configId;
    uint32_t codec;          // 'hvc1'
    uint32_t width;
    uint32_t height;
    uint32_t layout;         // Layout
    uint32_t projection;     // Projection
    uint32_t fpsNum;
    uint32_t fpsDen;
    uint32_t colorPrimaries; // ISO/IEC 23091-2 codes: 1 = BT.709, 9 = BT.2020
    uint32_t transfer;       // 1 = BT.709, 16 = PQ, 18 = HLG
    uint32_t matrix;         // 1 = BT.709, 9 = BT.2020nc
    uint32_t bitDepth;
    uint32_t rasterWidth;    // output raster Resolve is sending (e.g. 7680x4320)
    uint32_t rasterHeight;
    char modeName[24];
    uint32_t csdLength;      // followed by csdLength bytes of Annex-B VPS/SPS/PPS
};

struct VideoMsg {
    uint32_t configId;
    uint32_t flags;          // VideoFlags
    uint64_t frameSeq;       // monotonically increasing per sender
    int64_t ptsNs;           // scan-out time on the SENDER clock (ns)
    int64_t durationNs;
    int64_t sentNs;          // sender clock when queued for send (for latency telemetry)
    uint32_t timecodeBCD;    // RP188 from Resolve, 0xFFFFFFFF if none
    uint32_t encodeIndex;    // consecutive per configId; a gap means the bitstream is broken until a keyframe
    // followed by Annex-B access unit
};

struct PlaybackMsg {
    uint32_t state;          // 0 stopped/paused, 1 playing
    uint32_t reserved;
    double speed;
    int64_t stopPtsNs;       // on stop: drop queued timed frames with pts > this (Resolve flushed them)
};

struct PingMsg {
    uint32_t id;
    int64_t t1;              // receiver clock at send
};

struct ScopesMsg {
    uint32_t width;          // kScopesW
    uint32_t height;         // kScopesH
    uint32_t layout;         // kScopesLayoutV1
    uint32_t timecodeBCD;    // frame the scopes were measured on (0xFFFFFFFF if none)
    int64_t ptsNs;           // that frame's scan-out time on the sender clock
    uint32_t sampleCount;    // pixels measured (subsampled)
    uint32_t reserved;
    // followed by width*height bytes
};

struct ScopesReqMsg {
    uint32_t enable;         // 1 = send scopes, 0 = stop
    uint32_t hz;             // wanted rate (the sender caps it)
};

struct PongMsg {
    uint32_t id;
    int64_t t1;              // echoed
    int64_t t2;              // sender clock at receive
    int64_t t3;              // sender clock at send
};

struct StatsMsg {
    uint32_t framesShown;
    uint32_t framesSkipped;   // decoded but never displayed (arrived too late)
    uint32_t cadenceErrors;   // frames shown for a different number of refreshes than nominal
    uint32_t holds;           // refreshes where the expected new frame was not ready
    int32_t playoutDelayMs;
    int32_t latencyP50Ms;     // capture(scan-out) -> photon
    int32_t latencyP95Ms;
    int32_t clockRttUs;
    float refreshHz;
    float decodeMsP50;
    uint32_t bitrateKbps;
    uint32_t reserved[3];
};

struct DiscoveryRequest {     // UDP broadcast from receiver
    uint32_t magic;           // kMagic
    uint32_t kind;            // 1 = request
};

struct DiscoveryReply {       // UDP unicast reply from sender
    uint32_t magic;
    uint32_t kind;            // 2 = reply
    uint16_t tcpPort;
    uint16_t version;
    char name[48];
};
#pragma pack(pop)

static_assert(sizeof(MsgHeader) == 8, "header size");

// ----- serialization helpers -----
inline void appendMessage(std::vector<uint8_t>& out, MsgType type, const void* body, size_t bodyLen,
                          const void* extra = nullptr, size_t extraLen = 0) {
    MsgHeader h{};
    h.length = (uint32_t)(bodyLen + extraLen);
    h.type = (uint8_t)type;
    size_t off = out.size();
    out.resize(off + sizeof h + bodyLen + extraLen);
    std::memcpy(out.data() + off, &h, sizeof h);
    if (bodyLen) std::memcpy(out.data() + off + sizeof h, body, bodyLen);
    if (extraLen) std::memcpy(out.data() + off + sizeof h + bodyLen, extra, extraLen);
}

template <typename T>
inline std::vector<uint8_t> makeMessage(MsgType type, const T& body, const void* extra = nullptr,
                                        size_t extraLen = 0) {
    std::vector<uint8_t> v;
    v.reserve(sizeof(MsgHeader) + sizeof(T) + extraLen);
    appendMessage(v, type, &body, sizeof(T), extra, extraLen);
    return v;
}

// Incremental stream parser: feed bytes, pop complete messages.
class StreamParser {
public:
    struct Message {
        MsgType type;
        std::vector<uint8_t> payload;
    };
    // Returns false on protocol error (oversized message).
    bool feed(const uint8_t* data, size_t len) {
        m_buf.insert(m_buf.end(), data, data + len);
        return !m_error;
    }
    // Pops one message if complete. Returns false if none available (or on error; check error()).
    bool next(Message& msg) {
        if (m_error) return false;
        size_t avail = m_buf.size() - m_pos;
        if (avail < sizeof(MsgHeader)) {
            compact();
            return false;
        }
        MsgHeader h;
        std::memcpy(&h, m_buf.data() + m_pos, sizeof h);
        if (h.length > kMaxPayload) {
            m_error = true;
            return false;
        }
        if (avail < sizeof h + h.length) {
            compact();
            return false;
        }
        msg.type = (MsgType)h.type;
        msg.payload.assign(m_buf.begin() + (long)(m_pos + sizeof h), m_buf.begin() + (long)(m_pos + sizeof h + h.length));
        m_pos += sizeof h + h.length;
        return true;
    }
    bool error() const { return m_error; }
    size_t buffered() const { return m_buf.size() - m_pos; }

private:
    void compact() {
        if (m_pos > 0 && (m_pos > 1 << 20 || m_pos == m_buf.size())) {
            m_buf.erase(m_buf.begin(), m_buf.begin() + (long)m_pos);
            m_pos = 0;
        }
    }
    std::vector<uint8_t> m_buf;
    size_t m_pos = 0;
    bool m_error = false;
};

template <typename T>
inline bool readBody(const std::vector<uint8_t>& payload, T& out, const uint8_t** extra = nullptr,
                     size_t* extraLen = nullptr) {
    if (payload.size() < sizeof(T)) return false;
    std::memcpy(&out, payload.data(), sizeof(T));
    if (extra) *extra = payload.data() + sizeof(T);
    if (extraLen) *extraLen = payload.size() - sizeof(T);
    return true;
}

inline const char* layoutName(uint32_t l) {
    switch ((Layout)l) {
        case Layout::Mono: return "mono";
        case Layout::SideBySide: return "SBS";
        case Layout::TopBottom: return "TB";
    }
    return "?";
}
inline const char* projectionName(uint32_t p) {
    switch ((Projection)p) {
        case Projection::Flat: return "flat";
        case Projection::Equirect180: return "180";
        case Projection::Equirect360: return "360";
        case Projection::Fisheye180: return "fisheye";
    }
    return "?";
}

}  // namespace vrlproto
