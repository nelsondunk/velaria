#include "tap.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace velaria {

static bool isAmbisonic(SpeakerArrangement arr) {
    // any arrangement made only of ACN speakers
    const SpeakerArrangement acn = SpeakerArr::kAmbi3rdOrderACN;   // ACN 0..15
    return arr != 0 && (arr & ~acn) == 0;
}

Processor::Processor() { setControllerClass(kControllerUID); }

Processor::~Processor() {
    closeSink();
    stopLogger();
    free(m_scratch);
    free(m_zero);
}

tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    tresult r = AudioEffect::initialize(context);
    if (r != kResultOk) return r;
    // Default to first-order ambisonics (the bus this is built for); setBusArrangements accepts anything up to 16.
    addAudioInput(STR16("In"), SpeakerArr::kAmbi1stOrderACN);
    addAudioOutput(STR16("Out"), SpeakerArr::kAmbi1stOrderACN);
    return kResultOk;
}

tresult PLUGIN_API Processor::terminate() {
    closeSink();
    return AudioEffect::terminate();
}

tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs, int32 numOuts) {
    // Strictly one bus in, one bus out, identical arrangement, 1..16 channels. Anything else is refused so the host
    // never gets a plugin that narrows the bus (that is what starved Fairlight's Ambisonics bus of X and Z).
    if (numIns != 1 || numOuts != 1) return kResultFalse;
    if (inputs[0] != outputs[0]) return kResultFalse;
    int32 n = SpeakerArr::getChannelCount(inputs[0]);
    if (n < 1 || n > kMaxChannels) return kResultFalse;
    removeAudioBusses();
    addAudioInput(STR16("In"), inputs[0]);
    addAudioOutput(STR16("Out"), outputs[0]);
    m_ambisonic = isAmbisonic(inputs[0]);
    logLine("arrangement: %d channel(s), %s (0x%llx)", (int)n, m_ambisonic ? "ambisonic ACN" : "speakers", (unsigned long long)inputs[0]);
    return kResultTrue;
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize) {
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    m_sampleRate = setup.sampleRate;
    size_t need = (size_t)std::max<int32>(setup.maxSamplesPerBlock, 64) * kMaxChannels;
    if (need > m_scratchFloats) {
        free(m_scratch);
        m_scratch = (float*)malloc(need * sizeof(float));
        m_scratchFloats = m_scratch ? need : 0;
    }
    size_t zero = (size_t)std::max<int32>(setup.maxSamplesPerBlock, 64);
    if (zero > m_zeroFloats) {
        free(m_zero);
        m_zero = (float*)calloc(zero, sizeof(float));
        m_zeroFloats = m_zero ? zero : 0;
    }
    logLine("setup: %.0f Hz, max block %d, %s", setup.sampleRate, (int)setup.maxSamplesPerBlock,
            setup.processMode == kRealtime ? "realtime" : setup.processMode == kOffline ? "offline (render)" : "prefetch");
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state) openSink(); else closeSink();
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API Processor::process(ProcessData& data) {
    if (data.numInputs < 1 || data.numOutputs < 1 || data.numSamples <= 0) return kResultOk;
    if (!data.inputs || !data.outputs) return kResultOk;
    AudioBusBuffers& in = data.inputs[0];
    AudioBusBuffers& out = data.outputs[0];
    int ch = std::min(in.numChannels, out.numChannels);
    if (ch <= 0) return kResultOk;
    // VST3 allows a null channel-buffer array, and null channel pointers, for an inactive bus (review, 20 Nov). An
    // input that is missing is silence; an output that is missing is simply not written. Nothing is dereferenced
    // before it has been checked.
    const int frames = (int)data.numSamples;
    const bool zeroOk = m_zero && (size_t)frames <= m_zeroFloats;   // false only for a block larger than promised
    const float* src[kMaxChannels];
    int n = std::min(ch, kMaxChannels);
    bool complete = true;   // every tapped channel has real or substitute samples
    for (int c = 0; c < n; ++c) {
        const float* s = in.channelBuffers32 ? in.channelBuffers32[c] : nullptr;
        if (!s) { s = zeroOk ? m_zero : nullptr; if (!s) complete = false; }
        src[c] = s;
    }
    // pass-through (the host may hand us the same buffers for in and out); a missing input writes silence
    for (int c = 0; c < ch; ++c) {
        float* d = out.channelBuffers32 ? out.channelBuffers32[c] : nullptr;
        if (!d) continue;
        const float* s = c < n ? src[c] : (in.channelBuffers32 ? in.channelBuffers32[c] : nullptr);
        if (!s) memset(d, 0, sizeof(float) * (size_t)frames);
        else if (d != s) memcpy(d, s, sizeof(float) * (size_t)frames);
    }
    out.silenceFlags = in.channelBuffers32 ? in.silenceFlags : ~0ull;
    if (!complete) return kResultOk;
    // Offline (Deliver) renders go through here too; the headset wants the live mix only.
    if (processSetup.processMode == kOffline) return kResultOk;
    meter(src, n, frames, data.processContext);
    sendBlock(src, n, frames, data.processContext);
    return kResultOk;
}

