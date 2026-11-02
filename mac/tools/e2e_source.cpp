// Drives the VR Link virtual card exactly like Resolve does (10.11 output interface, scheduled playback,
// preview callback) with synthetic frames. Each frame carries its frame number as a 24-bit barcode inside the
// active picture so a receiver can verify every frame arrives exactly once and in order.
// Usage: e2e_source <framework> [mode=8k23] [seconds=20] [activeW=5760] [activeH=2880] [paused=0] [restarts=0]
// E2E_V210_FILE=clip.v210 fills the active picture with real footage (raw v210, activeW x activeH, frames looped)
// instead of the synthetic gradient, so bitrate and quality can be checked on real content.
#include <CoreFoundation/CoreFoundation.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include "DeckLinkAPI.h"
#include "DeckLinkAPIVideoOutput_v10_11.h"

typedef IDeckLinkIterator* (*CreateIteratorFunc)(void);

// Barcode geometry, in fractions of the active picture (left eye region for SBS).
static const int kBits = 24;
void barcodeCell(int bit, int aw, int ah, int& x0, int& y0, int& x1, int& y1) {
    double cw = aw * 0.40 / kBits;   // 24 cells across 40% of the width
    x0 = (int)(aw * 0.05 + bit * cw);
    x1 = (int)(x0 + cw * 0.8);
    y0 = (int)(ah * 0.45);
    y1 = (int)(ah * 0.55);
}

static inline void putV210Group(uint32_t* g, uint32_t Y, uint32_t Cb, uint32_t Cr) {
    g[0] = Cb | (Y << 10) | (Cr << 20);
    g[1] = Y | (Cb << 10) | (Y << 20);
    g[2] = Cr | (Y << 10) | (Cb << 20);
    g[3] = Y | (Cr << 10) | (Y << 20);
}

struct Footage {
    const uint8_t* data = nullptr;
    size_t frameBytes = 0, frames = 0;
    long rowBytes = 0;
};
static Footage g_footage;

void paint(uint8_t* base, int W, int H, long rb, int ax, int ay, int aw, int ah, uint32_t frameNo) {
    if (g_footage.data) {
        const uint8_t* src = g_footage.data + (size_t)((frameNo - 1) % g_footage.frames) * g_footage.frameBytes;
        for (int y = 0; y < H; y++) {
            uint32_t* row = (uint32_t*)(base + (long)y * rb);
            if (y < ay || y >= ay + ah) {
                for (int gx = 0; gx < W / 6; gx++) putV210Group(row + gx * 4, 64, 512, 512);
                continue;
            }
            for (int gx = 0; gx < ax / 6; gx++) putV210Group(row + gx * 4, 64, 512, 512);
            memcpy((uint8_t*)row + (ax / 6) * 16, src + (size_t)(y - ay) * g_footage.rowBytes, (size_t)(aw / 6) * 16);
            for (int gx = (ax + aw) / 6; gx < W / 6; gx++) putV210Group(row + gx * 4, 64, 512, 512);
        }
        for (int b = 0; b < kBits; b++) {   // barcode on top, as below
            int x0, y0, x1, y1;
            barcodeCell(b, aw, ah, x0, y0, x1, y1);
            uint32_t Y = ((frameNo >> (kBits - 1 - b)) & 1) ? 940 : 64;
            for (int y = ay + y0; y < ay + y1; y++) {
                uint32_t* row = (uint32_t*)(base + (long)y * rb);
                for (int x = ((ax + x0 + 5) / 6) * 6; x + 6 <= ax + x1; x += 6) putV210Group(row + (x / 6) * 4, Y, 512, 512);
            }
        }
        return;
    }
    // background raster: video black; active picture: a soft gradient with a moving bar + the barcode
    for (int y = 0; y < H; y++) {
        uint32_t* row = (uint32_t*)(base + (long)y * rb);
        for (int gx = 0; gx < W / 6; gx++) {
            int x = gx * 6;
            if (x < ax || x >= ax + aw || y < ay || y >= ay + ah) {
                putV210Group(row + gx * 4, 64, 512, 512);
                continue;
            }
            int lx = x - ax, ly = y - ay;
            uint32_t Y = 200 + (uint32_t)((lx * 300) / aw) + (uint32_t)((ly * 200) / ah);
            int bar = (int)((frameNo * 16) % (uint32_t)aw);
            if (lx >= bar && lx < bar + 48) Y = 900;
            putV210Group(row + gx * 4, Y, 512 + ((ly * 64) / ah), 512 - ((lx * 64) / aw));
        }
    }
    for (int b = 0; b < kBits; b++) {
        int x0, y0, x1, y1;
        barcodeCell(b, aw, ah, x0, y0, x1, y1);
        uint32_t Y = ((frameNo >> (kBits - 1 - b)) & 1) ? 940 : 64;
        for (int y = ay + y0; y < ay + y1; y++) {
            uint32_t* row = (uint32_t*)(base + (long)y * rb);
            for (int x = ((ax + x0 + 5) / 6) * 6; x + 6 <= ax + x1; x += 6) putV210Group(row + (x / 6) * 4, Y, 512, 512);
        }
    }
}

