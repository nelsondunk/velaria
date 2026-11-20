#include "audio.h"
#include "hrir_sh1.h"

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
constexpr int64_t kSoftJoinNs = 60 * 1000000LL;       // ...and a block that overlaps the FIFO's end by up to this much is
                                                      // trimmed and crossfaded in (the presentation delay walking down 10 ms
                                                      // at a time used to restart the FIFO: a hole every step, +77 on Bruce;
                                                      // easing it a sample at a time buzzed at 480 Hz, +78)
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
    if (m_stream.load() && !m_errored.load()) return true;
    // a failed open is not retried every frame (review 15): one try a second, so a device that refuses 48 kHz
    // stereo s16 does not fill the log or stall the render loop
    int64_t now = nowNs();
    if (m_lastStartTryNs && now - m_lastStartTryNs < kNs) return false;
    m_lastStartTryNs = now;
    if (AAudioStream* old = m_stream.exchange(nullptr)) {   // an errored stream: close it here, never in the callback
        AAudioStream_requestStop(old);
        AAudioStream_close(old);
        LOGI("audio: errored stream closed; reopening");
    }
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
        // a fresh stream starts from nothing: queued network blocks and the HRIR history belong to the old one
        m_queue.clear();
        resetFifoLocked(0);
        m_fifoValid = false;
        m_lastRotValid = false;
        m_stream = s;
        m_errored = false;
        m_stats.running = true;
    }
    m_lastStartTryNs = 0;
    LOGI("audio: stream open, burst %d frames, buffer %d", burst, AAudioStream_getBufferSizeInFrames(s));
    return true;
}

void AudioOut::stop() {
    AAudioStream* s = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        s = m_stream.exchange(nullptr);
        m_errored = false;
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
    resetFifoLocked(0);
    m_fifoValid = false;
}

void AudioOut::resetFifoLocked(int64_t dueNs) {
    m_fifo.clear();
    m_fifoPos = 0;
    m_fifoDueNs = dueNs;
    clearHrir();
}

void AudioOut::setHead(float qx, float qy, float qz, float qw) {
    std::unique_lock<std::mutex> lk(m_mutex, std::try_to_lock);
    if (!lk.owns_lock()) return;   // the callback has it: next frame
    m_head[0] = qx; m_head[1] = qy; m_head[2] = qz; m_head[3] = qw;
}

