// Standalone HEVC decode test for the Quest (runs from `adb shell`, no app/VR needed).
// Feeds a raw Annex-B dump captured by `probe` (PROBE_DUMP=...) through the same AMediaCodec setup the app uses and
// reports what the hardware decoder does with it.
//   dectest <file.hevc> <width> <height> [mode=buffer|p010|private] [snapFrame=30] [out.pgm]
// buffer : ByteBuffer output (linear), luma of frame snapFrame written as a 1/4-scale 8-bit PGM
// p010   : AImageReader YCBCR_P010 + CPU read (surface path, linear), same snapshot
// private: AImageReader PRIVATE + GPU_SAMPLED exactly like the app (counts frames only)
// gpu    : like private, plus each frame goes through the app's EGLImage/external-sampler path and is read back:
//          prints one char per frame ('.' clean, 'X' mostly black = corrupt) and writes a snapshot of snapFrame.
//          env HOLD=n keeps n images acquired (like the app), PACE=1 feeds in real time at 29.97 fps,
//          CACHE=0 disables the pointer-keyed EGLImage cache.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

static int64_t nowNs() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

struct Nal {
    size_t start, end;   // payload (after start code)
    int type;
};

static std::vector<Nal> splitNals(const std::vector<uint8_t>& d) {
    std::vector<Nal> out;
    size_t n = d.size(), i = 0;
    auto findSC = [&](size_t from, size_t& scLen) -> size_t {
        for (size_t k = from; k + 3 <= n; k++) {
            if (d[k] == 0 && d[k + 1] == 0 && d[k + 2] == 1) {
                scLen = 3;
                return k;
            }
        }
        scLen = 0;
        return n;
    };
    size_t sc;
    i = findSC(0, sc);
    while (i < n) {
        size_t s = i + sc, scn;
        size_t e = findSC(s, scn);
        size_t realEnd = e;
        while (realEnd > s && d[realEnd - 1] == 0 && e < n) realEnd--;   // trailing zero of a 4-byte start code
        if (s < n) out.push_back({s, realEnd, (d[s] >> 1) & 0x3f});
        i = e;
        sc = scn;
    }
    return out;
}


// ---------------------------------------------------------------- GPU readback path (mirrors quest/src/gfx.cpp)
struct Gpu {
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLContext ctx;
    EGLSurface surf;
    GLuint prog = 0, vao = 0, fbo = 0, tex = 0, fboBig = 0, texBig = 0;
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC getCB;
    PFNEGLCREATEIMAGEKHRPROC createImage;
    PFNEGLDESTROYIMAGEKHRPROC destroyImage;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC imageTarget;
    struct Imported {
        EGLImageKHR img;
        GLuint tex;
        uint64_t id;
    };
    std::map<AHardwareBuffer*, Imported> cache;
    bool useCache = true;
    int reuse = 0;
    static const int kS = 128, kB = 960;