// ---------------------------------------------------------------- sink

void Processor::openSink() {
    if (m_sock < 0) {
        m_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (m_sock >= 0) {
            fcntl(m_sock, F_SETFL, O_NONBLOCK);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = htons(kTapPort);
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            ::connect(m_sock, (sockaddr*)&a, sizeof a);   // fixes the destination; sends never block
        }
    }
    if (!m_log) {
        const char* home = getenv("HOME");
        char path[512];
        snprintf(path, sizeof path, "%s/Library/Logs/Velaria", home ? home : "/tmp");
        mkdir(path, 0755);
        snprintf(path, sizeof path, "%s/Library/Logs/Velaria/tap.log", home ? home : "/tmp");
        m_log = fopen(path, "a");
        startLogger();
        logLine("Velaria Sound active (sending to 127.0.0.1:%u)", (unsigned)kTapPort);
    }
}

void Processor::closeSink() {
    if (m_sock >= 0) { close(m_sock); m_sock = -1; }
    if (m_log) {
        logLine("Velaria Sound inactive");
        stopLogger();          // drains the ring into the file first
        fclose(m_log);
        m_log = nullptr;
    }
}

void Processor::sendBlock(const float* const* in, int channels, int frames, const ProcessContext* ctx) {
    if (m_sock < 0 || !m_scratch) return;
    // keep every datagram under ~60 KB: 16 ch x 4 B = 64 B a frame -> 900 frames; use 512
    const int kMaxFrames = 512;
    for (int off = 0; off < frames; off += kMaxFrames) {
        int n = std::min(kMaxFrames, frames - off);
        if ((size_t)n * (size_t)channels > m_scratchFloats) return;   // a block larger than setupProcessing promised
        TapPacket h{};
        h.magic = kTapMagic;
        h.version = 1;
        h.channels = (uint16_t)channels;
        h.sampleRate = (uint32_t)std::lround(m_sampleRate);
        h.flags = (ctx && (ctx->state & ProcessContext::kPlaying) ? 1u : 0u) | (ctx ? 2u : 0u) | (m_ambisonic ? 4u : 0u);
        h.projectTime = ctx ? ctx->projectTimeSamples + off : -1;
        h.frames = (uint32_t)n;
        h.seq = m_seq++;
        float* p = m_scratch;
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < channels; ++c) *p++ = in[c][off + i];
        iovec iov[2] = {{&h, sizeof h}, {m_scratch, sizeof(float) * (size_t)n * (size_t)channels}};
        msghdr m{};
        m.msg_iov = iov;
        m.msg_iovlen = 2;
        sendmsg(m_sock, &m, 0);   // nobody listening: ECONNREFUSED, ignored; the log line still says what flows
    }
}

void Processor::meter(const float* const* in, int channels, int frames, const ProcessContext* ctx) {
    for (int c = 0; c < channels; ++c) {
        double s = 0;
        const float* x = in[c];
        for (int i = 0; i < frames; ++i) s += (double)x[i] * x[i];
        m_sq[c] += s;
    }
    m_meterFrames += frames;
    m_blocks++;
    if (m_meterFrames < (int64_t)(m_sampleRate * 2)) return;
    char line[512];
    int o = snprintf(line, sizeof line, "levels (%d ch, %lld blocks):", channels, (long long)m_blocks);
    for (int c = 0; c < channels && o < (int)sizeof line - 16; ++c) {
        double ms = m_sq[c] / (double)m_meterFrames;
        if (ms < 1e-10) o += snprintf(line + o, sizeof line - o, " %d:-", c + 1);
        else o += snprintf(line + o, sizeof line - o, " %d:%.0f", c + 1, 10.0 * log10(ms));
        m_sq[c] = 0;
    }
    if (ctx)
        snprintf(line + o, sizeof line - o, " | host %s, project time %lld (%.2f s)%s",
                 (ctx->state & ProcessContext::kPlaying) ? "playing" : "stopped", (long long)ctx->projectTimeSamples,
                 ctx->projectTimeSamples / std::max(1.0, m_sampleRate),
                 (ctx->state & ProcessContext::kSystemTimeValid) ? "" : ", no system time");
    else
        snprintf(line + o, sizeof line - o, " | no process context from host");
    logLine("%s", line);
    m_meterFrames = 0;
}

