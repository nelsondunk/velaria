#include "tap.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <arpa/inet.h>
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
    free(m_scratch);
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
    AudioBusBuffers& in = data.inputs[0];
    AudioBusBuffers& out = data.outputs[0];
    int ch = std::min(in.numChannels, out.numChannels);
    if (ch <= 0) return kResultOk;
    // pass-through (the host may hand us the same buffers for in and out)
    for (int c = 0; c < ch; ++c)
        if (out.channelBuffers32[c] != in.channelBuffers32[c])
            memcpy(out.channelBuffers32[c], in.channelBuffers32[c], sizeof(float) * (size_t)data.numSamples);
    out.silenceFlags = in.silenceFlags;
    // Offline (Deliver) renders go through here too; the headset wants the live mix only.
    if (processSetup.processMode == kOffline) return kResultOk;
    int n = std::min(ch, kMaxChannels);
    meter(in.channelBuffers32, n, data.numSamples, data.processContext);
    sendBlock(in.channelBuffers32, n, data.numSamples, data.processContext);
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
        logLine("Velaria Sound active (sending to 127.0.0.1:%u)", (unsigned)kTapPort);
    }
}

void Processor::closeSink() {
    if (m_sock >= 0) { close(m_sock); m_sock = -1; }
    if (m_log) { logLine("Velaria Sound inactive"); fclose(m_log); m_log = nullptr; }
}

void Processor::sendBlock(const float* const* in, int channels, int frames, const ProcessContext* ctx) {
    if (m_sock < 0 || !m_scratch) return;
    // keep every datagram under ~60 KB: 16 ch x 4 B = 64 B a frame -> 900 frames; use 512
    const int kMaxFrames = 512;
    for (int off = 0; off < frames; off += kMaxFrames) {
        int n = std::min(kMaxFrames, frames - off);
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

void Processor::logLine(const char* fmt, ...) {
    if (!m_log) return;
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char ts[32];
    strftime(ts, sizeof ts, "%H:%M:%S", &tmv);
    fprintf(m_log, "%s ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(m_log, fmt, ap);
    va_end(ap);
    fputc('\n', m_log);
    fflush(m_log);
}

// ---------------------------------------------------------------- controller (no parameters, no view)

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    return EditControllerEx1::initialize(context);
}

}  // namespace velaria