    static GLuint sh(GLenum t, const char* src) {
        GLuint s = glCreateShader(t);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char l[1024];
            glGetShaderInfoLog(s, sizeof l, nullptr, l);
            printf("shader: %s\n", l);
        }
        return s;
    }
    bool init() {
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (!eglInitialize(dpy, nullptr, nullptr)) return false;
        const EGLint ca[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_RENDERABLE_TYPE,
                             EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
        EGLConfig cfg;
        EGLint n;
        if (!eglChooseConfig(dpy, ca, &cfg, 1, &n) || n < 1) return false;
        const EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xa);
        const EGLint pa[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        surf = eglCreatePbufferSurface(dpy, cfg, pa);
        if (!eglMakeCurrent(dpy, surf, surf, ctx)) return false;
        getCB = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");
        createImage = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
        destroyImage = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
        imageTarget = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        const char* vs = "#version 300 es\nout vec2 vUv;\nvoid main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));"
                         "vUv=vec2(p.x,1.0-p.y);gl_Position=vec4(p*2.0-1.0,0.0,1.0);}\n";
        const char* fs = "#version 300 es\n#extension GL_OES_EGL_image_external_essl3 : require\nprecision highp float;\n"
                         "uniform samplerExternalOES uTex;in vec2 vUv;out vec4 o;void main(){o=vec4(clamp(texture(uTex,vUv).rgb,0.0,1.0),1.0);}\n";
        prog = glCreateProgram();
        glAttachShader(prog, sh(GL_VERTEX_SHADER, vs));
        glAttachShader(prog, sh(GL_FRAGMENT_SHADER, fs));
        glLinkProgram(prog);
        glGenVertexArrays(1, &vao);
        auto mk = [](GLuint& t, GLuint& f, int s) {
            glGenTextures(1, &t);
            glBindTexture(GL_TEXTURE_2D, t);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, s, s);
            glGenFramebuffers(1, &f);
            glBindFramebuffer(GL_FRAMEBUFFER, f);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
        };
        mk(tex, fbo, kS);
        mk(texBig, fboBig, kB);
        printf("GL: %s\n", glGetString(GL_RENDERER));
        return true;
    }
    GLuint import(AHardwareBuffer* ahb, uint64_t id) {
        auto it = cache.find(ahb);
        if (useCache && it != cache.end()) {
            if (it->second.id != id) reuse++;   // same pointer, different buffer: the app would sample a stale image
            return it->second.tex;
        }
        if (it != cache.end()) {
            glDeleteTextures(1, &it->second.tex);
            destroyImage(dpy, it->second.img);
            cache.erase(it);
        }
        const EGLint attrs[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
        EGLImageKHR img = createImage(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, getCB(ahb), attrs);
        if (img == EGL_NO_IMAGE_KHR) {
            printf("eglCreateImageKHR failed 0x%x\n", eglGetError());
            return 0;
        }
        GLuint t;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, t);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        imageTarget(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)img);
        cache[ahb] = {img, t, id};
        return t;
    }
    // returns fraction of near-black pixels
    double measure(AHardwareBuffer* ahb, uint64_t id, const char* snapPath) {
        GLuint t = import(ahb, id);
        if (!t) return -1;
        bool big = snapPath != nullptr;
        int s = big ? kB : kS;
        glBindFramebuffer(GL_FRAMEBUFFER, big ? fboBig : fbo);
        glViewport(0, 0, s, s);
        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, t);
        glUniform1i(glGetUniformLocation(prog, "uTex"), 0);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        std::vector<uint8_t> px((size_t)s * s * 4);
        glReadPixels(0, 0, s, s, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        size_t black = 0;
        for (size_t i = 0; i < (size_t)s * s; i++)
            if (std::max({px[i * 4], px[i * 4 + 1], px[i * 4 + 2]}) < 12) black++;
        if (big) {
            FILE* o = fopen(snapPath, "wb");
            if (o) {
                fprintf(o, "P6\n%d %d\n255\n", s, s);
                for (int y = s - 1; y >= 0; y--)   // GL rows are bottom-up
                    for (int x = 0; x < s; x++) fwrite(&px[((size_t)y * s + x) * 4], 1, 3, o);
                fclose(o);
            }
        }
        return (double)black / ((double)s * s);
    }
};

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: dectest file.hevc width height [buffer|p010|private] [snapFrame] [out.pgm]\n");
        return 2;
    }
    const char* path = argv[1];
    int W = atoi(argv[2]), H = atoi(argv[3]);
    std::string mode = argc > 4 ? argv[4] : "buffer";
    int snapFrame = argc > 5 ? atoi(argv[5]) : 30;
    std::string snapPath = argc > 6 ? argv[6] : "/data/local/tmp/dectest.pgm";

    FILE* f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return 2;
    }
    std::vector<uint8_t> data;
    uint8_t tmp[1 << 16];
    size_t r;
    while ((r = fread(tmp, 1, sizeof tmp, f)) > 0) data.insert(data.end(), tmp, tmp + r);
    fclose(f);

    // group NAL units into access units: a VCL NAL with first_slice_segment_in_pic_flag starts a new picture,
    // and parameter sets / AUD / prefix SEI that follow a VCL NAL belong to the next picture.
    auto nals = splitNals(data);
    std::vector<std::vector<uint8_t>> aus;
    std::vector<uint8_t> csd, cur;
    bool curHasVcl = false;
    int slicesInCur = 0, maxSlices = 0;
    for (auto& nl : nals) {
        bool vcl = nl.type < 32;
        bool firstSlice = vcl && nl.end - nl.start > 2 && (data[nl.start + 2] & 0x80);
        bool prefix = nl.type == 32 || nl.type == 33 || nl.type == 34 || nl.type == 35 || nl.type == 39;
        if (curHasVcl && (firstSlice || prefix)) {
            aus.push_back(cur);
            maxSlices = std::max(maxSlices, slicesInCur);
            cur.clear();
            curHasVcl = false;
            slicesInCur = 0;
        }
        static const uint8_t sc4[4] = {0, 0, 0, 1};
        cur.insert(cur.end(), sc4, sc4 + 4);
        cur.insert(cur.end(), data.begin() + nl.start, data.begin() + nl.end);
        if (nl.type >= 32 && nl.type <= 34 && aus.empty() && !curHasVcl) {
            csd.insert(csd.end(), sc4, sc4 + 4);
            csd.insert(csd.end(), data.begin() + nl.start, data.begin() + nl.end);
        }
        if (vcl) {
            curHasVcl = true;
            slicesInCur++;
        }
    }
    if (curHasVcl) aus.push_back(cur);
    printf("dectest: %zu bytes, %zu NAL units, %zu access units, max slice segments per picture %d, csd %zu B\n",
           data.size(), nals.size(), aus.size(), maxSlices, csd.size());

    AImageReader* reader = nullptr;
    ANativeWindow* window = nullptr;
    Gpu gpu;
    if (mode == "gpu") {
        if (!gpu.init()) {
            printf("EGL init failed\n");
            return 1;
        }
        gpu.useCache = !getenv("CACHE") || atoi(getenv("CACHE")) != 0;
    }
    int holdN = getenv("HOLD") ? atoi(getenv("HOLD")) : 4;
    bool pace = getenv("PACE") && atoi(getenv("PACE")) != 0;
    std::deque<AImage*> held;
    std::string marks;
    AHardwareBuffer_Desc lastDesc{};
    if (mode == "p010" || mode == "private" || mode == "gpu") {
        int32_t fmt = mode == "p010" ? 0x36 /*AIMAGE_FORMAT_YCBCR_P010*/ : AIMAGE_FORMAT_PRIVATE;
        uint64_t usage = mode == "p010" ? AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN : AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
        media_status_t st = AImageReader_newWithUsage(W, H, fmt, usage, 6, &reader);
        if (st != AMEDIA_OK) {
            printf("AImageReader_newWithUsage failed %d\n", st);
            return 1;
        }
        AImageReader_getWindow(reader, &window);
    }

    AMediaCodec* codec = AMediaCodec_createDecoderByType("video/hevc");
    char* name = nullptr;
    AMediaCodec_getName(codec, &name);
    AMediaFormat* fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "video/hevc");
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, W);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, H);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_MAX_WIDTH, W);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_MAX_HEIGHT, H);
    if (!getenv("NO_LOWLAT")) AMediaFormat_setInt32(fmt, "low-latency", 1);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_PRIORITY, 0);
    AMediaFormat_setFloat(fmt, AMEDIAFORMAT_KEY_OPERATING_RATE, 60.0f);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 24 * 1024 * 1024);
    if (!csd.empty()) AMediaFormat_setBuffer(fmt, "csd-0", csd.data(), csd.size());
    media_status_t cs = AMediaCodec_configure(codec, fmt, window, nullptr, 0);
    AMediaFormat_delete(fmt);
    if (cs != AMEDIA_OK || AMediaCodec_start(codec) != AMEDIA_OK) {
        printf("configure/start failed %d (%s %dx%d)\n", cs, name ? name : "?", W, H);
        return 1;
    }
    printf("decoder %s configured %dx%d mode %s%s\n", name ? name : "?", W, H, mode.c_str(),
           getenv("NO_LOWLAT") ? " (low-latency off)" : "");

    size_t fed = 0;
    int outCount = 0, errors = 0, readerFrames = 0;
    int32_t stride = W, sliceH = H, colorFmt = 0;
    int64_t t0 = nowNs();
    std::vector<int64_t> queuedAt(aus.size() + 1, 0);
    double latSum = 0, latMax = 0;
    bool eosSent = false, eosSeen = false;
    int64_t lastProgress = nowNs();
    auto writePgm = [&](const uint8_t* y, int32_t ys, bool tenBit, int w, int h) {
        FILE* o = fopen(snapPath.c_str(), "wb");
        if (!o) return;
        fprintf(o, "P5\n%d %d\n255\n", w / 4, h / 4);
        for (int yy = 0; yy < h / 4; yy++)
            for (int xx = 0; xx < w / 4; xx++) {
                uint8_t v;
                if (tenBit) v = (uint8_t)(((const uint16_t*)(y + (size_t)(yy * 4) * ys))[xx * 4] >> 8);
                else v = y[(size_t)(yy * 4) * ys + xx * 4];
                fputc(v, o);
            }
        fclose(o);
        printf("snapshot of frame %d written to %s\n", snapFrame, snapPath.c_str());
    };
    while (!eosSeen && nowNs() - lastProgress < 5000000000LL) {
        bool feedNow = !pace || (double)(nowNs() - t0) / 1e9 >= fed * 1001.0 / 30000.0;
        if (!eosSent && feedNow) {
            ssize_t in = AMediaCodec_dequeueInputBuffer(codec, 1000);
            if (in >= 0) {
                if (fed < aus.size()) {
                    size_t cap;
                    uint8_t* b = AMediaCodec_getInputBuffer(codec, (size_t)in, &cap);
                    auto& au = aus[fed];
                    if (au.size() > cap) {
                        printf("AU %zu: %zu bytes > input capacity %zu\n", fed, au.size(), cap);
                        return 1;
                    }
                    memcpy(b, au.data(), au.size());
                    queuedAt[fed] = nowNs();
                    AMediaCodec_queueInputBuffer(codec, (size_t)in, 0, au.size(), (uint64_t)fed, 0);
                    fed++;
                } else {
                    AMediaCodec_queueInputBuffer(codec, (size_t)in, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    eosSent = true;
                }
            }
        }
        AMediaCodecBufferInfo info;
        ssize_t idx = AMediaCodec_dequeueOutputBuffer(codec, &info, 1000);
        if (idx >= 0) {
            lastProgress = nowNs();
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) eosSeen = true;
            if (info.size > 0 || window) {
                size_t fi = (size_t)info.presentationTimeUs;
                if (fi < aus.size() && queuedAt[fi]) {
                    double l = (nowNs() - queuedAt[fi]) / 1e6;
                    latSum += l;
                    if (l > latMax) latMax = l;
                }
                if (mode == "buffer" && outCount == snapFrame && info.size > 0) {
                    size_t sz;
                    uint8_t* ob = AMediaCodec_getOutputBuffer(codec, (size_t)idx, &sz);
                    bool tenBit = colorFmt == 54 /*YUVP010*/ || stride >= W * 2;
                    if (ob) writePgm(ob + info.offset, stride, tenBit, W, H);
                }
                if (!eosSeen || info.size > 0) outCount++;
            }
            AMediaCodec_releaseOutputBuffer(codec, (size_t)idx, window != nullptr && info.size > 0);
        } else if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* of = AMediaCodec_getOutputFormat(codec);
            AMediaFormat_getInt32(of, "stride", &stride);
            AMediaFormat_getInt32(of, "slice-height", &sliceH);
            AMediaFormat_getInt32(of, "color-format", &colorFmt);
            printf("output format: %s\n", AMediaFormat_toString(of));
            AMediaFormat_delete(of);
        } else if (idx != AMEDIACODEC_INFO_TRY_AGAIN_LATER && idx != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            printf("dequeueOutputBuffer error %zd\n", idx);
            if (++errors > 5) break;
        }
        if (reader) {
            AImage* img = nullptr;
            while (AImageReader_acquireNextImage(reader, &img) == AMEDIA_OK && img) {
                if (mode == "gpu") {
                    AHardwareBuffer* ahb = nullptr;
                    AImage_getHardwareBuffer(img, &ahb);
                    if (ahb) {
                        AHardwareBuffer_Desc d;
                        AHardwareBuffer_describe(ahb, &d);
                        if (d.format != lastDesc.format || d.stride != lastDesc.stride || d.width != lastDesc.width) {
                            printf("[frame %d] buffer %ux%u stride %u format 0x%x usage 0x%llx\n", readerFrames, d.width, d.height,
                                   d.stride, d.format, (unsigned long long)d.usage);
                            lastDesc = d;
                        }
                        uint64_t id = 0;
                        AHardwareBuffer_getId(ahb, &id);
                        std::string sp = snapPath;
                        double bf = gpu.measure(ahb, id, readerFrames == snapFrame ? sp.c_str() : nullptr);
                        marks += bf < 0 ? '?' : bf > 0.5 ? 'X' : bf > 0.1 ? 'x' : '.';
                    }
                    held.push_back(img);
                    readerFrames++;
                    while ((int)held.size() > holdN) {
                        glFinish();
                        AImage_delete(held.front());
                        held.pop_front();
                    }
                    img = nullptr;
                    continue;
                }
                if (mode == "p010" && readerFrames == snapFrame) {
                    uint8_t* y;
                    int len;
                    int32_t ys;
                    if (AImage_getPlaneData(img, 0, &y, &len) == AMEDIA_OK && AImage_getPlaneRowStride(img, 0, &ys) == AMEDIA_OK)
                        writePgm(y, ys, true, W, H);
                    else printf("p010 plane read failed\n");
                }
                readerFrames++;
                AImage_delete(img);
            }
        }
    }
    if (mode == "gpu") {
        size_t bad = 0;
        for (char c : marks) bad += c == 'X' || c == 'x';
        printf("gpu readback: %zu frames, %zu mostly-black, %zu distinct buffers imported, %d stale-pointer hits (cache %s)\n",
               marks.size(), bad, gpu.cache.size(), gpu.reuse, gpu.useCache ? "on" : "off");
        for (size_t i = 0; i < marks.size(); i += 100) printf("  %5zu %s\n", i, marks.substr(i, 100).c_str());
        glFinish();
        for (auto* h : held) AImage_delete(h);
        held.clear();
    }
    double secs = (nowNs() - t0) / 1e9;
    printf("fed %zu/%zu AUs, decoder output %d frames%s, reader got %d, %.2f s (%.1f fps), queue->output avg %.1f max %.1f ms, errors %d\n",
           fed, aus.size(), outCount, eosSeen ? " (EOS)" : " (STALLED)", readerFrames, secs, outCount / secs,
           outCount ? latSum / outCount : 0.0, latMax, errors);
    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    if (reader) AImageReader_delete(reader);
    return 0;
}