// Log lines are formatted into a fixed ring on the calling thread (no allocation, no I/O: process() calls this
// every two seconds for the meter) and written to the file by a worker (review finding 8, 20 Nov: fprintf/fflush
// on the VST3 processing thread could glitch Fairlight when the filesystem stalled).
void Processor::logLine(const char* fmt, ...) {
    if (!m_log) return;
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char ts[32];
    strftime(ts, sizeof ts, "%H:%M:%S", &tmv);
    uint32_t w = m_logWrite.load(std::memory_order_relaxed);
    uint32_t r = m_logRead.load(std::memory_order_acquire);
    if (w - r >= kLogRing) { m_logDropped.fetch_add(1, std::memory_order_relaxed); return; }   // full: drop, count
    char* line = m_logRing[w % kLogRing];
    int o = snprintf(line, kLogLine, "%s ", ts);
    va_list ap;
    va_start(ap, fmt);
    if (o > 0 && o < (int)kLogLine) vsnprintf(line + o, kLogLine - (size_t)o, fmt, ap);
    va_end(ap);
    m_logWrite.store(w + 1, std::memory_order_release);
    m_logWake.notify_one();
}

void Processor::startLogger() {
    if (m_logThread.joinable()) return;
    m_logQuit = false;
    m_logThread = std::thread([this] {
        std::unique_lock<std::mutex> lk(m_logMutex);
        for (;;) {
            m_logWake.wait_for(lk, std::chrono::milliseconds(250));
            drainLog();
            if (m_logQuit) { drainLog(); return; }
        }
    });
}

void Processor::stopLogger() {
    if (!m_logThread.joinable()) return;
    { std::lock_guard<std::mutex> lk(m_logMutex); m_logQuit = true; }
    m_logWake.notify_one();
    m_logThread.join();
}

void Processor::drainLog() {
    // worker thread only; m_log is opened before the worker starts and closed after it stops
    uint32_t r = m_logRead.load(std::memory_order_relaxed);
    uint32_t w = m_logWrite.load(std::memory_order_acquire);
    bool any = r != w;
    for (; r != w; ++r) {
        if (m_log) { fputs(m_logRing[r % kLogRing], m_log); fputc('\n', m_log); }
        m_logRead.store(r + 1, std::memory_order_release);
    }
    uint32_t d = m_logDropped.exchange(0, std::memory_order_relaxed);
    if (d && m_log) { fprintf(m_log, "(%u log line(s) dropped: ring full)\n", d); any = true; }
    if (any && m_log) fflush(m_log);
}

// ---------------------------------------------------------------- state

static const char kStateMagic[8] = {'V', 'S', 'N', 'D', 0, 0, 0, 1};   // "VSND" + version 1

tresult PLUGIN_API Processor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    int32 written = 0;
    return state->write((void*)kStateMagic, sizeof kStateMagic, &written) == kResultOk && written == (int32)sizeof kStateMagic
               ? kResultOk : kResultFalse;
}

tresult PLUGIN_API Processor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    char buf[8] = {};
    int32 read = 0;
    state->read(buf, sizeof buf, &read);
    if (read >= 4 && memcmp(buf, kStateMagic, 4) == 0) logLine("state restored (version %d)", (int)buf[7]);
    else logLine("state: %d byte(s), not ours; defaults", (int)read);
    return kResultOk;   // nothing to configure either way; the insert stays
}

// ---------------------------------------------------------------- controller (no parameters, no view)

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    return EditControllerEx1::initialize(context);
}

tresult PLUGIN_API Controller::getState(IBStream* state) {
    if (!state) return kResultFalse;
    int32 written = 0;
    return state->write((void*)kStateMagic, sizeof kStateMagic, &written) == kResultOk ? kResultOk : kResultFalse;
}

}  // namespace velaria
