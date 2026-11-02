// Reference receiver for VR Link: connects like the headset, keeps a synced clock, decodes every frame with
// VideoToolbox, reads the e2e barcode, and runs the same refresh scheduler the headset uses on a simulated display.
// Usage: probe [host=127.0.0.1] [seconds=20] [hz=72] [delayMs=60] [snapshot.ppm]   (port: VRLINK_PORT, default 47810)
#include <CoreMedia/CoreMedia.h>
#include <VideoToolbox/VideoToolbox.h>
#include <arpa/inet.h>
#include <mach/mach_time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "protocol.h"
#include "timing.h"

using namespace vrlproto;
using namespace vrltime;

static int64_t nowNs() {
    static mach_timebase_info_data_t tb = [] {
        mach_timebase_info_data_t t;
        mach_timebase_info(&t);
        return t;
    }();
    return (int64_t)((__uint128_t)mach_absolute_time() * tb.numer / tb.denom);
}

static const int kBits = 24;
static void barcodeCell(int bit, int aw, int ah, int& x0, int& y0, int& x1, int& y1) {
    double cw = aw * 0.40 / kBits;
    x0 = (int)(aw * 0.05 + bit * cw);
    x1 = (int)(x0 + cw * 0.8);
    y0 = (int)(ah * 0.45);
    y1 = (int)(ah * 0.55);
}

struct Decoded {
    bool noDisplay;
    uint64_t seq;
    int64_t ptsSender;
    int64_t durationNs;
    bool sync;
    uint32_t barcode;
    int64_t decodedRx;
};

struct Probe {
    int fd = -1;
    std::mutex sendMutex;
    ClockSync clock;
    std::mutex clockMutex;
    VTDecompressionSessionRef session = nullptr;
    CMVideoFormatDescriptionRef fmt = nullptr;
    StreamConfigMsg cfg{};
    bool haveCfg = false;
    std::string snapshotPath;
    bool snapshotDone = false;
    FILE* dump = nullptr;          // PROBE_DUMP=path: raw Annex-B (parameter sets + every access unit) for offline analysis
    bool dumpStarted = false;
    uint32_t dumpConfig = 0;

    // stats
    uint64_t received = 0, decoded = 0, decodeErrors = 0, bytes = 0, keyframes = 0, syncFrames = 0;
    uint64_t indexGaps = 0, noDisplay = 0;
    uint32_t lastIndex = 0, lastIndexConfig = 0;
    std::vector<uint32_t> barcodes;
    RollingStat arrivalLatencyMs{100000};   // scan-out (sender) -> fully received
    RollingStat decodeLatencyMs{100000};    // scan-out -> decoded
    RollingStat margin{100000};
    std::mutex readyMutex;
    std::vector<Decoded> ready;
    int64_t pendingDue = INT64_MAX;
    std::map<uint64_t, Decoded> inflight;   // received, not decoded yet

    bool sendMsg(const std::vector<uint8_t>& m) {
        std::lock_guard<std::mutex> lk(sendMutex);
        size_t off = 0;
        while (off < m.size()) {
            ssize_t w = send(fd, m.data() + off, m.size() - off, 0);
            if (w <= 0) return false;
            off += (size_t)w;
        }
        return true;
    }

