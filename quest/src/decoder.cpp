#include "decoder.h"

#include <android/log.h>
#include <media/NdkMediaFormat.h>
#include <unistd.h>

#include <climits>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VRLink", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "VRLink", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "VRLink", __VA_ARGS__)

namespace vrq {

using namespace vrlproto;

static constexpr int kMaxImages = 6;

void Decoder::start() {
    m_running = true;
    m_thread = std::thread([this] { threadMain(); });
}

void Decoder::stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
    releaseCodec();
}

AImageReader* Decoder::reader(uint32_t& generation) {
    std::lock_guard<std::mutex> lk(m_mutex);
    generation = m_generation;
    return m_reader;
}

void Decoder::collectRetired(std::vector<AImageReader*>& out) {
    std::lock_guard<std::mutex> lk(m_mutex);
    out.insert(out.end(), m_retired.begin(), m_retired.end());
    m_retired.clear();
}

bool Decoder::takeInfo(uint64_t seq, FrameInfo& out) {
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_info.find(seq);
    if (it == m_info.end()) return false;
    out = it->second;
    m_info.erase(it);
    return true;
}

int64_t Decoder::earliestPendingPts() {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto& kv : m_info)
        if (kv.second.decodedNs == 0 && !kv.second.sync && !kv.second.noDisplay) return kv.second.ptsSender;
    return INT64_MAX;
}

StreamConfig Decoder::config() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_cfg;
}

double Decoder::decodeMsP50() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_decodeMs.percentile(50);
}

double Decoder::decodeMsP95() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_decodeMs.count() ? m_decodeMs.percentile(95) : 25.0;
}

void Decoder::releaseCodec() {
    if (m_codec) {
        AMediaCodec_stop(m_codec);
        AMediaCodec_delete(m_codec);
        m_codec = nullptr;
    }
}

bool Decoder::configure(const StreamConfig& cfg) {
    releaseCodec();   // stop producing into the old surface first
    AImageReader* reader = nullptr;
    media_status_t st = AImageReader_newWithUsage((int32_t)cfg.msg.width, (int32_t)cfg.msg.height, AIMAGE_FORMAT_PRIVATE,
                                                  AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE, kMaxImages, &reader);
    if (st != AMEDIA_OK || !reader) {
        LOGE("AImageReader_newWithUsage %ux%u failed %d", cfg.msg.width, cfg.msg.height, st);
        return false;
    }
    ANativeWindow* window = nullptr;
    AImageReader_getWindow(reader, &window);

    AMediaCodec* codec = AMediaCodec_createDecoderByType("video/hevc");
    if (!codec) {
        LOGE("no HEVC decoder");
        AImageReader_delete(reader);
        return false;
    }
    AMediaFormat* fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "video/hevc");
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, (int32_t)cfg.msg.width);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, (int32_t)cfg.msg.height);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_MAX_WIDTH, (int32_t)cfg.msg.width);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_MAX_HEIGHT, (int32_t)cfg.msg.height);
    AMediaFormat_setInt32(fmt, "low-latency", 1);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_PRIORITY, 0);   // realtime
    if (cfg.msg.fpsNum && cfg.msg.fpsDen)
        AMediaFormat_setFloat(fmt, AMEDIAFORMAT_KEY_OPERATING_RATE, (float)cfg.msg.fpsNum / cfg.msg.fpsDen * 2.0f);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 24 * 1024 * 1024);
    if (!cfg.csd.empty()) AMediaFormat_setBuffer(fmt, "csd-0", cfg.csd.data(), cfg.csd.size());
    media_status_t cs = AMediaCodec_configure(codec, fmt, window, nullptr, 0);
    AMediaFormat_delete(fmt);
    if (cs != AMEDIA_OK || AMediaCodec_start(codec) != AMEDIA_OK) {
        LOGE("codec configure/start failed %d for %ux%u", cs, cfg.msg.width, cfg.msg.height);
        AMediaCodec_delete(codec);
        AImageReader_delete(reader);
        return false;
    }
    char* name = nullptr;
    if (AMediaCodec_getName(codec, &name) == AMEDIA_OK && name) {
        LOGI("decoder %s configured %ux%u", name, cfg.msg.width, cfg.msg.height);
        AMediaCodec_releaseName(codec, name);
    }
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_reader) m_retired.push_back(m_reader);   // render thread deletes it after dropping its images
    m_reader = reader;
    m_codec = codec;
    m_cfg = cfg;
    m_generation++;
    m_info.clear();
    m_hasConfig = true;
    return true;
}