struct Frame : public IDeckLinkVideoFrame {
    int w, h;
    long rb;
    uint8_t* bytes;
    std::atomic<int> refs{1};
    uint32_t number = 0;
    HRESULT QueryInterface(REFIID, LPVOID* ppv) override {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++refs; }
    ULONG Release() override { return --refs; }   // pool-owned
    long GetWidth() override { return w; }
    long GetHeight() override { return h; }
    long GetRowBytes() override { return rb; }
    BMDPixelFormat GetPixelFormat() override { return bmdFormat10BitYUV; }
    BMDFrameFlags GetFlags() override { return 0; }
    HRESULT GetBytes(void** b) override {
        *b = bytes;
        return S_OK;
    }
    HRESULT GetTimecode(BMDTimecodeFormat, IDeckLinkTimecode** t) override {
        *t = nullptr;
        return S_FALSE;
    }
    HRESULT GetAncillaryData(IDeckLinkVideoFrameAncillary** a) override {
        *a = nullptr;
        return S_FALSE;
    }
};

struct Cb : public IDeckLinkVideoOutputCallback {
    std::atomic<int> completed{0}, late{0};
    HRESULT QueryInterface(REFIID, LPVOID* ppv) override {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return 1; }
    ULONG Release() override { return 1; }
    HRESULT ScheduledFrameCompleted(IDeckLinkVideoFrame*, BMDOutputFrameCompletionResult r) override {
        completed++;
        if (r != bmdOutputFrameCompleted) late++;
        return S_OK;
    }
    HRESULT ScheduledPlaybackHasStopped() override { return S_OK; }
};

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "/Library/Frameworks/DeckLinkAPI.framework";
    const char* modeStr = argc > 2 ? argv[2] : "8k23";
    double seconds = argc > 3 ? atof(argv[3]) : 20;
    int aw = argc > 4 ? atoi(argv[4]) : 5760;
    int ah = argc > 5 ? atoi(argv[5]) : 2880;
    bool paused = argc > 6 && atoi(argv[6]) != 0;
    BMDDisplayMode mode = (modeStr[0] << 24) | (modeStr[1] << 16) | (modeStr[2] << 8) | modeStr[3];

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)path, strlen(path), true);
    CFBundleRef bundle = CFBundleCreate(nullptr, url);
    auto createIt = bundle ? (CreateIteratorFunc)CFBundleGetFunctionPointerForName(bundle, CFSTR("CreateDeckLinkIteratorInstance_0004")) : nullptr;
    if (!createIt) {
        fprintf(stderr, "cannot load %s\n", path);
        return 1;
    }
    IDeckLinkIterator* it = createIt();
    IDeckLink* dl = nullptr;
    it->Next(&dl);
    IDeckLinkOutput_v10_11* out = nullptr;
    if (!dl || dl->QueryInterface(IID_IDeckLinkOutput_v10_11, (void**)&out) != S_OK) {
        fprintf(stderr, "no output\n");
        return 1;
    }
    IDeckLinkDisplayMode* dm = nullptr;
    BMDDisplayModeSupport_v10_11 sup;
    out->DoesSupportVideoMode(mode, bmdFormat10BitYUV, 0, &sup, &dm);
    if (!dm) {
        fprintf(stderr, "mode unsupported\n");
        return 1;
    }
    int W = (int)dm->GetWidth(), H = (int)dm->GetHeight();
    BMDTimeValue dur;
    BMDTimeScale scale;
    dm->GetFrameRate(&dur, &scale);
    long rb = ((W + 47) / 48) * 128;
    if (const char* fp = getenv("E2E_V210_FILE")) {
        int fd = open(fp, O_RDONLY);
        struct stat stt {};
        if (fd < 0 || fstat(fd, &stt) != 0) {
            fprintf(stderr, "cannot open %s\n", fp);
            return 1;
        }
        g_footage.rowBytes = ((aw + 47) / 48) * 128;   // ffmpeg v210 rows are padded to 128 bytes per 48 pixels
        g_footage.frameBytes = (size_t)g_footage.rowBytes * ah;
        g_footage.frames = (size_t)stt.st_size / g_footage.frameBytes;
        g_footage.data = (const uint8_t*)mmap(nullptr, (size_t)stt.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (g_footage.data == MAP_FAILED || g_footage.frames == 0) {
            fprintf(stderr, "bad footage file %s\n", fp);
            return 1;
        }
        printf("source: real footage %s, %zu frames of %dx%d\n", fp, g_footage.frames, aw, ah);
    }
    int ax = ((W - aw) / 2) / 6 * 6, ay = (H - ah) / 2;
    printf("source: %dx%d @ %lld/%lld, active %dx%d+%d+%d, %.0f s%s\n", W, H, (long long)dur, (long long)scale, aw, ah, ax,
           ay, seconds, paused ? " (paused/scrub frames)" : "");

    const int kPool = 10;
    std::vector<Frame*> pool;
    for (int i = 0; i < kPool; i++) {
        auto* f = new Frame();
        f->w = W;
        f->h = H;
        f->rb = rb;
        posix_memalign((void**)&f->bytes, 4096, (size_t)rb * H);
        pool.push_back(f);
    }
    Cb cb;
    out->SetScheduledFrameCompletionCallback(&cb);
    out->EnableVideoOutput(mode, bmdVideoOutputRP188);

    // Like Resolve: keep the stream running, schedule frames ~6 frames ahead of the stream clock.
    out->StartScheduledPlayback(0, scale, 1.0);
    int total = (int)std::lround(seconds * scale / dur);
    int lead = getenv("E2E_LEAD_FRAMES") ? atoi(getenv("E2E_LEAD_FRAMES")) : 3;   // Resolve schedules ~3 frames (60-90 ms) ahead
    usleep(1500000);   // give a receiver time to connect to the card's server
    BMDTimeValue t0;
    double sp;
    out->GetScheduledStreamTime(scale, &t0, &sp);
    t0 = (t0 / dur + lead) * dur;
    int restarts = argc > 7 ? atoi(argv[7]) : 0;   // emulate Resolve's stop -> immediate restart -> park -> play
    int segment = restarts > 0 ? total / (restarts + 1) : total + 1;
    int segStart = 0;
    for (int n = 0; n < total; n++) {
        if (n > 0 && n % segment == 0 && restarts > 0) {
            BMDTimeValue stopAt;
            out->StopScheduledPlayback(0, &stopAt, scale);
            out->StartScheduledPlayback(0, scale, 1.0);   // Resolve keeps the stream running while parked
            printf("source: stop/restart after frame %d\n", n);
            usleep(600000);
            BMDTimeValue now;
            out->GetScheduledStreamTime(scale, &now, &sp);
            t0 = (now / dur + lead) * dur;
            segStart = n;
        }
        Frame* f = pool[n % kPool];
        // wait for the pool slot to come back (completed) before repainting it
        while (f->refs.load() > 1) usleep(500);
        f->number = (uint32_t)(n + 1);
        paint(f->bytes, W, H, rb, ax, ay, aw, ah, f->number);
        if (paused) {
            out->DisplayVideoFrameSync(f);
            usleep(200000);
            continue;
        }
        // keep at most `lead` frames queued
        while (true) {
            BMDTimeValue now;
            out->GetScheduledStreamTime(scale, &now, &sp);
            if (t0 + (n - segStart) * dur - now <= lead * dur) break;
            usleep(1000);
        }
        out->ScheduleVideoFrame(f, t0 + (n - segStart) * dur, dur, scale);
    }
    usleep((useconds_t)((lead + 4) * 1e6 * dur / scale));
    BMDTimeValue stopAt;
    out->StopScheduledPlayback(0, &stopAt, scale);
    usleep(300000);
    printf("source: scheduled %d frames, completed %d, late %d\n", total, (int)cb.completed, (int)cb.late);
    out->DisableVideoOutput();
    out->Release();
    usleep(500000);
    return 0;
}
