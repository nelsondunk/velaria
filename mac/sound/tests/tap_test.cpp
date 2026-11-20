// Host-side checks for Velaria Sound that the VST3 validator does not cover: null channel buffers on an inactive
// bus (review finding 1, 20 Nov 2026) and the pass-through staying exact. Built as a plain executable next to the
// plugin (see CMakeLists); run: build/tap/tap_test
#include "../src/tap.h"

#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main() {
    velaria::Processor* p = new velaria::Processor();
    p->initialize(nullptr);
    SpeakerArrangement arr = SpeakerArr::kAmbi1stOrderACN;
    CHECK(p->setBusArrangements(&arr, 1, &arr, 1) == kResultTrue);
    ProcessSetup setup{kRealtime, kSample32, 256, 48000.0};
    CHECK(p->setupProcessing(setup) == kResultOk);
    // not activated: no socket, no log file; process() must still be safe

    const int N = 128;
    std::vector<float> in[4], out[4];
    float* inp[4]; float* outp[4];
    for (int c = 0; c < 4; ++c) {
        in[c].assign(N, 0); out[c].assign(N, -1.0f);
        for (int i = 0; i < N; ++i) in[c][i] = std::sin(0.01f * i * (c + 1));
        inp[c] = in[c].data(); outp[c] = out[c].data();
    }
    AudioBusBuffers ib{}; ib.numChannels = 4; ib.channelBuffers32 = inp;
    AudioBusBuffers ob{}; ob.numChannels = 4; ob.channelBuffers32 = outp;
    ProcessData d{};
    d.processMode = kRealtime; d.symbolicSampleSize = kSample32; d.numSamples = N;
    d.numInputs = 1; d.numOutputs = 1; d.inputs = &ib; d.outputs = &ob;

    // 1. plain pass-through is exact
    CHECK(p->process(d) == kResultOk);
    for (int c = 0; c < 4; ++c) CHECK(memcmp(out[c].data(), in[c].data(), N * sizeof(float)) == 0);

    // 2. one null input channel -> that output is silence, the others still copy
    inp[2] = nullptr;
    for (int c = 0; c < 4; ++c) std::fill(out[c].begin(), out[c].end(), -1.0f);
    CHECK(p->process(d) == kResultOk);
    for (int i = 0; i < N; ++i) CHECK(out[2][i] == 0.0f);
    CHECK(memcmp(out[0].data(), in[0].data(), N * sizeof(float)) == 0);
    inp[2] = in[2].data();

    // 3. null output channel pointer -> skipped, no crash
    outp[1] = nullptr;
    CHECK(p->process(d) == kResultOk);
    outp[1] = out[1].data();

    // 4. null channel-buffer arrays (inactive bus) -> no crash, silence flags set for a missing input
    ib.channelBuffers32 = nullptr;
    CHECK(p->process(d) == kResultOk);
    CHECK(ob.silenceFlags == ~0ull);
    for (int i = 0; i < N; ++i) CHECK(out[0][i] == 0.0f);
    ib.channelBuffers32 = inp;
    ob.channelBuffers32 = nullptr;
    CHECK(p->process(d) == kResultOk);
    ob.channelBuffers32 = outp;

    // 5. null bus arrays
    d.inputs = nullptr;
    CHECK(p->process(d) == kResultOk);
    d.inputs = &ib;

    // 6. a block larger than promised is passed through but not tapped (no read past the silence buffer)
    std::vector<float> big(1024, 0.5f), bigOut(1024, 0.0f);
    float* bi[1] = {big.data()}; float* bo[1] = {bigOut.data()};
    AudioBusBuffers ib1{}; ib1.numChannels = 1; ib1.channelBuffers32 = bi;
    AudioBusBuffers ob1{}; ob1.numChannels = 1; ob1.channelBuffers32 = bo;
    d.inputs = &ib1; d.outputs = &ob1; d.numSamples = 1024;
    CHECK(p->process(d) == kResultOk);
    CHECK(bigOut[1023] == 0.5f);

    // 7. active: log ring + worker, meter crossing the 2 s line several times, then clean shutdown
    d.inputs = &ib; d.outputs = &ob; d.numSamples = N;
    p->setActive(true);
    for (int k = 0; k < 48000 * 5 / N; ++k) CHECK(p->process(d) == kResultOk);
    p->setActive(false);
    p->terminate();
    p->release();

    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    puts("tap_test: all checks passed");
    return 0;
}
