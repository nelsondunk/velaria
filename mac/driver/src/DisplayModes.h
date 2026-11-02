#pragma once
#include "Common.h"
#include <vector>

namespace vrl {

struct ModeInfo {
    BMDDisplayMode mode;
    const char* name;
    int width;
    int height;
    BMDTimeValue frameDuration;
    BMDTimeScale timeScale;
};

const std::vector<ModeInfo>& supportedModes();
const ModeInfo* findMode(BMDDisplayMode mode);
bool isSupportedPixelFormat(BMDPixelFormat pf);

// IDeckLinkDisplayMode implementation
class DisplayMode final : public IDeckLinkDisplayMode, public RefCounted {
public:
    explicit DisplayMode(const ModeInfo& m) : m_info(m) {}
    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override;
    VRL_REFCOUNT_METHODS
    HRESULT GetName(CFStringRef* name) override;
    BMDDisplayMode GetDisplayMode() override { return m_info.mode; }
    long GetWidth() override { return m_info.width; }
    long GetHeight() override { return m_info.height; }
    HRESULT GetFrameRate(BMDTimeValue* frameDuration, BMDTimeScale* timeScale) override;
    BMDFieldDominance GetFieldDominance() override { return bmdProgressiveFrame; }
    BMDDisplayModeFlags GetFlags() override;

private:
    ModeInfo m_info;
};

class DisplayModeIterator final : public IDeckLinkDisplayModeIterator, public RefCounted {
public:
    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override;
    VRL_REFCOUNT_METHODS
    HRESULT Next(IDeckLinkDisplayMode** out) override;

private:
    size_t m_index = 0;
};

}  // namespace vrl
