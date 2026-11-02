#include "DisplayModes.h"

namespace vrl {

const std::vector<ModeInfo>& supportedModes() {
    // Rates offered for each raster. Resolve picks one in Project Settings > Video Monitoring.
    static const std::vector<ModeInfo> modes = {
        // HD
        {bmdModeHD1080p2398, "1080p23.98", 1920, 1080, 1001, 24000},
        {bmdModeHD1080p24, "1080p24", 1920, 1080, 1000, 24000},
        {bmdModeHD1080p25, "1080p25", 1920, 1080, 1000, 25000},
        {bmdModeHD1080p2997, "1080p29.97", 1920, 1080, 1001, 30000},
        {bmdModeHD1080p30, "1080p30", 1920, 1080, 1000, 30000},
        {bmdModeHD1080p50, "1080p50", 1920, 1080, 1000, 50000},
        {bmdModeHD1080p5994, "1080p59.94", 1920, 1080, 1001, 60000},
        {bmdModeHD1080p6000, "1080p60", 1920, 1080, 1000, 60000},
        // UHD
        {bmdMode4K2160p2398, "2160p23.98", 3840, 2160, 1001, 24000},
        {bmdMode4K2160p24, "2160p24", 3840, 2160, 1000, 24000},
        {bmdMode4K2160p25, "2160p25", 3840, 2160, 1000, 25000},
        {bmdMode4K2160p2997, "2160p29.97", 3840, 2160, 1001, 30000},
        {bmdMode4K2160p30, "2160p30", 3840, 2160, 1000, 30000},
        {bmdMode4K2160p48, "2160p48", 3840, 2160, 1000, 48000},
        {bmdMode4K2160p50, "2160p50", 3840, 2160, 1000, 50000},
        {bmdMode4K2160p5994, "2160p59.94", 3840, 2160, 1001, 60000},
        {bmdMode4K2160p60, "2160p60", 3840, 2160, 1000, 60000},
        // 4K DCI
        {bmdMode4kDCI2398, "4K DCI 23.98", 4096, 2160, 1001, 24000},
        {bmdMode4kDCI24, "4K DCI 24", 4096, 2160, 1000, 24000},
        {bmdMode4kDCI25, "4K DCI 25", 4096, 2160, 1000, 25000},
        {bmdMode4kDCI2997, "4K DCI 29.97", 4096, 2160, 1001, 30000},
        {bmdMode4kDCI30, "4K DCI 30", 4096, 2160, 1000, 30000},
        {bmdMode4kDCI48, "4K DCI 48", 4096, 2160, 1000, 48000},
        {bmdMode4kDCI50, "4K DCI 50", 4096, 2160, 1000, 50000},
        {bmdMode4kDCI5994, "4K DCI 59.94", 4096, 2160, 1001, 60000},
        {bmdMode4kDCI60, "4K DCI 60", 4096, 2160, 1000, 60000},
        // 8K UHD
        {bmdMode8K4320p2398, "4320p23.98", 7680, 4320, 1001, 24000},
        {bmdMode8K4320p24, "4320p24", 7680, 4320, 1000, 24000},
        {bmdMode8K4320p25, "4320p25", 7680, 4320, 1000, 25000},
        {bmdMode8K4320p2997, "4320p29.97", 7680, 4320, 1001, 30000},
        {bmdMode8K4320p30, "4320p30", 7680, 4320, 1000, 30000},
        {bmdMode8K4320p48, "4320p48", 7680, 4320, 1000, 48000},
        {bmdMode8K4320p50, "4320p50", 7680, 4320, 1000, 50000},
        {bmdMode8K4320p5994, "4320p59.94", 7680, 4320, 1001, 60000},
        {bmdMode8K4320p60, "4320p60", 7680, 4320, 1000, 60000},
        // 8K DCI
        {bmdMode8kDCI2398, "8K DCI 23.98", 8192, 4320, 1001, 24000},
        {bmdMode8kDCI24, "8K DCI 24", 8192, 4320, 1000, 24000},
        {bmdMode8kDCI25, "8K DCI 25", 8192, 4320, 1000, 25000},
        {bmdMode8kDCI2997, "8K DCI 29.97", 8192, 4320, 1001, 30000},
        {bmdMode8kDCI30, "8K DCI 30", 8192, 4320, 1000, 30000},
        {bmdMode8kDCI48, "8K DCI 48", 8192, 4320, 1000, 48000},
        {bmdMode8kDCI50, "8K DCI 50", 8192, 4320, 1000, 50000},
        {bmdMode8kDCI5994, "8K DCI 59.94", 8192, 4320, 1001, 60000},
        {bmdMode8kDCI60, "8K DCI 60", 8192, 4320, 1000, 60000},
    };
    return modes;
}

const ModeInfo* findMode(BMDDisplayMode mode) {
    for (const auto& m : supportedModes())
        if (m.mode == mode) return &m;
    return nullptr;
}

bool isSupportedPixelFormat(BMDPixelFormat pf) {
    return pf == bmdFormat10BitYUV || pf == bmdFormat8BitYUV || pf == bmdFormat8BitBGRA || pf == bmdFormat8BitARGB;
}

HRESULT DisplayMode::QueryInterface(REFIID iid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    if (isIUnknown(iid) || iidEq(iid, IID_IDeckLinkDisplayMode)) {
        *ppv = static_cast<IDeckLinkDisplayMode*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    VLOGD("DisplayMode QI unknown %s (%s)", iidString(iid).c_str(), iidName(iid));
    return E_NOINTERFACE;
}

HRESULT DisplayMode::GetName(CFStringRef* name) {
    if (!name) return E_POINTER;
    *name = makeCFString(m_info.name);
    return S_OK;
}

HRESULT DisplayMode::GetFrameRate(BMDTimeValue* frameDuration, BMDTimeScale* timeScale) {
    if (frameDuration) *frameDuration = m_info.frameDuration;
    if (timeScale) *timeScale = m_info.timeScale;
    return S_OK;
}

BMDDisplayModeFlags DisplayMode::GetFlags() {
    BMDDisplayModeFlags f = m_info.height > 1080 ? bmdDisplayModeColorspaceRec2020 | bmdDisplayModeColorspaceRec709
                                                 : bmdDisplayModeColorspaceRec709;
    if (m_info.height <= 2160) f |= bmdDisplayModeSupports3D;
    return f;
}

HRESULT DisplayModeIterator::QueryInterface(REFIID iid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    if (isIUnknown(iid) || iidEq(iid, IID_IDeckLinkDisplayModeIterator)) {
        *ppv = static_cast<IDeckLinkDisplayModeIterator*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

HRESULT DisplayModeIterator::Next(IDeckLinkDisplayMode** out) {
    if (!out) return E_POINTER;
    const auto& modes = supportedModes();
    if (m_index >= modes.size()) {
        *out = nullptr;
        return S_FALSE;
    }
    *out = new DisplayMode(modes[m_index++]);
    return S_OK;
}

}  // namespace vrl
