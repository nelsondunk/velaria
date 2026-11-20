// Velaria Sound — VST3 insert: audio passes through untouched; a copy of every channel plus the host's timeline
// position goes to the Velaria driver over UDP on this Mac (127.0.0.1:47813), and a level line goes to
// ~/Library/Logs/Velaria/tap.log every two seconds so a first insert can be judged without the headset.
//
// Why: Fairlight will not patch an Ambisonics bus to any hardware output, but it does accept a third-party VST3 on
// that bus and feeds it the raw channels after the panner (checked 19 Nov 2026 with Ambi Head HD, which reported
// "ambiX O1"). A plugin whose output count is smaller than the bus starves everything after it, so this one is
// strictly N in, N out, same arrangement.
#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/base/funknown.h"

#include <cstdint>
#include <cstdio>

namespace velaria {

// Class IDs (generated 19 Nov 2026; never change once a project has saved an instance)
static const Steinberg::FUID kProcessorUID(0xF643FD0E, 0x4FF14D82, 0xBDC7E4CE, 0x3B47D62C);
static const Steinberg::FUID kControllerUID(0xCC0B3643, 0xC6054225, 0x91890FA0, 0x5FC7FDAC);

constexpr int kMaxChannels = 16;                 // third-order ambisonics; the driver meters 16 too
constexpr uint16_t kTapPort = 47813;             // UDP, this Mac only (the driver's headset server is TCP 47810)
constexpr uint32_t kTapMagic = 0x50415456;       // 'VTAP' little-endian

// One UDP datagram: this header, then `frames` x `channels` float32 samples, interleaved, native byte order.
struct TapPacket {
    uint32_t magic;
    uint16_t version;        // 1
    uint16_t channels;       // 1..16
    uint32_t sampleRate;
    uint32_t flags;          // bit 0: host is playing; bit 1: projectTime valid; bit 2: arrangement is ambisonic (ACN)
    int64_t projectTime;     // first sample of this block, in samples on the host timeline
    uint32_t frames;
    uint32_t seq;            // packet counter, so the receiver can see loss
};

class Processor : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    ~Processor() override;
    static Steinberg::FUnknown* create(void*) { return (Steinberg::Vst::IAudioProcessor*)new Processor(); }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) override { return Steinberg::kResultOk; }

private:
    void openSink();
    void closeSink();
    void sendBlock(const float* const* in, int channels, int frames, const Steinberg::Vst::ProcessContext* ctx);
    void meter(const float* const* in, int channels, int frames, const Steinberg::Vst::ProcessContext* ctx);
    void logLine(const char* fmt, ...);

    int m_sock = -1;
    FILE* m_log = nullptr;
    double m_sampleRate = 48000.0;
    uint32_t m_seq = 0;
    bool m_ambisonic = true;
    // level meter, like the driver's: mean square per channel over ~2 s
    double m_sq[kMaxChannels] = {};
    int64_t m_meterFrames = 0;
    int64_t m_blocks = 0;
    // interleave scratch, sized for the largest block seen (allocated outside process once we know maxSamplesPerBlock)
    float* m_scratch = nullptr;
    size_t m_scratchFloats = 0;
};

class Controller : public Steinberg::Vst::EditControllerEx1 {
public:
    static Steinberg::FUnknown* create(void*) { return (Steinberg::Vst::IEditController*)new Controller(); }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
};

}  // namespace velaria
