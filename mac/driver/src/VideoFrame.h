#pragma once
#include "Common.h"

namespace vrl {

class Timecode final : public IDeckLinkTimecode, public RefCounted {
public:
    Timecode(uint8_t h, uint8_t m, uint8_t s, uint8_t f, BMDTimecodeFlags flags)
        : m_h(h), m_m(m), m_s(s), m_f(f), m_flags(flags) {}
    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override;
    VRL_REFCOUNT_METHODS
    BMDTimecodeBCD GetBCD() override;
    HRESULT GetComponents(uint8_t* h, uint8_t* m, uint8_t* s, uint8_t* f) override;
    HRESULT GetString(CFStringRef* tc) override;
    BMDTimecodeFlags GetFlags() override { return m_flags; }
    HRESULT GetTimecodeUserBits(BMDTimecodeUserBits* ub) override {
        if (ub) *ub = m_userBits;
        return S_OK;
    }
    void setUserBits(BMDTimecodeUserBits ub) { m_userBits = ub; }

private:
    uint8_t m_h, m_m, m_s, m_f;
    BMDTimecodeFlags m_flags;
    BMDTimecodeUserBits m_userBits = 0;
};

// Frame created by IDeckLinkOutput::CreateVideoFrame. Owns its pixel buffer.
class MutableVideoFrame final : public IDeckLinkMutableVideoFrame, public RefCounted {
public:
    MutableVideoFrame(int32_t w, int32_t h, int32_t rowBytes, BMDPixelFormat pf, BMDFrameFlags flags,
                      IDeckLinkMemoryAllocator* allocator);
    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override;
    VRL_REFCOUNT_METHODS

    long GetWidth() override { return m_width; }
    long GetHeight() override { return m_height; }
    long GetRowBytes() override { return m_rowBytes; }
    BMDPixelFormat GetPixelFormat() override { return m_pixelFormat; }
    BMDFrameFlags GetFlags() override { return m_flags; }
    HRESULT GetBytes(void** buffer) override;
    HRESULT GetTimecode(BMDTimecodeFormat format, IDeckLinkTimecode** timecode) override;
    HRESULT GetAncillaryData(IDeckLinkVideoFrameAncillary** ancillary) override;

    HRESULT SetFlags(BMDFrameFlags newFlags) override {
        m_flags = newFlags;
        return S_OK;
    }
    HRESULT SetTimecode(BMDTimecodeFormat format, IDeckLinkTimecode* timecode) override;
    HRESULT SetTimecodeFromComponents(BMDTimecodeFormat format, uint8_t h, uint8_t m, uint8_t s, uint8_t f,
                                      BMDTimecodeFlags flags) override;
    HRESULT SetAncillaryData(IDeckLinkVideoFrameAncillary* ancillary) override;
    HRESULT SetTimecodeUserBits(BMDTimecodeFormat format, BMDTimecodeUserBits userBits) override;

    bool valid() const { return m_buffer != nullptr; }

protected:
    ~MutableVideoFrame() override;

private:
    int32_t m_width, m_height, m_rowBytes;
    BMDPixelFormat m_pixelFormat;
    BMDFrameFlags m_flags;
    void* m_buffer = nullptr;
    IDeckLinkMemoryAllocator* m_allocator = nullptr;
    Timecode* m_timecode = nullptr;
};

}  // namespace vrl
