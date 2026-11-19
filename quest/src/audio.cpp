#include "audio.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <time.h>

#include <algorithm>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "VRLink", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "VRLink", __VA_ARGS__)

namespace vrq {

namespace {
constexpr int32_t kRate = 48000;
constexpr int32_t kChannels = 2;
constexpr int64_t kNs = 1000000000LL;
constexpr int64_t kDropBehindNs = 40 * 1000000LL;     // behind by more than this: skip ahead rather than lag
constexpr int64_t kContiguousNs = 5 * 1000000LL;      // blocks closer than this to the FIFO's end are joined
constexpr int64_t kFarNs = 2 * kNs;                   // a block due this far off is nonsense: ignore it

int64_t nowNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * kNs + ts.tv_nsec;
}

aaudio_data_callback_result_t dataCb(AAudioStream*, void* user, void* audioData, int32_t numFrames) {
    auto* self = static_cast<AudioOut*>(user);
    return self->onRender(static_cast<int16_t*>(audioData), numFrames) == 0 ? AAUDIO_CALLBACK_RESULT_CONTINUE
                                                                                : AAUDIO_CALLBACK_RESULT_STOP;
}

void errorCb(AAudioStream*, void* user, aaudio_result_t err) {
    LOGW("audio: stream error %s", AAudio_convertResultToText(err));
    static_cast<AudioOut*>(user)->onError();
}
}  // namespace

bool AudioOut::start() {
    if (m_stream) return true;
    AAudioStreamBuilder* b = nullptr;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK || !b) return false;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, kChannels);
    AAudioStreamBuilder_setSampleRate(b, kRate);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_MEDIA);
    AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MOVIE);
    AAudioStreamBuilder_setDataCallback(b, dataCb, this);
    AAudioStreamBuilder_setErrorCallback(b, errorCb, this);
    AAudioStream* s = nullptr;
    aaudio_result_t r = AAudioStreamBuilder_openStream(b, &s);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK || !s) {
        LOGW("audio: open failed: %s", AAudio_convertResultToText(r));
        return false;
    }
    if (AAudioStream_getSampleRate(s) != kRate || AAudioStream_getChannelCount(s) != kChannels ||
        AAudioStream_getFormat(s) != AAUDIO_FORMAT_PCM_I16) {
        LOGW("audio: device gave %d Hz x %d ch format %d, wanted 48000 x 2 s16", AAudioStream_getSampleRate(s),
             AAudioStream_getChannelCount(s), (int)AAudioStream_getFormat(s));
        AAudioStream_close(s);
        return false;
    }
    // two bursts of buffering: enough to ride a scheduling hiccup, small enough to keep our timing honest
    int32_t burst = AAudioStream_getFramesPerBurst(s);
    if (burst > 0) AAudioStream_setBufferSizeInFrames(s, burst * 2);
    r = AAudioStream_requestStart(s);
    if (r != AAUDIO_OK) {
        LOGW("audio: start failed: %s", AAudio_convertResultToText(r));
        AAudioStream_close(s);
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_stream = s;
        m_errored = false;
        m_fifo.clear(); m_fifoPos = 0; m_fifoValid = false;
        m_stats.running = true;
    }
    LOGI("audio: stream open, burst %d frames, buffer %d", burst, AAudioStream_getBufferSizeInFrames(s));
    return true;
}

void AudioOut::stop() {
    AAudioStream* s = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        s = m_stream;
        m_stream = nullptr;
        m_stats.running = false;
    }
    if (s) {
        AAudioStream_requestStop(s);
        AAudioStream_close(s);
    }
    flush();
}

void AudioOut::flush() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_queue.clear();
    m_fifo.clear(); m_fifoPos = 0; m_fifoValid = false;
}

void AudioOut::push(AudioBlock&& b) {
    if (b.frames == 0 || b.pcm.size() < (size_t)b.frames * kChannels) return;
    int64_t now = nowNs();
    if (b.dueLocalNs < now - kFarNs || b.dueLocalNs > now + kFarNs) return;
    std::lock_guard<std::mutex> lk(m_mutex);
    m_stats.blocks++;
    if (m_queue.size() > 90) {   // 3 s: the callback is not running or is hopelessly behind
        m_queue.clear();
        m_stats.resets++;
    }
    m_queue.push_back(std::move(b));
}

AudioStats AudioOut::stats() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_stats;
}

void AudioOut::onError() {
    m_errored = true;   // the owner notices running() going false on its next check and reopens
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_stream) { AAudioStream_close(m_stream); m_stream = nullptr; }
    m_stats.running = false;
}

