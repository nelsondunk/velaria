// Drives the virtual DeckLink like a playback app (Resolve-style scheduled playback) and checks timing.
// Usage: selftest <path/to/DeckLinkAPI.framework> [modeFourCC] [seconds]
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "DeckLinkAPI.h"
#include "DeckLinkAPIVideoOutput_v10_11.h"

static int g_failures = 0;
#define CHECK(cond, ...)                         \
    do {                                         \
        if (!(cond)) {                           \
            printf("FAIL: " __VA_ARGS__);        \
            printf("\n");                        \
            g_failures++;                        \
        } else {                                 \
            printf("ok:   " __VA_ARGS__);        \
            printf("\n");                        \
        }                                        \
    } while (0)

static int64_t nowNs() {
    static mach_timebase_info_data_t tb = [] {
        mach_timebase_info_data_t t;
        mach_timebase_info(&t);
        return t;
    }();
    return (int64_t)((__uint128_t)mach_absolute_time() * tb.numer / tb.denom);
}

typedef IDeckLinkIterator* (*CreateIteratorFunc)(void);
typedef IDeckLinkAPIInformation* (*CreateAPIInfoFunc)(void);

class Callback : public IDeckLinkVideoOutputCallback, public IDeckLinkAudioOutputCallback,
                 public IDeckLinkScreenPreviewCallback {
public:
    HRESULT QueryInterface(REFIID, LPVOID* ppv) override {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return 1; }
    ULONG Release() override { return 1; }
    HRESULT ScheduledFrameCompleted(IDeckLinkVideoFrame* f, BMDOutputFrameCompletionResult r) override {
        std::lock_guard<std::mutex> lk(m);
        completions.push_back({nowNs(), r});
        return S_OK;
    }
    HRESULT ScheduledPlaybackHasStopped() override {
        stopped = true;
        return S_OK;
    }
    HRESULT DrawFrame(IDeckLinkVideoFrame*) override {
        draws++;
        return S_OK;
    }
    std::atomic<int> draws{0};
    HRESULT RenderAudioSamples(bool preroll) override {
        audioCalls++;
        if (preroll) prerollCalls++;
        return S_OK;
    }
    std::mutex m;
    std::vector<std::pair<int64_t, BMDOutputFrameCompletionResult>> completions;
    std::atomic<bool> stopped{false};
    std::atomic<int> audioCalls{0}, prerollCalls{0};
};

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "/Library/Frameworks/DeckLinkAPI.framework";
    double seconds = argc > 3 ? atof(argv[3]) : 3.0;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)path, strlen(path), true);
    CFBundleRef bundle = CFBundleCreate(nullptr, url);
    CHECK(bundle != nullptr, "bundle loads from %s", path);
    if (!bundle) return 1;
    auto createIt = (CreateIteratorFunc)CFBundleGetFunctionPointerForName(bundle, CFSTR("CreateDeckLinkIteratorInstance_0004"));
    auto createInfo = (CreateAPIInfoFunc)CFBundleGetFunctionPointerForName(bundle, CFSTR("CreateDeckLinkAPIInformationInstance_0001"));
    CHECK(createIt && createInfo, "dispatch symbols present");
    if (!createIt || !createInfo) return 1;

    IDeckLinkAPIInformation* info = createInfo();
    int64_t ver = 0;
    CHECK(info && info->GetInt(BMDDeckLinkAPIVersion, &ver) == S_OK && ver > 0, "API version 0x%08llx", (long long)ver);
    if (info) info->Release();

    IDeckLinkIterator* it = createIt();
    IDeckLink* dl = nullptr;
    CHECK(it && it->Next(&dl) == S_OK && dl, "iterator yields a device");
    IDeckLink* none = nullptr;
    CHECK(it->Next(&none) == S_FALSE, "iterator yields exactly one device");
    it->Release();

    CFStringRef name = nullptr;
    dl->GetDisplayName(&name);
    char nbuf[128] = {0};
    if (name) {
        CFStringGetCString(name, nbuf, sizeof nbuf, kCFStringEncodingUTF8);
        CFRelease(name);
    }
    CHECK(strlen(nbuf) > 0, "display name '%s'", nbuf);

    IDeckLinkProfileAttributes* attr = nullptr;
    CHECK(dl->QueryInterface(IID_IDeckLinkProfileAttributes, (void**)&attr) == S_OK, "attributes interface");
    int64_t io = 0;
    attr->GetInt(BMDDeckLinkVideoIOSupport, &io);
    CHECK(io & bmdDeviceSupportsPlayback, "supports playback");
    attr->Release();

    IDeckLinkInput* in = nullptr;
    CHECK(dl->QueryInterface(IID_IDeckLinkInput, (void**)&in) == E_NOINTERFACE, "no input interface (output-only)");

    IDeckLinkOutput_v10_11* out1011 = nullptr;
    CHECK(dl->QueryInterface(IID_IDeckLinkOutput_v10_11, (void**)&out1011) == S_OK && out1011, "10.11 output interface (Resolve)");
    if (out1011) {
        BMDDisplayModeSupport_v10_11 sup = 0;
        IDeckLinkDisplayMode* rm = nullptr;
        out1011->DoesSupportVideoMode(bmdMode4K2160p2398, bmdFormat10BitYUV, bmdVideoOutputFlagDefault, &sup, &rm);
        CHECK(sup == bmdDisplayModeSupported_v10_11 && rm && rm->GetWidth() == 3840, "10.11 DoesSupportVideoMode UHD");
        if (rm) rm->Release();
        out1011->Release();
    }
    IDeckLinkOutput* out = nullptr;
    CHECK(dl->QueryInterface(IID_IDeckLinkOutput, (void**)&out) == S_OK && out, "output interface");

    IDeckLinkDisplayModeIterator* mit = nullptr;
    out->GetDisplayModeIterator(&mit);
    int nmodes = 0;
    IDeckLinkDisplayMode* dm = nullptr;
    while (mit->Next(&dm) == S_OK) {
        nmodes++;
        dm->Release();
    }
    mit->Release();
    CHECK(nmodes >= 40, "%d display modes", nmodes);

    BMDDisplayMode mode = bmdMode8K4320p2398;
    if (argc > 2 && strlen(argv[2]) == 4) mode = (argv[2][0] << 24) | (argv[2][1] << 16) | (argv[2][2] << 8) | argv[2][3];
    bool supported = false;
    BMDDisplayMode actual = 0;
    out->DoesSupportVideoMode(bmdVideoConnectionUnspecified, mode, bmdFormat10BitYUV, bmdNoVideoOutputConversion,
                              bmdSupportedVideoModeDefault, &actual, &supported);
    CHECK(supported && actual == mode, "mode supported in 10-bit YUV");
    out->DoesSupportVideoMode(bmdVideoConnectionUnspecified, bmdModePAL, bmdFormat10BitYUV, bmdNoVideoOutputConversion,
                              bmdSupportedVideoModeDefault, &actual, &supported);
    CHECK(!supported, "PAL correctly unsupported");

    IDeckLinkDisplayMode* m = nullptr;
    out->GetDisplayMode(mode, &m);
    BMDTimeValue dur;
    BMDTimeScale scale;
    m->GetFrameRate(&dur, &scale);
    long w = m->GetWidth(), h = m->GetHeight();
    m->Release();
    printf("      mode %ldx%ld @ %lld/%lld\n", w, h, (long long)dur, (long long)scale);

    Callback cb;
    CHECK(out->EnableVideoOutput(mode, bmdVideoOutputRP188) == S_OK, "EnableVideoOutput");
    out->SetScheduledFrameCompletionCallback(&cb);
    out->SetScreenPreviewCallback(&cb);
    CHECK(out->EnableAudioOutput(bmdAudioSampleRate48kHz, bmdAudioSampleType32bitInteger, 16, bmdAudioOutputStreamTimestamped) == S_OK,
          "EnableAudioOutput 16ch");
    out->SetAudioCallback(&cb);

    long rowBytes = ((w + 47) / 48) * 128;
    const int kPool = 8;
    std::vector<IDeckLinkMutableVideoFrame*> pool;
    for (int i = 0; i < kPool; i++) {
        IDeckLinkMutableVideoFrame* f = nullptr;
        out->CreateVideoFrame((int32_t)w, (int32_t)h, (int32_t)rowBytes, bmdFormat10BitYUV, bmdFrameFlagDefault, &f);
        if (f) {
            void* b = nullptr;
            f->GetBytes(&b);
            memset(b, 0x80, (size_t)rowBytes * h);
            f->SetTimecodeFromComponents(bmdTimecodeRP188Any, 1, 0, 0, (uint8_t)i, bmdTimecodeFlagDefault);
            pool.push_back(f);
        }
    }
    CHECK((int)pool.size() == kPool, "created %d v210 frames (%ld rowbytes)", kPool, rowBytes);

    int total = (int)std::lround(seconds * scale / dur);
    // Preroll like Resolve: schedule kPool frames, audio preroll, start.
    int scheduled = 0;
    for (; scheduled < kPool && scheduled < total; scheduled++)
        out->ScheduleVideoFrame(pool[scheduled], scheduled * dur, dur, scale);
    uint32_t buffered = 0;
    out->GetBufferedVideoFrameCount(&buffered);
    CHECK(buffered == (uint32_t)kPool, "%u frames buffered before start", buffered);

    std::vector<int32_t> audio(1600 * 16, 0);
    uint32_t written = 0;
    out->BeginAudioPreroll();
    usleep(30000);
    out->ScheduleAudioSamples(audio.data(), 1600, 0, 48000, &written);
    out->EndAudioPreroll();
    CHECK(written == 1600, "audio preroll accepted %u samples", written);
    CHECK(cb.prerollCalls > 0, "RenderAudioSamples(preroll) called %d times", (int)cb.prerollCalls);

    int64_t start = nowNs();
    CHECK(out->StartScheduledPlayback(0, scale, 1.0) == S_OK, "StartScheduledPlayback");

    // Keep the pipeline fed as frames complete (reuse pool round-robin).
    int completedSeen = 0;
    while (scheduled < total) {
        usleep(2000);
        std::lock_guard<std::mutex> lk(cb.m);
        while (completedSeen < (int)cb.completions.size() && scheduled < total) {
            out->ScheduleVideoFrame(pool[scheduled % kPool], scheduled * dur, dur, scale);
            scheduled++;
            completedSeen++;
        }
    }
    // wait for the rest
    int64_t deadline = nowNs() + (int64_t)((kPool + 4) * 1e9 * dur / scale);
    while (nowNs() < deadline) {
        {
            std::lock_guard<std::mutex> lk(cb.m);
            if ((int)cb.completions.size() >= total) break;
        }
        usleep(5000);
    }
    BMDTimeValue st = 0;
    double sp = 0;
    out->GetScheduledStreamTime(scale, &st, &sp);
    CHECK(sp == 1.0 && st > 0, "stream time advancing (%.3fs)", (double)st / scale);

    // Timing analysis
    std::vector<double> errMs;
    int late = 0;
    {
        std::lock_guard<std::mutex> lk(cb.m);
        for (size_t i = 0; i < cb.completions.size(); i++) {
            double expected = start + i * 1e9 * dur / scale;
            errMs.push_back((cb.completions[i].first - expected) / 1e6);
            if (cb.completions[i].second != bmdOutputFrameCompleted) late++;
        }
    }
    CHECK((int)errMs.size() == total, "%zu/%d frames completed", errMs.size(), total);
    double maxErr = 0, sum = 0;
    for (double e : errMs) {
        maxErr = std::max(maxErr, std::fabs(e));
        sum += e;
    }
    double mean = errMs.empty() ? 0 : sum / errMs.size();
    CHECK(maxErr < 4.0, "scan-out timing: mean %.3f ms, max |err| %.3f ms", mean, maxErr);
    CHECK(late == 0, "%d late/dropped frames", late);
    CHECK(cb.draws >= total - 1, "screen preview DrawFrame called for displayed frames (%d/%d)", (int)cb.draws, total);
    CHECK(cb.audioCalls > 0, "RenderAudioSamples called during playback (%d)", (int)cb.audioCalls);

    BMDTimeValue actualStop = 0;
    out->StopScheduledPlayback(0, &actualStop, scale);
    for (int i = 0; i < 100 && !cb.stopped; i++) usleep(5000);
    CHECK(cb.stopped, "ScheduledPlaybackHasStopped delivered");
    bool running = true;
    out->IsScheduledPlaybackRunning(&running);
    CHECK(!running, "playback not running after stop");

    // Paused/scrub path
    CHECK(out->DisplayVideoFrameSync(pool[0]) == S_OK, "DisplayVideoFrameSync");

    out->DisableAudioOutput();
    CHECK(out->DisableVideoOutput() == S_OK, "DisableVideoOutput");
    for (auto* f : pool) f->Release();
    out->Release();
    dl->Release();
    printf("\n%s: %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
