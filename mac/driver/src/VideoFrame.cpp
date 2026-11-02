#include "VideoFrame.h"

#include <cstdio>
#include <cstdlib>

namespace vrl {

static uint8_t bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

HRESULT Timecode::QueryInterface(REFIID iid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    if (isIUnknown(iid) || iidEq(iid, IID_IDeckLinkTimecode)) {
        *ppv = static_cast<IDeckLinkTimecode*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

BMDTimecodeBCD Timecode::GetBCD() {
    return ((uint32_t)bcd(m_h) << 24) | ((uint32_t)bcd(m_m) << 16) | ((uint32_t)bcd(m_s) << 8) | bcd(m_f);
}

HRESULT Timecode::GetComponents(uint8_t* h, uint8_t* m, uint8_t* s, uint8_t* f) {
    if (h) *h = m_h;
    if (m) *m = m_m;
    if (s) *s = m_s;
    if (f) *f = m_f;
    return S_OK;
}

HRESULT Timecode::GetString(CFStringRef* tc) {
    if (!tc) return E_POINTER;
    char buf[16];
    snprintf(buf, sizeof buf, "%02u:%02u:%02u%c%02u", m_h, m_m, m_s,
             (m_flags & bmdTimecodeIsDropFrame) ? ';' : ':', m_f);
    *tc = makeCFString(buf);
    return S_OK;
}

MutableVideoFrame::MutableVideoFrame(int32_t w, int32_t h, int32_t rowBytes, BMDPixelFormat pf, BMDFrameFlags flags,
                                     IDeckLinkMemoryAllocator* allocator)
    : m_width(w), m_height(h), m_rowBytes(rowBytes), m_pixelFormat(pf), m_flags(flags), m_allocator(allocator) {
    size_t size = (size_t)rowBytes * (size_t)h;
    if (m_allocator) {
        m_allocator->AddRef();
        if (m_allocator->AllocateBuffer((uint32_t)size, &m_buffer) != S_OK) m_buffer = nullptr;
    } else {
        if (posix_memalign(&m_buffer, 4096, size) != 0) m_buffer = nullptr;
    }
}

MutableVideoFrame::~MutableVideoFrame() {
    if (m_timecode) m_timecode->Release();
    if (m_buffer) {
        if (m_allocator)
            m_allocator->ReleaseBuffer(m_buffer);
        else
            free(m_buffer);
    }
    if (m_allocator) m_allocator->Release();
}

HRESULT MutableVideoFrame::QueryInterface(REFIID iid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    if (isIUnknown(iid) || iidEq(iid, IID_IDeckLinkVideoFrame) || iidEq(iid, IID_IDeckLinkMutableVideoFrame)) {
        *ppv = static_cast<IDeckLinkMutableVideoFrame*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

HRESULT MutableVideoFrame::GetBytes(void** buffer) {
    if (!buffer) return E_POINTER;
    *buffer = m_buffer;
    return m_buffer ? S_OK : E_FAIL;
}

HRESULT MutableVideoFrame::GetTimecode(BMDTimecodeFormat, IDeckLinkTimecode** timecode) {
    if (!timecode) return E_POINTER;
    if (!m_timecode) {
        *timecode = nullptr;
        return S_FALSE;
    }
    m_timecode->AddRef();
    *timecode = m_timecode;
    return S_OK;
}

HRESULT MutableVideoFrame::GetAncillaryData(IDeckLinkVideoFrameAncillary** ancillary) {
    if (ancillary) *ancillary = nullptr;
    return S_FALSE;
}

HRESULT MutableVideoFrame::SetTimecode(BMDTimecodeFormat, IDeckLinkTimecode* timecode) {
    if (m_timecode) {
        m_timecode->Release();
        m_timecode = nullptr;
    }
    if (timecode) {
        uint8_t h = 0, m = 0, s = 0, f = 0;
        timecode->GetComponents(&h, &m, &s, &f);
        m_timecode = new Timecode(h, m, s, f, timecode->GetFlags());
    }
    return S_OK;
}

HRESULT MutableVideoFrame::SetTimecodeFromComponents(BMDTimecodeFormat, uint8_t h, uint8_t m, uint8_t s, uint8_t f,
                                                     BMDTimecodeFlags flags) {
    if (m_timecode) m_timecode->Release();
    m_timecode = new Timecode(h, m, s, f, flags);
    return S_OK;
}

HRESULT MutableVideoFrame::SetAncillaryData(IDeckLinkVideoFrameAncillary*) { return S_OK; }

HRESULT MutableVideoFrame::SetTimecodeUserBits(BMDTimecodeFormat, BMDTimecodeUserBits userBits) {
    if (!m_timecode) return E_FAIL;
    m_timecode->setUserBits(userBits);
    return S_OK;
}

}  // namespace vrl
