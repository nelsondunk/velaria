#pragma once
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

#include "ActiveArea.h"
#include "Common.h"

namespace vrl {

struct ColourInfo {
    uint32_t primaries = 1;   // ISO 23091-2: 1 = BT.709, 9 = BT.2020
    uint32_t transfer = 1;    // 1 = BT.709, 16 = PQ, 18 = HLG
    uint32_t matrix = 1;      // 1 = BT.709, 9 = BT.2020nc
    bool operator==(const ColourInfo& o) const {
        return primaries == o.primaries && transfer == o.transfer && matrix == o.matrix;
    }
};

// Crops a DeckLink frame (in place, no copy of the full raster) and converts/scales it to a 10-bit 4:2:0
// IOSurface buffer ready for the encoder.
class FrameConverter {
public:
    ~FrameConverter();
    // Returns a +1 CVPixelBuffer of size outW x outH, or nullptr.
    CVPixelBufferRef convert(const uint8_t* base, long rowBytes, BMDPixelFormat pf, const CropRect& crop, int outW,
                             int outH, const ColourInfo& colour);

private:
    bool ensurePool(int w, int h);
    VTPixelTransferSessionRef m_session = nullptr;
    CVPixelBufferPoolRef m_pool = nullptr;
    int m_poolW = 0, m_poolH = 0;
};

struct EncodedPacket {
    std::vector<uint8_t> annexB;   // access unit, Annex-B start codes; parameter sets prepended on keyframes
    bool keyframe = false;
    uint64_t seq = 0;
    int64_t encodeNs = 0;          // time spent in the encoder
    void* user = nullptr;
};

class HevcEncoder {
public:
    using Output = std::function<void(EncodedPacket&&)>;
    ~HevcEncoder() { close(); }
    bool open(int width, int height, double fps, int bitrateMbps, int keyframeIntervalFrames, const ColourInfo& colour,
              Output out);
    void close();
    bool isOpen() const { return m_session != nullptr; }
    // Takes its own reference on pb. `user` is handed back in the packet.
    bool encode(CVPixelBufferRef pb, uint64_t seq, bool forceKeyframe, void* user);
    void flush();
    std::vector<uint8_t> parameterSets();   // Annex-B VPS+SPS+PPS (empty until first frame encoded)
    int width() const { return m_width; }
    int height() const { return m_height; }
    int inFlight() const { return m_inFlight.load(); }

private:
    static void outputCallback(void* refCon, void* frameRefCon, OSStatus status, VTEncodeInfoFlags flags,
                               CMSampleBufferRef sb);
    void handleOutput(void* frameRefCon, OSStatus status, CMSampleBufferRef sb);

    VTCompressionSessionRef m_session = nullptr;
    Output m_out;
    int m_width = 0, m_height = 0;
    double m_fps = 30.0;
    uint64_t m_frameIndex = 0;   // presentation timestamps: frame index / fps
    std::mutex m_psMutex;
    std::vector<uint8_t> m_paramSets;
    std::atomic<int> m_inFlight{0};
};

}  // namespace vrl