    static void decodeCallback(void* ref, void* frameRef, OSStatus st, VTDecodeInfoFlags, CVImageBufferRef img, CMTime,
                               CMTime) {
        auto* p = (Probe*)ref;
        auto* d = (Decoded*)frameRef;
        if (st != noErr || !img) {
            p->decodeErrors++;
            fprintf(stderr, "decode error %d seq %llu\n", (int)st, (unsigned long long)d->seq);
            delete d;
            return;
        }
        p->decoded++;
        d->decodedRx = nowNs();
        CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
        const uint8_t* yp = (const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(img, 0);
        size_t ys = CVPixelBufferGetBytesPerRowOfPlane(img, 0);
        int w = (int)CVPixelBufferGetWidth(img), h = (int)CVPixelBufferGetHeight(img);
        uint32_t code = 0;
        for (int b = 0; b < kBits; b++) {
            int x0, y0, x1, y1;
            barcodeCell(b, w, h, x0, y0, x1, y1);
            int v = yp[(size_t)((y0 + y1) / 2) * ys + (size_t)((x0 + x1) / 2)];
            code = (code << 1) | (v > 128 ? 1u : 0u);
        }
        d->barcode = code;
        if (!p->snapshotPath.empty() && !p->snapshotDone && p->decoded > 30) {
            p->writeSnapshot(img);
            p->snapshotDone = true;
        }
        CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
        {
            std::lock_guard<std::mutex> lk(p->readyMutex);
            p->barcodes.push_back(code);
            p->ready.push_back(*d);
            std::sort(p->ready.begin(), p->ready.end(), [](const Decoded& a, const Decoded& b) { return a.seq < b.seq; });
        }
        delete d;
    }

    void writeSnapshot(CVImageBufferRef img) {
        // 1/4-scale RGB PPM (BT.709 limited range)
        const uint8_t* yp = (const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(img, 0);
        const uint8_t* uv = (const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(img, 1);
        size_t ys = CVPixelBufferGetBytesPerRowOfPlane(img, 0), us = CVPixelBufferGetBytesPerRowOfPlane(img, 1);
        int w = (int)CVPixelBufferGetWidth(img) / 4, h = (int)CVPixelBufferGetHeight(img) / 4;
        FILE* f = fopen(snapshotPath.c_str(), "wb");
        if (!f) return;
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                double Y = (yp[(size_t)(y * 4) * ys + (size_t)(x * 4)] - 16) / 219.0;
                double Cb = (uv[(size_t)(y * 2) * us + (size_t)(x * 2) * 2] - 128) / 224.0;
                double Cr = (uv[(size_t)(y * 2) * us + (size_t)(x * 2) * 2 + 1] - 128) / 224.0;
                double r = Y + 1.5748 * Cr, g = Y - 0.1873 * Cb - 0.4681 * Cr, b = Y + 1.8556 * Cb;
                uint8_t px[3] = {(uint8_t)std::clamp(r * 255, 0.0, 255.0), (uint8_t)std::clamp(g * 255, 0.0, 255.0),
                                 (uint8_t)std::clamp(b * 255, 0.0, 255.0)};
                fwrite(px, 1, 3, f);
            }
        fclose(f);
        printf("snapshot written: %s\n", snapshotPath.c_str());
    }

    bool setupDecoder(const uint8_t* csd, size_t len) {
        if (session) {
            VTDecompressionSessionWaitForAsynchronousFrames(session);
            VTDecompressionSessionInvalidate(session);
            CFRelease(session);
            session = nullptr;
        }
        if (fmt) CFRelease(fmt);
        fmt = nullptr;
        std::vector<const uint8_t*> ptrs;
        std::vector<size_t> sizes;
        size_t i = 0;
        auto findStart = [&](size_t from) {
            for (size_t k = from; k + 4 <= len; k++)
                if (csd[k] == 0 && csd[k + 1] == 0 && csd[k + 2] == 0 && csd[k + 3] == 1) return k;
            return len;
        };
        i = findStart(0);
        while (i < len) {
            size_t s = i + 4, e = findStart(s);
            ptrs.push_back(csd + s);
            sizes.push_back(e - s);
            i = e;
        }
        if (ptrs.size() < 3) return false;
        OSStatus st = CMVideoFormatDescriptionCreateFromHEVCParameterSets(nullptr, ptrs.size(), ptrs.data(), sizes.data(),
                                                                          4, nullptr, &fmt);
        if (st != noErr) {
            fprintf(stderr, "format description failed %d\n", (int)st);
            return false;
        }
        int32_t pf = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        CFNumberRef n = CFNumberCreate(nullptr, kCFNumberSInt32Type, &pf);
        const void* k[] = {kCVPixelBufferPixelFormatTypeKey};
        const void* v[] = {n};
        CFDictionaryRef attrs = CFDictionaryCreate(nullptr, k, v, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        VTDecompressionOutputCallbackRecord cb{decodeCallback, this};
        st = VTDecompressionSessionCreate(nullptr, fmt, nullptr, attrs, &cb, &session);
        CFRelease(attrs);
        CFRelease(n);
        if (st != noErr) {
            fprintf(stderr, "decoder create failed %d\n", (int)st);
            return false;
        }
        return true;
    }

    void decode(const uint8_t* au, size_t len, Decoded* meta) {
        // Annex-B -> 4-byte length prefixed, dropping in-band VPS/SPS/PPS
        std::vector<uint8_t> out;
        out.reserve(len);
        auto findStart = [&](size_t from) {
            for (size_t k = from; k + 4 <= len; k++)
                if (au[k] == 0 && au[k + 1] == 0 && au[k + 2] == 0 && au[k + 3] == 1) return k;
            return len;
        };
        size_t i = findStart(0);
        while (i < len) {
            size_t s = i + 4, e = findStart(s);
            int type = (au[s] >> 1) & 0x3f;
            if (type < 32 || type > 34) {
                uint32_t n = (uint32_t)(e - s);
                uint8_t l[4] = {(uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n};
                out.insert(out.end(), l, l + 4);
                out.insert(out.end(), au + s, au + e);
            }
            i = e;
        }
        CMBlockBufferRef bb = nullptr;
        CMBlockBufferCreateWithMemoryBlock(nullptr, nullptr, out.size(), nullptr, nullptr, 0, out.size(), 0, &bb);
        CMBlockBufferReplaceDataBytes(out.data(), bb, 0, out.size());
        CMSampleBufferRef sb = nullptr;
        size_t sz = out.size();
        CMSampleBufferCreateReady(nullptr, bb, fmt, 1, 0, nullptr, 1, &sz, &sb);
        OSStatus st = VTDecompressionSessionDecodeFrame(session, sb, 0, meta, nullptr);
        if (st != noErr) {
            decodeErrors++;
            delete meta;
        }
        CFRelease(sb);
        CFRelease(bb);
    }
};

int main(int argc, char** argv) {
    std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    double seconds = argc > 2 ? atof(argv[2]) : 20;
    double hz = argc > 3 ? atof(argv[3]) : 72;
    int64_t delayNs = (int64_t)((argc > 4 ? atof(argv[4]) : 60) * 1e6);
    Probe p;
    if (argc > 5) p.snapshotPath = argv[5];
    if (getenv("PROBE_DUMP")) p.dump = fopen(getenv("PROBE_DUMP"), "wb");

    int64_t deadline = nowNs() + 10LL * 1000000000LL;
    while (true) {
        p.fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(getenv("VRLINK_PORT") ? (uint16_t)atoi(getenv("VRLINK_PORT")) : kTcpPort);
        inet_pton(AF_INET, host.c_str(), &a.sin_addr);
        if (connect(p.fd, (sockaddr*)&a, sizeof a) == 0) break;
        close(p.fd);
        if (nowNs() > deadline) {
            fprintf(stderr, "probe: cannot connect to %s:%d\n", host.c_str(), kTcpPort);
            return 2;
        }
        usleep(100000);
    }
    int one = 1;
    setsockopt(p.fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    int rcv = 8 << 20;
    setsockopt(p.fd, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof rcv);
    HelloMsg h{};
    h.magic = kMagic;
    h.version = kVersion;
    h.maxWidth = 8192;
    h.maxHeight = 8192;
    strncpy(h.name, "vrlink-probe", sizeof h.name);
    p.sendMsg(makeMessage(MsgType::Hello, h));
    printf("probe: connected to %s\n", host.c_str());

    std::atomic<bool> running{true};
    std::thread pinger([&] {
        uint32_t id = 0;
        while (running) {
            PingMsg m{++id, nowNs()};
            p.sendMsg(makeMessage(MsgType::Ping, m));
            usleep(250000);
        }
    });

    // simulated headset display
    FrameScheduler sched;
    int64_t refresh = (int64_t)std::llround(1e9 / hz);
    sched.setRefreshPeriod(refresh);
    std::atomic<bool> playing{false};
    std::thread display([&] {
        int64_t next = nowNs();
        uint64_t onScreen = 0;
        int64_t onScreenDur = 0;
        while (running) {
            next += refresh;
            while (nowNs() < next) usleep(200);
            std::vector<ScheduledItem> items;
            int64_t offsetNow;
            {
                std::lock_guard<std::mutex> lk(p.clockMutex);
                if (!p.clock.valid()) continue;
                offsetNow = p.clock.offsetAt(next);
            }
            std::lock_guard<std::mutex> lk(p.readyMutex);
            for (auto& d : p.ready)
                items.push_back({d.seq, d.sync ? 0 : d.ptsSender - offsetNow + delayNs, d.durationNs, d.sync});
            int idx = sched.pick(items, next, INT64_MAX);
            if (idx >= 0) {
                onScreen = items[idx].seq;
                onScreenDur = items[idx].durationNs;
                p.ready.erase(p.ready.begin(), p.ready.begin() + idx + 1);
            }
            if (onScreen) sched.onRefresh(onScreen, onScreenDur, playing.load());
        }
        sched.finish();
    });

    StreamParser parser;
    std::vector<uint8_t> buf(1 << 20);
    int64_t start = nowNs(), firstVideo = 0, lastVideo = 0;
    while (nowNs() - start < (int64_t)(seconds * 1e9)) {
        ssize_t r = recv(p.fd, buf.data(), buf.size(), 0);
        int64_t rx = nowNs();
        if (r <= 0) break;
        parser.feed(buf.data(), (size_t)r);
        StreamParser::Message m;
        while (parser.next(m)) {
            if (m.type == MsgType::Pong) {
                PongMsg pg;
                if (readBody(m.payload, pg)) {
                    std::lock_guard<std::mutex> lk(p.clockMutex);
                    p.clock.addSample(pg.t1, pg.t2, pg.t3, rx);
                }
            } else if (m.type == MsgType::StreamConfig) {
                const uint8_t* csd;
                size_t csdLen;
                if (readBody(m.payload, p.cfg, &csd, &csdLen)) {
                    p.haveCfg = p.setupDecoder(csd, std::min<size_t>(csdLen, p.cfg.csdLength));
                    if (p.dump && !p.dumpStarted) {
                        fwrite(csd, 1, std::min<size_t>(csdLen, p.cfg.csdLength), p.dump);
                        p.dumpConfig = p.cfg.configId;
                    }
                    printf("config %u: %ux%u %s %s %.3f fps raster %ux%u (%s) csd %u B -> decoder %s\n", p.cfg.configId,
                           p.cfg.width, p.cfg.height, layoutName(p.cfg.layout), projectionName(p.cfg.projection),
                           (double)p.cfg.fpsNum / p.cfg.fpsDen, p.cfg.rasterWidth, p.cfg.rasterHeight, p.cfg.modeName,
                           p.cfg.csdLength, p.haveCfg ? "ok" : "FAILED");
                }
            } else if (m.type == MsgType::Playback) {
                PlaybackMsg pb;
                if (readBody(m.payload, pb)) {
                    playing = pb.state == 1;
                    printf("playback: %s\n", pb.state ? "playing" : "stopped");
                }
            } else if (m.type == MsgType::Video) {
                VideoMsg v;
                const uint8_t* au;
                size_t len;
                if (!readBody(m.payload, v, &au, &len)) continue;
                p.received++;
                p.bytes += m.payload.size();
                if (v.configId == p.lastIndexConfig && v.encodeIndex != p.lastIndex + 1) p.indexGaps++;
                p.lastIndexConfig = v.configId;
                p.lastIndex = v.encodeIndex;
                if (v.flags & kVideoNoDisplay) p.noDisplay++;
                if (v.flags & kVideoKeyframe) p.keyframes++;
                if (v.flags & kVideoSync) p.syncFrames++;
                if (!firstVideo) firstVideo = rx;
                lastVideo = rx;
                int64_t ptsRx;
                {
                    std::lock_guard<std::mutex> lk(p.clockMutex);
                    ptsRx = p.clock.valid() ? p.clock.senderToReceiver(v.ptsNs, rx) : rx;
                }
                if (!(v.flags & kVideoSync)) {
                    p.arrivalLatencyMs.add((rx - ptsRx) / 1e6);
                }
                if (!p.haveCfg) continue;
                if (p.dump && v.configId == p.dumpConfig) {
                    if (!p.dumpStarted && (v.flags & kVideoKeyframe)) p.dumpStarted = true;
                    if (p.dumpStarted) fwrite(au, 1, len, p.dump);
                }
                auto* d = new Decoded{(v.flags & kVideoNoDisplay) != 0, v.frameSeq, v.ptsNs, v.durationNs,
                                      (v.flags & kVideoSync) != 0, 0, 0};
                int64_t t = nowNs();
                p.decode(au, len, d);
                p.decodeLatencyMs.add((nowNs() - ptsRx) / 1e6);
                (void)t;
            }
        }
    }
    running = false;
    shutdown(p.fd, SHUT_RDWR);
    pinger.join();
    display.join();
    if (p.session) VTDecompressionSessionWaitForAsynchronousFrames(p.session);
    if (p.dump) fclose(p.dump);

    // barcode verification: frame numbers must be consecutive, no duplicates, no gaps
    uint64_t gaps = 0, dups = 0, reorders = 0;
    std::set<uint32_t> seen;
    for (size_t i = 0; i < p.barcodes.size(); i++) {
        uint32_t c = p.barcodes[i];
        if (seen.count(c)) dups++;
        seen.insert(c);
        if (i > 0) {
            if (c < p.barcodes[i - 1]) reorders++;
            else if (c > p.barcodes[i - 1] + 1) gaps += c - p.barcodes[i - 1] - 1;
        }
    }
    double secs = (lastVideo - firstVideo) / 1e9;
    const auto& st = sched.stats();
    printf("\n==== probe summary ====\n");
    printf("video: received %llu (%llu key, %llu sync, %llu no-display), bitstream gaps %llu, decoded %llu, decode errors %llu, %.1f Mbit/s\n",
           (unsigned long long)p.received, (unsigned long long)p.keyframes, (unsigned long long)p.syncFrames,
           (unsigned long long)p.noDisplay, (unsigned long long)p.indexGaps,
           (unsigned long long)p.decoded, (unsigned long long)p.decodeErrors, secs > 0 ? p.bytes * 8 / secs / 1e6 : 0.0);
    if (!p.barcodes.empty())
        printf("barcodes: first %u last %u count %zu unique %zu  gaps %llu dups %llu reorders %llu\n", p.barcodes.front(),
               p.barcodes.back(), p.barcodes.size(), seen.size(), (unsigned long long)gaps, (unsigned long long)dups,
               (unsigned long long)reorders);
    printf("clock: rtt last %.2f ms min %.2f ms\n", p.clock.lastRttNs() / 1e6, p.clock.minRttNs() / 1e6);
    if (p.arrivalLatencyMs.count())
        printf("scan-out -> received: p5 %.1f  p50 %.1f  p95 %.1f  max %.1f ms (negative = arrived before scan-out)\n",
               p.arrivalLatencyMs.percentile(5), p.arrivalLatencyMs.percentile(50), p.arrivalLatencyMs.percentile(95),
               p.arrivalLatencyMs.percentile(100));
    if (p.decodeLatencyMs.count())
        printf("scan-out -> decoded:  p50 %.1f  p95 %.1f  max %.1f ms\n", p.decodeLatencyMs.percentile(50),
               p.decodeLatencyMs.percentile(95), p.decodeLatencyMs.percentile(100));
    printf("display @ %.0f Hz, delay %.0f ms: shown %llu skipped %llu cadenceErrors %llu holds %llu  refreshes/frame:",
           hz, delayNs / 1e6, (unsigned long long)st.framesShown, (unsigned long long)st.framesSkipped,
           (unsigned long long)st.cadenceErrors, (unsigned long long)st.holds);
    for (int i = 1; i < 8; i++)
        if (st.refreshHistogram[i]) printf(" %dx:%llu", i, (unsigned long long)st.refreshHistogram[i]);
    printf("\n");
    bool pass = p.decodeErrors == 0 && gaps == 0 && dups == 0 && reorders == 0 && p.decoded > 0 && p.indexGaps == 0;
    printf("RESULT: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