// Head-relative first-order components at the current head pose. AmbiX axes are x front, y left, z up; the
// headset's are x right, y up, -z forward, so ambi (x, y, z) = (-Z, -X, Y) in headset terms. The field is rotated by
// the inverse head rotation (first-order components rotate like a vector): m gives (Y' Z' X') from (Y Z X).
void AudioOut::headRotation(float m[3][3]) {
    // inverse head rotation as a 3x3 (conjugate quaternion)
    float x = -m_head[0], y = -m_head[1], z = -m_head[2], w = m_head[3];
    float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                     {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                     {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
    // a world-frame ambi component vector (X, Y, Z)_ambi = (-Zh, -Xh, Yh): build the headset-frame vector from the
    // ambi unit axes, rotate, and read back the head-relative ambi components
    auto rel = [&](float ax, float ay, float az, float out[3]) {
        float hx = -ay, hy = az, hz = -ax;   // ambi -> headset axes
        float rx = r[0][0] * hx + r[0][1] * hy + r[0][2] * hz;
        float ry = r[1][0] * hx + r[1][1] * hy + r[1][2] * hz;
        float rz = r[2][0] * hx + r[2][1] * hy + r[2][2] * hz;
        out[0] = -rz; out[1] = -rx; out[2] = ry;   // headset -> ambi (x, y, z)
    };
    // where each incoming component lands: cx = image of X, etc., as (x, y, z)
    float cx[3], cy[3], cz[3];
    rel(1, 0, 0, cx);
    rel(0, 1, 0, cy);
    rel(0, 0, 1, cz);
    // rows: Y' Z' X' ; columns: Y Z X
    m[0][0] = cy[1]; m[0][1] = cz[1]; m[0][2] = cx[1];
    m[1][0] = cy[2]; m[1][1] = cz[2]; m[1][2] = cx[2];
    m[2][0] = cy[0]; m[2][1] = cz[0]; m[2][2] = cx[0];
}

bool AudioOut::soundField(float cov[4][4], int64_t now) {
    std::unique_lock<std::mutex> lk(m_mutex, std::try_to_lock);
    if (!lk.owns_lock()) return false;
    if (m_covNs == 0 || now - m_covNs > 1500000000LL) return false;
    std::memcpy(cov, m_cov, sizeof m_cov);
    return true;
}

void AudioOut::clearHrir() {
    m_hist.assign((size_t)4 * 2 * kHrirTaps, 0.0f);
    m_histPos = 0;
}

namespace {
// the SH HRIRs reversed, so each ear is a plain dot product with a contiguous history window
struct HrirReversed {
    float h[4][kHrirTaps];
    HrirReversed() {
        for (int c = 0; c < 4; c++)
            for (int k = 0; k < kHrirTaps; k++) h[c][k] = kHrirSh1[c][kHrirTaps - 1 - k];
    }
};
const HrirReversed& hrirReversed() { static HrirReversed r; return r; }
constexpr float kHrirGain = 1.6f;   // a full-scale front source (W = X = 1) lands near full scale
}

void AudioOut::push(AudioBlock&& b) {
    int ch = b.channels == 4 ? 4 : 2;
    if (b.frames == 0 || b.pcm.size() < (size_t)b.frames * ch) return;
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
    // AAudio forbids closing the stream from its own error callback; flag it and let start() (main thread) close
    // and reopen. Everything queued is dropped there as well (review finding 6).
    m_errored = true;
    std::lock_guard<std::mutex> lk(m_mutex);
    m_stats.running = false;
}

// local time at which the sample we are about to write (framesAhead past what has been written) will be heard
int64_t AudioOut::playTimeNs(int64_t framesAhead) {
    int64_t framePos = 0, timeNs = 0;
    AAudioStream* st = m_stream.load();
    if (st && AAudioStream_getTimestamp(st, CLOCK_MONOTONIC, &framePos, &timeNs) == AAUDIO_OK && timeNs > 0) {
        int64_t written = AAudioStream_getFramesWritten(st);
        return timeNs + (written + framesAhead - framePos) * kNs / kRate;
    }
    // before the first timestamp is available: assume two bursts of latency
    int32_t buf = st ? AAudioStream_getBufferSizeInFrames(st) : 0;
    return nowNs() + (buf + framesAhead) * kNs / kRate;
}

// move queued blocks into the FIFO; a block that does not follow the FIFO's end restarts it
void AudioOut::pullBlocksLocked() {
    while (!m_queue.empty()) {
        AudioBlock& b = m_queue.front();
        int bch = b.channels == 4 ? 4 : 2;
        if (bch != m_fifoChannels) {   // stereo <-> ambisonic: start the FIFO over with the new stride
            m_fifo.clear();
            m_fifoPos = 0;
            m_fifoValid = false;
            m_fifoChannels = bch;
            clearHrir();
        }
        const int kChannels = m_fifoChannels;
        int64_t fifoEnd = m_fifoDueNs + (int64_t)((m_fifo.size() - m_fifoPos) / kChannels) * kNs / kRate;
        int64_t off = b.dueLocalNs - fifoEnd;
        bool follows = m_fifoValid && !b.discontinuity && std::llabs(off) <= kContiguousNs;
        size_t trim = 0;   // frames of the new block that overlap audio already queued
        if (m_fifoValid && !b.discontinuity && !follows && off < 0 && -off <= kSoftJoinNs) {
            trim = std::min<size_t>(b.frames, (size_t)((-off) * kRate / kNs));
            follows = true;
        }
        if (!m_fifoValid || !follows) {
            if (b.discontinuity) {
                // the sender says this block does not continue the last one (seek, stop/resume, gap): nothing already
                // queued may play first, and the HRIR history of the old passage must not bleed in (review finding 5)
                resetFifoLocked(b.dueLocalNs);
                if (m_fifoValid) m_stats.resets++;
            } else if (m_fifoValid && m_fifo.size() > m_fifoPos) {
                // keep what is still unplayed only if the new block is later: a seek backwards starts fresh
                if (b.dueLocalNs > fifoEnd && b.dueLocalNs - fifoEnd < kNs) {
                    // a gap under a second: pad with silence so the join stays in time
                    size_t gap = (size_t)((b.dueLocalNs - fifoEnd) * kRate / kNs) * kChannels;
                    m_fifo.insert(m_fifo.end(), gap, 0);
                    m_stats.silenceFrames += gap / kChannels;
                } else {
                    resetFifoLocked(b.dueLocalNs);
                    m_stats.resets++;
                }
            } else {
                resetFifoLocked(b.dueLocalNs);
            }
            m_fifoValid = true;
        }
        if (trim) {
            // crossfade the last millisecond already queued with the first millisecond of what follows the overlap
            size_t xf = std::min<size_t>({(size_t)48, (m_fifo.size() - m_fifoPos) / (size_t)kChannels, (size_t)(b.frames - trim)});
            int16_t* tail = m_fifo.data() + m_fifo.size() - xf * kChannels;
            const int16_t* head = b.pcm.data() + trim * kChannels;
            for (size_t i = 0; i < xf; i++) {
                float t = (float)(i + 1) / (float)(xf + 1);
                for (int c = 0; c < kChannels; c++)
                    tail[i * kChannels + c] = (int16_t)(tail[i * kChannels + c] * (1.0f - t) + head[i * kChannels + c] * t);
            }
            m_fifo.insert(m_fifo.end(), b.pcm.begin() + (trim + xf) * kChannels, b.pcm.begin() + (size_t)b.frames * kChannels);
            m_stats.droppedFrames += trim;
        } else {
            m_fifo.insert(m_fifo.end(), b.pcm.begin(), b.pcm.begin() + (size_t)b.frames * kChannels);
        }
        m_queue.pop_front();
        if (m_fifoPos > 48000 * kChannels) {   // compact occasionally
            m_fifo.erase(m_fifo.begin(), m_fifo.begin() + m_fifoPos);
            m_fifoPos = 0;
        }
    }
}

int AudioOut::onRender(int16_t* out, int32_t frames) {
    const int kOut = 2;   // the stream is always stereo
    std::unique_lock<std::mutex> lk(m_mutex, std::try_to_lock);
    if (!lk.owns_lock()) {   // never block the audio thread: a missed callback is a few ms of silence
        std::memset(out, 0, (size_t)frames * kOut * sizeof(int16_t));
        return 0;
    }
    pullBlocksLocked();
    const int kChannels = m_fifoChannels;
    // head rotation for this callback: from the last callback's to the current pose, so a turn is a glide
    float mNow[3][3], mFrom[3][3];
    headRotation(mNow);
    if (!m_lastRotValid) { std::memcpy(m_lastRot, mNow, sizeof mNow); m_lastRotValid = true; }
    std::memcpy(mFrom, m_lastRot, sizeof mFrom);
    std::memcpy(m_lastRot, mNow, sizeof mNow);
    if (m_hist.empty()) clearHrir();
    const HrirReversed& H = hrirReversed();
    int32_t done = 0;
    int64_t playAt = playTimeNs(0);
    while (done < frames) {
        size_t avail = (m_fifo.size() - m_fifoPos) / kChannels;
        if (!m_fifoValid || avail == 0) {
            if (m_fifoValid && m_stats.played > 0) m_stats.underruns++;
            std::memset(out + (size_t)done * kOut, 0, (size_t)(frames - done) * kOut * sizeof(int16_t));
            m_fifoValid = false;   // the next block restarts the clock
            break;
        }
        int64_t at = playAt + (int64_t)done * kNs / kRate;
        int64_t diff = m_fifoDueNs - at;   // > 0: not yet due; < 0: behind
        m_stats.skewMs = -diff / 1e6;
        if (diff > kNs / 1000) {
            int64_t wait = std::min<int64_t>(frames - done, diff * kRate / kNs);
            if (wait <= 0) wait = 1;
            std::memset(out + (size_t)done * kOut, 0, (size_t)wait * kOut * sizeof(int16_t));
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
        if (kChannels == 2) {
            std::memcpy(out + (size_t)done * kOut, m_fifo.data() + m_fifoPos, n * kOut * sizeof(int16_t));
        } else {
            const int16_t* in = m_fifo.data() + m_fifoPos;
            int16_t* o = out + (size_t)done * kOut;
            float inv = frames > 1 ? 1.0f / (float)(frames - 1) : 0.0f;
            float* hist = m_hist.data();
            // sound map: covariance of this run of samples in the world (picture) frame, folded into the EMA
            {
                double c[4][4] = {};
                for (size_t i = 0; i < n; i++) {
                    float v[4] = {in[i * 4] / 32768.0f, in[i * 4 + 1] / 32768.0f, in[i * 4 + 2] / 32768.0f, in[i * 4 + 3] / 32768.0f};
                    for (int a = 0; a < 4; a++)
                        for (int b = a; b < 4; b++) c[a][b] += (double)v[a] * v[b];
                }
                // time constant ~120 ms: alpha per block from the block length
                float alpha = 1.0f - std::exp(-(float)n / (0.12f * kRate));
                for (int a = 0; a < 4; a++)
                    for (int b = a; b < 4; b++) {
                        float blk = (float)(c[a][b] / (double)n);
                        m_cov[a][b] += (blk - m_cov[a][b]) * alpha;
                        m_cov[b][a] = m_cov[a][b];
                    }
                m_covNs = nowNs();
            }
            for (size_t i = 0; i < n; i++) {
                float t = (float)(done + (int32_t)i) * inv;
                float w = in[i * 4], yy = in[i * 4 + 1], zz = in[i * 4 + 2], xx = in[i * 4 + 3];
                float m[3][3];
                for (int r = 0; r < 3; r++)
                    for (int c = 0; c < 3; c++) m[r][c] = mFrom[r][c] + (mNow[r][c] - mFrom[r][c]) * t;
                // head-relative components, into the history (twice: at pos and pos + taps)
                float v[4] = {w, m[0][0] * yy + m[0][1] * zz + m[0][2] * xx,
                              m[1][0] * yy + m[1][1] * zz + m[1][2] * xx,
                              m[2][0] * yy + m[2][1] * zz + m[2][2] * xx};
                int pos = m_histPos;
                for (int c = 0; c < 4; c++) {
                    float* hc = hist + (size_t)c * 2 * kHrirTaps;
                    hc[pos] = v[c];
                    hc[pos + kHrirTaps] = v[c];
                }
                // y_c = sum_k h_c[k] x_c[n-k] = dot(h_c reversed, window ending at pos)
                float acc[4];
                for (int c = 0; c < 4; c++) {
                    const float* win = hist + (size_t)c * 2 * kHrirTaps + pos + 1;
                    const float* hr = H.h[c];
                    float a = 0.0f;
                    for (int k = 0; k < kHrirTaps; k++) a += hr[k] * win[k];
                    acc[c] = a;
                }
                m_histPos = pos + 1 == kHrirTaps ? 0 : pos + 1;
                float l = (acc[0] + acc[1] + acc[2] + acc[3]) * kHrirGain;
                float rr = (acc[0] - acc[1] + acc[2] + acc[3]) * kHrirGain;
                o[i * 2] = (int16_t)std::max(-32768.0f, std::min(32767.0f, l));
                o[i * 2 + 1] = (int16_t)std::max(-32768.0f, std::min(32767.0f, rr));
            }
        }
        m_fifoPos += n * kChannels;
        m_fifoDueNs += (int64_t)n * kNs / kRate;
        m_stats.played += n;
        done += (int32_t)n;
    }
    return 0;
}

}  // namespace vrq