// local time at which the sample we are about to write (framesAhead past what has been written) will be heard
int64_t AudioOut::playTimeNs(int64_t framesAhead) {
    int64_t framePos = 0, timeNs = 0;
    if (m_stream && AAudioStream_getTimestamp(m_stream, CLOCK_MONOTONIC, &framePos, &timeNs) == AAUDIO_OK && timeNs > 0) {
        int64_t written = AAudioStream_getFramesWritten(m_stream);
        return timeNs + (written + framesAhead - framePos) * kNs / kRate;
    }
    // before the first timestamp is available: assume two bursts of latency
    int32_t buf = m_stream ? AAudioStream_getBufferSizeInFrames(m_stream) : 0;
    return nowNs() + (buf + framesAhead) * kNs / kRate;
}

// move queued blocks into the FIFO; a block that does not follow the FIFO's end restarts it
void AudioOut::pullBlocksLocked() {
    while (!m_queue.empty()) {
        AudioBlock& b = m_queue.front();
        int64_t fifoEnd = m_fifoDueNs + (int64_t)((m_fifo.size() - m_fifoPos) / kChannels) * kNs / kRate;
        bool follows = m_fifoValid && !b.discontinuity && std::llabs(b.dueLocalNs - fifoEnd) <= kContiguousNs;
        if (!m_fifoValid || !follows) {
            if (m_fifoValid && m_fifo.size() > m_fifoPos) {
                // keep what is still unplayed only if the new block is later: a seek backwards starts fresh
                if (b.dueLocalNs > fifoEnd && b.dueLocalNs - fifoEnd < kNs) {
                    // a gap under a second: pad with silence so the join stays in time
                    size_t gap = (size_t)((b.dueLocalNs - fifoEnd) * kRate / kNs) * kChannels;
                    m_fifo.insert(m_fifo.end(), gap, 0);
                    m_stats.silenceFrames += gap / kChannels;
                } else {
                    m_fifo.erase(m_fifo.begin(), m_fifo.end());
                    m_fifoPos = 0;
                    m_fifoDueNs = b.dueLocalNs;
                    m_stats.resets++;
                }
            } else {
                m_fifo.clear();
                m_fifoPos = 0;
                m_fifoDueNs = b.dueLocalNs;
            }
            m_fifoValid = true;
        }
        m_fifo.insert(m_fifo.end(), b.pcm.begin(), b.pcm.begin() + (size_t)b.frames * kChannels);
        m_queue.pop_front();
        if (m_fifoPos > 48000 * kChannels) {   // compact occasionally
            m_fifo.erase(m_fifo.begin(), m_fifo.begin() + m_fifoPos);
            m_fifoPos = 0;
        }
    }
}

int AudioOut::onRender(int16_t* out, int32_t frames) {
    std::unique_lock<std::mutex> lk(m_mutex, std::try_to_lock);
    if (!lk.owns_lock()) {   // never block the audio thread: a missed callback is a few ms of silence
        std::memset(out, 0, (size_t)frames * kChannels * sizeof(int16_t));
        return 0;
    }
    pullBlocksLocked();
    int32_t done = 0;
    int64_t playAt = playTimeNs(0);
    while (done < frames) {
        size_t avail = (m_fifo.size() - m_fifoPos) / kChannels;
        if (!m_fifoValid || avail == 0) {
            if (m_fifoValid && m_stats.played > 0) m_stats.underruns++;
            std::memset(out + (size_t)done * kChannels, 0, (size_t)(frames - done) * kChannels * sizeof(int16_t));
            m_fifoValid = false;   // the next block restarts the clock
            break;
        }
        int64_t at = playAt + (int64_t)done * kNs / kRate;
        int64_t diff = m_fifoDueNs - at;   // > 0: not yet due; < 0: behind
        m_stats.skewMs = -diff / 1e6;
        if (diff > kNs / 1000) {
            int64_t wait = std::min<int64_t>(frames - done, diff * kRate / kNs);
            if (wait <= 0) wait = 1;
            std::memset(out + (size_t)done * kChannels, 0, (size_t)wait * kChannels * sizeof(int16_t));
            m_stats.silenceFrames += (uint64_t)wait;
            done += (int32_t)wait;
            continue;
        }
        if (diff < -kDropBehindNs) {
            size_t drop = std::min(avail, (size_t)((-diff) * kRate / kNs));
            m_fifoPos += drop * kChannels;
            m_fifoDueNs += (int64_t)drop * kNs / kRate;
            m_stats.droppedFrames += drop;
            continue;
        }
        size_t n = std::min<size_t>(avail, (size_t)(frames - done));
        std::memcpy(out + (size_t)done * kChannels, m_fifo.data() + m_fifoPos, n * kChannels * sizeof(int16_t));
        m_fifoPos += n * kChannels;
        m_fifoDueNs += (int64_t)n * kNs / kRate;
        m_stats.played += n;
        done += (int32_t)n;
    }
    return 0;
}

}  // namespace vrq
