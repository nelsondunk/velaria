#include "Encoder.h"

#include <CoreMedia/CoreMedia.h>

#include <cmath>
#include <memory>

namespace vrl {

namespace {

OSType cvFormatFor(BMDPixelFormat pf) {
    switch (pf) {
        case bmdFormat10BitYUV: return kCVPixelFormatType_422YpCbCr10;
        case bmdFormat8BitYUV: return kCVPixelFormatType_422YpCbCr8;
        case bmdFormat8BitBGRA: return kCVPixelFormatType_32BGRA;
        case bmdFormat8BitARGB: return kCVPixelFormatType_32ARGB;
        default: return 0;
    }
}

long byteOffsetForX(BMDPixelFormat pf, int x) {
    switch (pf) {
        case bmdFormat10BitYUV: return (x / 6) * 16;
        case bmdFormat8BitYUV: return (long)x * 2;
        default: return (long)x * 4;
    }
}

CFStringRef primariesKey(uint32_t p) {
    return p == 9 ? kCVImageBufferColorPrimaries_ITU_R_2020 : kCVImageBufferColorPrimaries_ITU_R_709_2;
}
CFStringRef transferKey(uint32_t t) {
    if (t == 16) return kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ;
    if (t == 18) return kCVImageBufferTransferFunction_ITU_R_2100_HLG;
    return kCVImageBufferTransferFunction_ITU_R_709_2;
}
CFStringRef matrixKey(uint32_t m) {
    return m == 9 ? kCVImageBufferYCbCrMatrix_ITU_R_2020 : kCVImageBufferYCbCrMatrix_ITU_R_709_2;
}

void tagColour(CVBufferRef b, const ColourInfo& c) {
    CVBufferSetAttachment(b, kCVImageBufferColorPrimariesKey, primariesKey(c.primaries), kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(b, kCVImageBufferTransferFunctionKey, transferKey(c.transfer), kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(b, kCVImageBufferYCbCrMatrixKey, matrixKey(c.matrix), kCVAttachmentMode_ShouldPropagate);
}

CFNumberRef num(int64_t v) { return CFNumberCreate(nullptr, kCFNumberSInt64Type, &v); }
CFNumberRef numd(double v) { return CFNumberCreate(nullptr, kCFNumberDoubleType, &v); }

void setProp(VTSessionRef s, CFStringRef key, CFTypeRef value, const char* name) {
    OSStatus st = VTSessionSetProperty(s, key, value);
    if (st != noErr) VLOGD("encoder property %s not supported (%d)", name, (int)st);
}

struct FrameCtx {
    uint64_t seq;
    void* user;
    int64_t startNs;
};

}  // namespace

// ------------------------------------------------------------------------------------------------ converter

FrameConverter::~FrameConverter() {
    if (m_session) {
        VTPixelTransferSessionInvalidate(m_session);
        CFRelease(m_session);
    }
    if (m_pool) CVPixelBufferPoolRelease(m_pool);
}

bool FrameConverter::ensurePool(int w, int h) {
    if (m_pool && m_poolW == w && m_poolH == h) return true;
    if (m_pool) CVPixelBufferPoolRelease(m_pool);
    m_pool = nullptr;
    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFNumberRef fmt = num(kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange);
    CFNumberRef wn = num(w), hn = num(h);
    CFDictionaryRef ioProps = CFDictionaryCreate(nullptr, nullptr, nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCVPixelBufferPixelFormatTypeKey, fmt);
    CFDictionarySetValue(attrs, kCVPixelBufferWidthKey, wn);
    CFDictionarySetValue(attrs, kCVPixelBufferHeightKey, hn);
    CFDictionarySetValue(attrs, kCVPixelBufferIOSurfacePropertiesKey, ioProps);
    CVReturn r = CVPixelBufferPoolCreate(nullptr, nullptr, attrs, &m_pool);
    CFRelease(fmt);
    CFRelease(wn);
    CFRelease(hn);
    CFRelease(ioProps);
    CFRelease(attrs);
    if (r != kCVReturnSuccess) {
        VLOGE("CVPixelBufferPoolCreate %dx%d failed %d", w, h, r);
        m_pool = nullptr;
        return false;
    }
    m_poolW = w;
    m_poolH = h;
    return true;
}

CVPixelBufferRef FrameConverter::convert(const uint8_t* base, long rowBytes, BMDPixelFormat pf, const CropRect& crop,
                                         int outW, int outH, const ColourInfo& colour) {
    OSType fmt = cvFormatFor(pf);
    if (!fmt || !base || crop.empty()) return nullptr;
    if (!m_session) {
        if (VTPixelTransferSessionCreate(nullptr, &m_session) != noErr) {
            VLOGE("VTPixelTransferSessionCreate failed");
            m_session = nullptr;
            return nullptr;
        }
        VTSessionSetProperty(m_session, kVTPixelTransferPropertyKey_ScalingMode, kVTScalingMode_Normal);
        VTSessionSetProperty(m_session, kVTPixelTransferPropertyKey_RealTime, kCFBooleanTrue);
    }
    if (!ensurePool(outW, outH)) return nullptr;

    // Zero-copy crop: point the source buffer at the top-left of the active picture and keep the raster stride.
    const uint8_t* origin = base + (long)crop.y * rowBytes + byteOffsetForX(pf, crop.x);
    CVPixelBufferRef src = nullptr;
    CVReturn r = CVPixelBufferCreateWithBytes(nullptr, (size_t)crop.w, (size_t)crop.h, fmt, (void*)origin,
                                              (size_t)rowBytes, nullptr, nullptr, nullptr, &src);
    if (r != kCVReturnSuccess || !src) {
        VLOGE("CVPixelBufferCreateWithBytes failed %d", r);
        return nullptr;
    }
    tagColour(src, colour);
    CVPixelBufferRef dst = nullptr;
    r = CVPixelBufferPoolCreatePixelBuffer(nullptr, m_pool, &dst);
    if (r != kCVReturnSuccess || !dst) {
        CVPixelBufferRelease(src);
        VLOGE("pool exhausted (%d)", r);
        return nullptr;
    }
    tagColour(dst, colour);
    OSStatus st = VTPixelTransferSessionTransferImage(m_session, src, dst);
    CVPixelBufferRelease(src);
    if (st != noErr) {
        VLOGE("VTPixelTransferSessionTransferImage failed %d (src %s %dx%d)", (int)st, fourcc(pf).c_str(), crop.w, crop.h);
        CVPixelBufferRelease(dst);
        return nullptr;
    }
    return dst;
}

// ------------------------------------------------------------------------------------------------ encoder

bool HevcEncoder::open(int width, int height, double fps, int bitrateMbps, int keyInterval, const ColourInfo& colour,
                       Output out) {
    close();
    m_out = std::move(out);
    m_width = width;
    m_height = height;
    m_fps = fps > 0 ? fps : 30.0;
    m_frameIndex = 0;
    {
        std::lock_guard<std::mutex> lk(m_psMutex);
        m_paramSets.clear();
    }

    const void* specKeys[] = {kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder,
                              kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder};
    const void* specVals[] = {kCFBooleanTrue, kCFBooleanTrue};
    CFDictionaryRef spec = CFDictionaryCreate(nullptr, specKeys, specVals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    CFMutableDictionaryRef srcAttrs = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFNumberRef fmt = num(kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange);
    CFDictionarySetValue(srcAttrs, kCVPixelBufferPixelFormatTypeKey, fmt);
    CFRelease(fmt);

    OSStatus st = VTCompressionSessionCreate(nullptr, width, height, kCMVideoCodecType_HEVC, spec, srcAttrs, nullptr,
                                             &HevcEncoder::outputCallback, this, &m_session);
    CFRelease(spec);
    CFRelease(srcAttrs);
    if (st != noErr || !m_session) {
        VLOGE("VTCompressionSessionCreate HEVC %dx%d failed %d", width, height, (int)st);
        m_session = nullptr;
        return false;
    }
    int64_t bps = (int64_t)bitrateMbps * 1000000;
    setProp(m_session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue, "RealTime");
    setProp(m_session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse, "AllowFrameReordering");
    setProp(m_session, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_HEVC_Main10_AutoLevel, "ProfileLevel");
    // Emit each frame as soon as it is encoded (Resolve only schedules ~60-90 ms ahead of scan-out).
    CFNumberRef zero = num(0);
    setProp(m_session, kVTCompressionPropertyKey_MaxFrameDelayCount, zero, "MaxFrameDelayCount");
    CFRelease(zero);
    setProp(m_session, kVTCompressionPropertyKey_PrioritizeEncodingSpeedOverQuality, kCFBooleanTrue,
            "PrioritizeEncodingSpeedOverQuality");
    CFNumberRef v = num(bps);
    setProp(m_session, kVTCompressionPropertyKey_AverageBitRate, v, "AverageBitRate");
    CFRelease(v);
    // Cap bursts: 1.5x the average over any 1 s window (keeps keyframes from flooding Wi-Fi).
    CFNumberRef bytes = num(bps * 3 / 2 / 8);
    CFNumberRef secs = numd(1.0);
    const void* lim[] = {bytes, secs};
    CFArrayRef limits = CFArrayCreate(nullptr, lim, 2, &kCFTypeArrayCallBacks);
    setProp(m_session, kVTCompressionPropertyKey_DataRateLimits, limits, "DataRateLimits");
    CFRelease(limits);
    CFRelease(bytes);
    CFRelease(secs);
    v = num(keyInterval);
    setProp(m_session, kVTCompressionPropertyKey_MaxKeyFrameInterval, v, "MaxKeyFrameInterval");
    CFRelease(v);
    v = numd(fps);
    setProp(m_session, kVTCompressionPropertyKey_ExpectedFrameRate, v, "ExpectedFrameRate");
    CFRelease(v);
    setProp(m_session, kVTCompressionPropertyKey_ColorPrimaries, primariesKey(colour.primaries), "ColorPrimaries");
    setProp(m_session, kVTCompressionPropertyKey_TransferFunction, transferKey(colour.transfer), "TransferFunction");
    setProp(m_session, kVTCompressionPropertyKey_YCbCrMatrix, matrixKey(colour.matrix), "YCbCrMatrix");
    VTCompressionSessionPrepareToEncodeFrames(m_session);
    VLOGI("HEVC Main10 encoder open %dx%d @ %.3f fps, %d Mbit/s, GOP %d", width, height, fps, bitrateMbps, keyInterval);
    return true;
}

void HevcEncoder::close() {
    if (!m_session) return;
    VTCompressionSessionCompleteFrames(m_session, kCMTimeInvalid);
    VTCompressionSessionInvalidate(m_session);
    CFRelease(m_session);
    m_session = nullptr;
}

void HevcEncoder::flush() {
    if (m_session) VTCompressionSessionCompleteFrames(m_session, kCMTimeInvalid);
}

bool HevcEncoder::encode(CVPixelBufferRef pb, uint64_t seq, bool forceKeyframe, void* user) {
    if (!m_session || !pb) return false;
    auto* ctx = new FrameCtx{seq, user, nowNs()};
    CFDictionaryRef props = nullptr;
    if (forceKeyframe) {
        const void* k[] = {kVTEncodeFrameOptionKey_ForceKeyFrame};
        const void* vv[] = {kCFBooleanTrue};
        props = CFDictionaryCreate(nullptr, k, vv, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    }
    m_inFlight++;
    // Timestamps must advance one real frame duration per frame: VideoToolbox's rate control budgets
    // AverageBitRate x elapsed time. (These used to step 1 ms per frame, which made the encoder think it ran at
    // 1000 fps and spend ~1/33 of the intended bits on each 29.97p frame: macroblocking.)
    const int32_t kScale = 90000;
    CMTime pts = CMTimeMake((int64_t)std::llround((double)m_frameIndex * kScale / m_fps), kScale);
    CMTime dur = CMTimeMake((int64_t)std::llround(kScale / m_fps), kScale);
    m_frameIndex++;
    OSStatus st = VTCompressionSessionEncodeFrame(m_session, pb, pts, dur, props, ctx, nullptr);
    if (props) CFRelease(props);
    if (st != noErr) {
        m_inFlight--;
        delete ctx;
        VLOGE("VTCompressionSessionEncodeFrame failed %d", (int)st);
        return false;
    }
    return true;
}

std::vector<uint8_t> HevcEncoder::parameterSets() {
    std::lock_guard<std::mutex> lk(m_psMutex);
    return m_paramSets;
}

void HevcEncoder::outputCallback(void* refCon, void* frameRefCon, OSStatus status, VTEncodeInfoFlags,
                                 CMSampleBufferRef sb) {
    static_cast<HevcEncoder*>(refCon)->handleOutput(frameRefCon, status, sb);
}

void HevcEncoder::handleOutput(void* frameRefCon, OSStatus status, CMSampleBufferRef sb) {
    std::unique_ptr<FrameCtx> ctx(static_cast<FrameCtx*>(frameRefCon));
    m_inFlight--;
    EncodedPacket pkt;
    pkt.seq = ctx ? ctx->seq : 0;
    pkt.user = ctx ? ctx->user : nullptr;
    pkt.encodeNs = ctx ? nowNs() - ctx->startNs : 0;
    if (status != noErr || !sb || !CMSampleBufferDataIsReady(sb)) {
        if (status != noErr) VLOGW("encode error %d for seq %llu", (int)status, (unsigned long long)pkt.seq);
        if (m_out) m_out(std::move(pkt));   // empty packet: lets the caller release `user`
        return;
    }
    bool key = true;
    CFArrayRef att = CMSampleBufferGetSampleAttachmentsArray(sb, false);
    if (att && CFArrayGetCount(att) > 0) {
        CFDictionaryRef d = (CFDictionaryRef)CFArrayGetValueAtIndex(att, 0);
        CFBooleanRef notSync = nullptr;
        if (CFDictionaryGetValueIfPresent(d, kCMSampleAttachmentKey_NotSync, (const void**)&notSync) && notSync &&
            CFBooleanGetValue(notSync))
            key = false;
    }
    static const uint8_t kStart[4] = {0, 0, 0, 1};
    CMFormatDescriptionRef fd = CMSampleBufferGetFormatDescription(sb);
    int nalLenSize = 4;
    if (key && fd) {
        size_t count = 0;
        int hdr = 4;
        std::vector<uint8_t> ps;
        if (CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fd, 0, nullptr, nullptr, &count, &hdr) == noErr) {
            nalLenSize = hdr;
            for (size_t i = 0; i < count; i++) {
                const uint8_t* p = nullptr;
                size_t sz = 0;
                if (CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(fd, i, &p, &sz, nullptr, nullptr) == noErr) {
                    ps.insert(ps.end(), kStart, kStart + 4);
                    ps.insert(ps.end(), p, p + sz);
                }
            }
        }
        {
            std::lock_guard<std::mutex> lk(m_psMutex);
            m_paramSets = ps;
        }
        pkt.annexB = ps;
    }
    CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sb);
    size_t total = CMBlockBufferGetDataLength(bb);
    std::vector<uint8_t> data(total);
    if (CMBlockBufferCopyDataBytes(bb, 0, total, data.data()) != kCMBlockBufferNoErr) {
        if (m_out) m_out(std::move(pkt));
        return;
    }
    size_t off = 0;
    pkt.annexB.reserve(pkt.annexB.size() + total + 64);
    while (off + (size_t)nalLenSize <= total) {
        uint32_t len = 0;
        for (int i = 0; i < nalLenSize; i++) len = (len << 8) | data[off + i];
        off += nalLenSize;
        if (len == 0 || off + len > total) break;
        pkt.annexB.insert(pkt.annexB.end(), kStart, kStart + 4);
        pkt.annexB.insert(pkt.annexB.end(), data.begin() + (long)off, data.begin() + (long)(off + len));
        off += len;
    }
    pkt.keyframe = key;
    if (m_out) m_out(std::move(pkt));
}

}  // namespace vrl