void Decoder::drainOutput() {
    if (!m_codec) return;
    for (;;) {
        AMediaCodecBufferInfo info;
        ssize_t idx = AMediaCodec_dequeueOutputBuffer(m_codec, &info, 0);
        if (idx >= 0) {
            uint64_t seq = (uint64_t)info.presentationTimeUs;
            bool render = true;
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                auto it = m_info.find(seq);
                if (it != m_info.end()) {
                    it->second.decodedNs = monoNs();
                    m_decodeMs.add((it->second.decodedNs - it->second.queuedNs) / 1e6);
                    if (it->second.noDisplay) {
                        render = false;
                        m_info.erase(it);
                    }
                }
            }
            AMediaCodec_releaseOutputBuffer(m_codec, (size_t)idx, render);
        } else if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* f = AMediaCodec_getOutputFormat(m_codec);
            if (f) {
                LOGI("decoder output format: %s", AMediaFormat_toString(f));
                AMediaFormat_delete(f);
            }
        } else {
            break;   // try again later / buffers changed
        }
    }
}

void Decoder::threadMain() {
    pthread_setname_np(pthread_self(), "VRLink.decode");
    VideoUnit u;
    bool havePending = false;
    auto applyConfigs = [this] {
        StreamConfig cfg;
        bool any = false;
        while (m_net.popConfig(cfg)) {
            if (!configure(cfg)) LOGE("reconfigure failed");
            any = true;
        }
        if (any) m_needKey = true;
    };
    while (m_running) {
        applyConfigs();
        drainOutput();
        if (!havePending) {
            if (!m_net.waitVideo(u, 4)) continue;
            if (!m_codec || u.configId != m_cfg.msg.configId) applyConfigs();   // config may have raced the frame
            if (!m_codec || u.configId != m_cfg.msg.configId) {
                m_needKey = true;
                continue;   // frame for a stream we can't decode
            }
            bool contiguous = u.configId == m_lastConfig && u.encodeIndex == m_lastIndex + 1;
            m_lastConfig = u.configId;
            m_lastIndex = u.encodeIndex;
            if (u.key) {
                m_needKey = false;
            } else if (m_needKey || !contiguous) {
                if (!m_needKey) {
                    LOGW("bitstream gap before frame %llu; waiting for keyframe", (unsigned long long)u.seq);
                    m_net.requestKeyframe();
                }
                m_needKey = true;
                continue;
            }
            havePending = true;
        }
        // Just-in-time feed: hold the compressed frame until it is within decode time + 2 refreshes of display.
        if (!u.sync && !u.noDisplay && m_net.clockValid()) {
            double decodeMs;
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                decodeMs = m_decodeMs.count() ? m_decodeMs.percentile(95) : 30.0;
            }
            int64_t lead = (int64_t)(decodeMs * 1e6) + 2 * m_refreshNs.load() + 25000000LL;
            int64_t due = m_net.senderToLocal(u.ptsSender) + m_delayNs.load();
            if (due - monoNs() > lead) {
                usleep(1000);
                continue;   // loop keeps draining output and picking up config changes
            }
        }
        ssize_t in = AMediaCodec_dequeueInputBuffer(m_codec, 2000);
        if (in < 0) continue;   // codec busy: keep draining output
        size_t cap = 0;
        uint8_t* buf = AMediaCodec_getInputBuffer(m_codec, (size_t)in, &cap);
        if (!buf || u.au.size() > cap) {
            LOGE("access unit %zu bytes exceeds input buffer %zu", u.au.size(), cap);
            AMediaCodec_queueInputBuffer(m_codec, (size_t)in, 0, 0, 0, 0);
            havePending = false;
            m_needKey = true;
            m_net.requestKeyframe();
            continue;
        }
        memcpy(buf, u.au.data(), u.au.size());
        FrameInfo fi;
        fi.seq = u.seq;
        fi.configId = u.configId;
        fi.ptsSender = u.ptsSender;
        fi.durationNs = u.durationNs;
        fi.sync = u.sync;
        fi.noDisplay = u.noDisplay;
        fi.timecode = u.timecode;
        fi.rxNs = u.rxNs;
        fi.queuedNs = monoNs();
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_info[u.seq] = fi;
            while (m_info.size() > 64) m_info.erase(m_info.begin());   // frames the renderer never claimed
        }
        AMediaCodec_queueInputBuffer(m_codec, (size_t)in, 0, u.au.size(), (uint64_t)u.seq, 0);
        havePending = false;
    }
}

}  // namespace vrq
