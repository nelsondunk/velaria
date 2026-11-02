#include "VirtualDevice.h"

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>

#include <algorithm>
#include <cmath>

#include "VideoFrame.h"

namespace vrl {

namespace {
constexpr int64_t kNsPerSec = 1000000000LL;
constexpr uint32_t kMaxAudioChannels = 16;
constexpr int64_t kAudioRate = 48000;
constexpr int64_t kMaxBufferedAudioSamples = kAudioRate * 4;   // 4 s, like a generous hardware FIFO
constexpr int64_t kAudioTickNs = 10 * 1000000LL;
constexpr int64_t kPersistentID = 0x564C4E4B;   // 'VLNK'

int64_t toNs(BMDTimeValue v, BMDTimeScale scale) {
    if (scale <= 0) return 0;
    return (int64_t)((__int128)v * kNsPerSec / scale);
}
BMDTimeValue fromNs(int64_t ns, BMDTimeScale scale) { return (BMDTimeValue)((__int128)ns * scale / kNsPerSec); }
}  // namespace

// ---------------------------------------------------------------------------------------------
// Tear-offs: lifetime is the device's (a process-wide singleton).

#define VRL_TEAROFF_UNKNOWN(Cls, IfaceIID, Iface, CompatIID)                        \
    HRESULT Cls::QueryInterface(REFIID iid, LPVOID* ppv) {                          \
        if (!ppv) return E_POINTER;                                                 \
        if (iidEq(iid, IfaceIID) || iidEq(iid, CompatIID)) {                        \
            *ppv = static_cast<Iface*>(this);                                       \
            AddRef();                                                               \
            return S_OK;                                                            \
        }                                                                           \
        return m_dev->QueryInterface(iid, ppv);                                     \
    }                                                                               \
    ULONG Cls::AddRef() { return m_dev->AddRef(); }                                 \
    ULONG Cls::Release() { return m_dev->Release(); }

VRL_TEAROFF_UNKNOWN(DeviceAttributes, IID_IDeckLinkProfileAttributes, IDeckLinkProfileAttributes,
                    IID_IDeckLinkAttributes_v10_11)
VRL_TEAROFF_UNKNOWN(DeviceStatus, IID_IDeckLinkStatus, IDeckLinkStatus, IID_IDeckLinkStatus)
VRL_TEAROFF_UNKNOWN(DeviceConfiguration, IID_IDeckLinkConfiguration, IDeckLinkConfiguration,
                    IID_IDeckLinkConfiguration_v10_11)

HRESULT DeviceAttributes::GetFlag(BMDDeckLinkAttributeID id, bool* v) {
    if (!v) return E_POINTER;
    switch (id) {
        case BMDDeckLinkSupportsIdleOutput:
        case BMDDeckLinkSupportsHDRMetadata:
        case BMDDeckLinkSupportsColorspaceMetadata:
        case BMDDeckLinkSupportsHighFrameRateTimecode:
            *v = true;
            break;
        case BMDDeckLinkSupportsInternalKeying:
        case BMDDeckLinkSupportsExternalKeying:
        case BMDDeckLinkSupportsInputFormatDetection:
        case BMDDeckLinkHasReferenceInput:
        case BMDDeckLinkHasSerialPort:
        case BMDDeckLinkHasAnalogVideoOutputGain:
        case BMDDeckLinkCanOnlyAdjustOverallVideoOutputGain:
        case BMDDeckLinkHasVideoInputAntiAliasingFilter:
        case BMDDeckLinkHasBypass:
        case BMDDeckLinkSupportsClockTimingAdjustment:
        case BMDDeckLinkSupportsFullFrameReferenceInputTimingOffset:
        case BMDDeckLinkSupportsSMPTELevelAOutput:
        case BMDDeckLinkSupportsAutoSwitchingPPsFOnInput:
        case BMDDeckLinkSupportsDualLinkSDI:
        case BMDDeckLinkSupportsQuadLinkSDI:
        case BMDDeckLinkVANCRequires10BitYUVVideoFrames:
        case BMDDeckLinkHasLTCTimecodeInput:
        case BMDDeckLinkSupportsHDMITimecode:
        case BMDDeckLinkSupportsSynchronizeToCaptureGroup:
        case BMDDeckLinkSupportsSynchronizeToPlaybackGroup:
            *v = false;
            break;
        default:
            *v = false;
            VLOGD("Attributes GetFlag unknown %s", fourcc(id).c_str());
            return E_INVALIDARG;
    }
    VLOGD("Attributes GetFlag %s -> %d", fourcc(id).c_str(), *v);
    return S_OK;
}

HRESULT DeviceAttributes::GetInt(BMDDeckLinkAttributeID id, int64_t* v) {
    if (!v) return E_POINTER;
    switch (id) {
        case BMDDeckLinkMaximumAudioChannels: *v = kMaxAudioChannels; break;
        case BMDDeckLinkMaximumAnalogAudioInputChannels: *v = 0; break;
        case BMDDeckLinkMaximumAnalogAudioOutputChannels: *v = 0; break;
        case BMDDeckLinkNumberOfSubDevices: *v = 1; break;
        case BMDDeckLinkSubDeviceIndex: *v = 0; break;
        case BMDDeckLinkPersistentID: *v = kPersistentID; break;
        case BMDDeckLinkDeviceGroupID: *v = kPersistentID; break;
        case BMDDeckLinkTopologicalID: *v = kPersistentID; break;
        case BMDDeckLinkVideoOutputConnections: *v = bmdVideoConnectionSDI | bmdVideoConnectionHDMI; break;
        case BMDDeckLinkVideoInputConnections: *v = 0; break;
        case BMDDeckLinkAudioOutputConnections: *v = bmdAudioConnectionEmbedded; break;
        case BMDDeckLinkAudioInputConnections: *v = 0; break;
        case BMDDeckLinkVideoIOSupport: *v = bmdDeviceSupportsPlayback; break;
        case BMDDeckLinkDeckControlConnections: *v = 0; break;
        case BMDDeckLinkDeviceInterface: *v = bmdDeviceInterfacePCI; break;
        case BMDDeckLinkAudioInputRCAChannelCount:
        case BMDDeckLinkAudioInputXLRChannelCount:
        case BMDDeckLinkAudioOutputRCAChannelCount:
        case BMDDeckLinkAudioOutputXLRChannelCount: *v = 0; break;
        case BMDDeckLinkProfileID: *v = bmdProfileOneSubDeviceHalfDuplex; break;
        case BMDDeckLinkDuplex: *v = bmdDuplexHalf; break;
        case BMDDeckLinkMinimumPrerollFrames: *v = 3; break;
        case BMDDeckLinkSupportedDynamicRange:
            *v = bmdDynamicRangeSDR | bmdDynamicRangeHDRStaticPQ | bmdDynamicRangeHDRStaticHLG;
            break;
        default:
            *v = 0;
            VLOGD("Attributes GetInt unknown %s", fourcc(id).c_str());
            return E_INVALIDARG;
    }
    VLOGD("Attributes GetInt %s -> %lld", fourcc(id).c_str(), (long long)*v);
    return S_OK;
}

HRESULT DeviceAttributes::GetFloat(BMDDeckLinkAttributeID id, double* v) {
    if (!v) return E_POINTER;
    *v = 0;
    VLOGD("Attributes GetFloat %s", fourcc(id).c_str());
    return E_INVALIDARG;
}

HRESULT DeviceAttributes::GetString(BMDDeckLinkAttributeID id, CFStringRef* v) {
    if (!v) return E_POINTER;
    const char* s = nullptr;
    switch (id) {
        case BMDDeckLinkVendorName: s = "Blackmagic Design"; break;
        case BMDDeckLinkDisplayName: s = "Velaria (Quest)"; break;
        case BMDDeckLinkModelName: s = "Velaria Virtual Output"; break;
        case BMDDeckLinkDeviceHandle: s = "VRLink:00000001"; break;
        case BMDDeckLinkSerialPortDeviceName: break;
        case BMDDeckLinkEthernetMACAddress: break;
        default: break;
    }
    VLOGD("Attributes GetString %s -> %s", fourcc(id).c_str(), s ? s : "(none)");
    if (!s) {
        *v = nullptr;
        return E_INVALIDARG;
    }
    *v = makeCFString(s);
    return S_OK;
}

HRESULT DeviceStatus::GetFlag(BMDDeckLinkStatusID id, bool* v) {
    if (!v) return E_POINTER;
    *v = false;
    VLOGD("Status GetFlag %s", fourcc(id).c_str());
    if (id == bmdDeckLinkStatusVideoInputSignalLocked || id == bmdDeckLinkStatusReferenceSignalLocked) return S_OK;
    return E_INVALIDARG;
}

HRESULT DeviceStatus::GetInt(BMDDeckLinkStatusID id, int64_t* v) {
    if (!v) return E_POINTER;
    *v = 0;
    HRESULT hr = S_OK;
    switch (id) {
        case bmdDeckLinkStatusCurrentVideoOutputMode: {
            const ModeInfo* m = m_dev->currentMode();
            *v = m ? m->mode : bmdModeUnknown;
            break;
        }
        case bmdDeckLinkStatusCurrentVideoOutputFlags: *v = 0; break;
        case bmdDeckLinkStatusBusy: *v = m_dev->isRunning() ? 2 /* playback busy */ : 0; break;
        case bmdDeckLinkStatusPCIExpressLinkWidth: *v = 4; break;
        case bmdDeckLinkStatusPCIExpressLinkSpeed: *v = 3; break;
        case bmdDeckLinkStatusLastVideoOutputPixelFormat: *v = bmdFormat10BitYUV; break;
        case bmdDeckLinkStatusReferenceSignalMode: *v = bmdModeUnknown; break;
        case bmdDeckLinkStatusReferenceSignalFlags: *v = 0; break;
        case bmdDeckLinkStatusDeviceTemperature: *v = 40; break;
        default: hr = E_INVALIDARG; break;
    }
    VLOGD("Status GetInt %s -> %lld (hr=%d)", fourcc(id).c_str(), (long long)*v, (int)hr);
    return hr;
}

HRESULT DeviceStatus::GetFloat(BMDDeckLinkStatusID id, double* v) {
    if (v) *v = 0;
    VLOGD("Status GetFloat %s", fourcc(id).c_str());
    return E_INVALIDARG;
}

HRESULT DeviceStatus::GetString(BMDDeckLinkStatusID id, CFStringRef* v) {
    if (v) *v = nullptr;
    VLOGD("Status GetString %s", fourcc(id).c_str());
    return E_INVALIDARG;
}

HRESULT DeviceStatus::GetBytes(BMDDeckLinkStatusID id, void*, uint32_t* size) {
    if (size) *size = 0;
    VLOGD("Status GetBytes %s", fourcc(id).c_str());
    return E_INVALIDARG;
}

HRESULT DeviceConfiguration::SetFlag(BMDDeckLinkConfigurationID id, bool value) {
    VirtualDevice::ConfigValue v;
    v.kind = 1;
    v.i = value;
    m_dev->configSet(id, v);
    VLOGD("Config SetFlag %s = %d", fourcc(id).c_str(), value);
    return S_OK;
}
HRESULT DeviceConfiguration::GetFlag(BMDDeckLinkConfigurationID id, bool* value) {
    if (!value) return E_POINTER;
    VirtualDevice::ConfigValue v;
    bool have = m_dev->configGet(id, v);
    *value = have ? v.i != 0 : false;
    VLOGD("Config GetFlag %s -> %d", fourcc(id).c_str(), *value);
    return S_OK;
}
HRESULT DeviceConfiguration::SetInt(BMDDeckLinkConfigurationID id, int64_t value) {
    VirtualDevice::ConfigValue v;
    v.kind = 2;
    v.i = value;
    m_dev->configSet(id, v);
    VLOGD("Config SetInt %s = %lld (%s)", fourcc(id).c_str(), (long long)value, fourcc((uint32_t)value).c_str());
    return S_OK;
}
HRESULT DeviceConfiguration::GetInt(BMDDeckLinkConfigurationID id, int64_t* value) {
    if (!value) return E_POINTER;
    VirtualDevice::ConfigValue v;
    if (m_dev->configGet(id, v)) {
        *value = v.i;
    } else {
        switch (id) {
            case bmdDeckLinkConfigVideoOutputConnection: *value = bmdVideoConnectionSDI; break;
            case bmdDeckLinkConfigVideoOutputConversionMode: *value = bmdNoVideoOutputConversion; break;
            case bmdDeckLinkConfigSDIOutputLinkConfiguration: *value = bmdLinkConfigurationSingleLink; break;
            case bmdDeckLinkConfigVideoOutputIdleOperation: *value = bmdIdleVideoOutputLastFrame; break;
            default: *value = 0; break;
        }
    }
    VLOGD("Config GetInt %s -> %lld", fourcc(id).c_str(), (long long)*value);
    return S_OK;
}
HRESULT DeviceConfiguration::SetFloat(BMDDeckLinkConfigurationID id, double value) {
    VirtualDevice::ConfigValue v;
    v.kind = 3;
    v.f = value;
    m_dev->configSet(id, v);
    VLOGD("Config SetFloat %s = %f", fourcc(id).c_str(), value);
    return S_OK;
}
HRESULT DeviceConfiguration::GetFloat(BMDDeckLinkConfigurationID id, double* value) {
    if (!value) return E_POINTER;
    VirtualDevice::ConfigValue v;
    *value = m_dev->configGet(id, v) ? v.f : 0.0;
    VLOGD("Config GetFloat %s -> %f", fourcc(id).c_str(), *value);
    return S_OK;
}
HRESULT DeviceConfiguration::SetString(BMDDeckLinkConfigurationID id, CFStringRef value) {
    VirtualDevice::ConfigValue v;
    v.kind = 4;
    char buf[512] = {0};
    if (value) CFStringGetCString(value, buf, sizeof buf, kCFStringEncodingUTF8);
    v.s = buf;
    m_dev->configSet(id, v);
    VLOGD("Config SetString %s = %s", fourcc(id).c_str(), buf);
    return S_OK;
}
HRESULT DeviceConfiguration::GetString(BMDDeckLinkConfigurationID id, CFStringRef* value) {
    if (!value) return E_POINTER;
    VirtualDevice::ConfigValue v;
    *value = makeCFString(m_dev->configGet(id, v) ? v.s : std::string());
    VLOGD("Config GetString %s", fourcc(id).c_str());
    return S_OK;
}
HRESULT DeviceConfiguration::WriteConfigurationToPreferences() {
    VLOGD("Config WriteConfigurationToPreferences");
    return S_OK;
}

// ---------------------------------------------------------------------------------------------

VirtualDevice* VirtualDevice::shared() {
    static VirtualDevice* dev = new VirtualDevice();
    return dev;
}

VirtualDevice::VirtualDevice() {
    m_sink = createDefaultSink();
    m_clockThread = std::thread([this] { clockThreadMain(); });
    m_clockThread.detach();
    VLOGI("VirtualDevice created");
}

void VirtualDevice::setSink(FrameSink* sink) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_sink = sink;
}

const ModeInfo* VirtualDevice::currentMode() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_mode;
}

bool VirtualDevice::isRunning() {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_running;
}

bool VirtualDevice::configGet(uint32_t id, ConfigValue& out) {
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_config.find(id);
    if (it == m_config.end()) return false;
    out = it->second;
    return true;
}

void VirtualDevice::configSet(uint32_t id, const ConfigValue& v) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_config[id] = v;
}

HRESULT VirtualDevice::QueryInterface(REFIID iid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (isIUnknown(iid) || iidEq(iid, IID_IDeckLink)) {
        *ppv = static_cast<IDeckLink*>(this);
    } else if (iidEq(iid, IID_IDeckLinkOutput)) {
        *ppv = static_cast<IDeckLinkOutput*>(this);
    } else if (iidEq(iid, IID_IDeckLinkOutput_v10_11)) {
        *ppv = static_cast<IDeckLinkOutput_v10_11*>(&m_outputV10_11);
    } else if (iidEq(iid, IID_IDeckLinkProfileAttributes) || iidEq(iid, IID_IDeckLinkAttributes_v10_11)) {
        // vtable-identical; one object serves both generations
        *ppv = static_cast<IDeckLinkProfileAttributes*>(&m_attributes);
    } else if (iidEq(iid, IID_IDeckLinkStatus)) {
        *ppv = static_cast<IDeckLinkStatus*>(&m_status);
    } else if (iidEq(iid, IID_IDeckLinkConfiguration) || iidEq(iid, IID_IDeckLinkConfiguration_v10_11)) {
        *ppv = static_cast<IDeckLinkConfiguration*>(&m_configuration);
    } else if (iidEq(iid, IID_IDeckLinkNotification) || iidEq(iid, IID_IDeckLinkNotification_v10_11)) {
        *ppv = static_cast<IDeckLinkNotification*>(this);
    }
    if (*ppv) {
        AddRef();
        VLOGD("Device QI %s -> ok", iidName(iid));
        return S_OK;
    }
    VLOGI("Device QI %s (%s) -> E_NOINTERFACE", iidString(iid).c_str(), iidName(iid));
    return E_NOINTERFACE;
}

HRESULT VirtualDevice::GetModelName(CFStringRef* modelName) {
    if (!modelName) return E_POINTER;
    *modelName = makeCFString("Velaria Virtual Output");
    VLOGD("GetModelName");
    return S_OK;
}

HRESULT VirtualDevice::GetDisplayName(CFStringRef* displayName) {
    if (!displayName) return E_POINTER;
    *displayName = makeCFString("Velaria (Quest)");
    VLOGD("GetDisplayName");
    return S_OK;
}

HRESULT VirtualDevice::Subscribe(BMDNotifications topic, IDeckLinkNotificationCallback* cb) {
    if (!cb) return E_INVALIDARG;
    std::lock_guard<std::mutex> lk(m_mutex);
    cb->AddRef();
    m_notify.push_back({topic, cb});
    VLOGD("Notification Subscribe %s", fourcc(topic).c_str());
    return S_OK;
}

HRESULT VirtualDevice::Unsubscribe(BMDNotifications topic, IDeckLinkNotificationCallback* cb) {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto it = m_notify.begin(); it != m_notify.end(); ++it) {
        if (it->first == topic && it->second == cb) {
            it->second->Release();
            m_notify.erase(it);
            break;
        }
    }
    VLOGD("Notification Unsubscribe %s", fourcc(topic).c_str());
    return S_OK;
}

// ---------------------------------------------------------------------------------------------
// IDeckLinkOutput

HRESULT VirtualDevice::DoesSupportVideoMode(BMDVideoConnection connection, BMDDisplayMode requestedMode,
                                            BMDPixelFormat pf, BMDVideoOutputConversionMode conversionMode,
                                            BMDSupportedVideoModeFlags flags, BMDDisplayMode* actualMode,
                                            bool* supported) {
    const ModeInfo* m = findMode(requestedMode);
    bool ok = m != nullptr && (pf == bmdFormatUnspecified || isSupportedPixelFormat(pf));
    if (supported) *supported = ok;
    if (actualMode) *actualMode = ok ? requestedMode : bmdModeUnknown;
    VLOGD("DoesSupportVideoMode conn=%u mode=%s pf=%s conv=%s flags=0x%x -> %d", connection,
          fourcc(requestedMode).c_str(), fourcc(pf).c_str(), fourcc(conversionMode).c_str(), flags, ok);
    return S_OK;
}

HRESULT VirtualDevice::GetDisplayMode(BMDDisplayMode displayMode, IDeckLinkDisplayMode** result) {
    if (!result) return E_POINTER;
    const ModeInfo* m = findMode(displayMode);
    VLOGD("GetDisplayMode %s -> %s", fourcc(displayMode).c_str(), m ? m->name : "unsupported");
    if (!m) {
        *result = nullptr;
        return E_INVALIDARG;
    }
    *result = new DisplayMode(*m);
    return S_OK;
}

HRESULT VirtualDevice::GetDisplayModeIterator(IDeckLinkDisplayModeIterator** iterator) {
    if (!iterator) return E_POINTER;
    *iterator = new DisplayModeIterator();
    VLOGD("GetDisplayModeIterator");
    return S_OK;
}

HRESULT VirtualDevice::SetScreenPreviewCallback(IDeckLinkScreenPreviewCallback* cb) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (cb) cb->AddRef();
    if (m_previewCb) m_previewCb->Release();
    m_previewCb = cb;
    VLOGD("SetScreenPreviewCallback %p", (void*)cb);
    return S_OK;
}

HRESULT VirtualDevice::EnableVideoOutput(BMDDisplayMode displayMode, BMDVideoOutputFlags flags) {
    const ModeInfo* m = findMode(displayMode);
    VLOGI("EnableVideoOutput %s flags=0x%x -> %s", fourcc(displayMode).c_str(), flags, m ? m->name : "UNSUPPORTED");
    if (!m) return E_INVALIDARG;
    FrameSink* sink;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_mode = m;
        m_outputFlags = flags;
        m_videoEnabled = true;
        sink = m_sink;
    }
    if (sink) sink->onOutputEnabled(m);
    for (auto& n : m_notify)
        if (n.first == bmdStatusChanged) n.second->Notify(bmdStatusChanged, bmdDeckLinkStatusCurrentVideoOutputMode, 0);
    return S_OK;
}

HRESULT VirtualDevice::DisableVideoOutput() {
    VLOGI("DisableVideoOutput");
    std::vector<IDeckLinkVideoFrame*> toRelease;
    FrameSink* sink;
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        // Let the clock thread deliver any pending flush/stop notifications first.
        m_cv.wait_for(lk, std::chrono::milliseconds(500), [this] { return !m_flushAndNotifyStop && !m_stopDelivering; });
        m_running = false;
        m_videoEnabled = false;
        for (auto& p : m_queue) toRelease.push_back(p.frame);
        m_queue.clear();
        m_completionTimes.clear();
        sink = m_sink;
    }
    for (auto* f : toRelease) f->Release();
    if (sink) sink->onOutputDisabled();
    return S_OK;
}

HRESULT VirtualDevice::SetVideoOutputFrameMemoryAllocator(IDeckLinkMemoryAllocator* a) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (a) a->AddRef();
    if (m_allocator) m_allocator->Release();
    m_allocator = a;
    VLOGD("SetVideoOutputFrameMemoryAllocator %p", (void*)a);
    return S_OK;
}

HRESULT VirtualDevice::CreateVideoFrame(int32_t width, int32_t height, int32_t rowBytes, BMDPixelFormat pf,
                                        BMDFrameFlags flags, IDeckLinkMutableVideoFrame** outFrame) {
    if (!outFrame) return E_POINTER;
    IDeckLinkMemoryAllocator* alloc;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        alloc = m_allocator;
    }
    auto* f = new MutableVideoFrame(width, height, rowBytes, pf, flags, alloc);
    if (!f->valid()) {
        f->Release();
        *outFrame = nullptr;
        return E_OUTOFMEMORY;
    }
    *outFrame = f;
    VLOGD("CreateVideoFrame %dx%d rb=%d pf=%s", width, height, rowBytes, fourcc(pf).c_str());
    return S_OK;
}

HRESULT VirtualDevice::CreateAncillaryData(BMDPixelFormat, IDeckLinkVideoFrameAncillary** outBuffer) {
    if (outBuffer) *outBuffer = nullptr;
    return E_NOTIMPL;
}

HRESULT VirtualDevice::DisplayVideoFrameSync(IDeckLinkVideoFrame* frame) {
    if (!frame) return E_INVALIDARG;
    FrameSink* sink;
    ScheduledFrame sf{};
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_videoEnabled) return E_ACCESSDENIED;
        sink = m_sink;
        sf.frame = frame;
        sf.seq = ++m_seq;
        sf.mode = m_mode;
        sf.streamTimeNs = -1;
        sf.durationNs = m_mode ? toNs(m_mode->frameDuration, m_mode->timeScale) : 0;
        sf.sync = true;
    }
    if (sink) {
        sink->onFrame(sf);
        sink->onFrameDisplayed(sf.seq, nowNs(), false);
    }
    // Real cards hand every displayed frame to the app's screen-preview callback; Resolve drives its viewer
    // and playhead from these DrawFrame calls while a DeckLink output is active.
    IDeckLinkScreenPreviewCallback* preview;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        preview = m_previewCb;
        if (preview) preview->AddRef();
    }
    if (preview) {
        preview->DrawFrame(frame);
        preview->Release();
    }
    return S_OK;
}

HRESULT VirtualDevice::ScheduleVideoFrame(IDeckLinkVideoFrame* frame, BMDTimeValue displayTime,
                                          BMDTimeValue displayDuration, BMDTimeScale timeScale) {
    if (!frame || timeScale <= 0) return E_INVALIDARG;
    ScheduledFrame sf{};
    FrameSink* sink;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_videoEnabled) return E_ACCESSDENIED;
        Pending p;
        p.frame = frame;
        p.streamNs = toNs(displayTime, timeScale);
        p.durationNs = toNs(displayDuration, timeScale);
        p.seq = ++m_seq;
        frame->AddRef();
        auto it = std::upper_bound(m_queue.begin(), m_queue.end(), p.streamNs,
                                   [](int64_t t, const Pending& q) { return t < q.streamNs; });
        m_queue.insert(it, p);
        sink = m_sink;
        sf.frame = frame;
        sf.seq = p.seq;
        sf.mode = m_mode;
        sf.streamTimeNs = p.streamNs;
        sf.durationNs = p.durationNs;
        sf.sync = false;
    }
    if (sink) sink->onFrame(sf);
    wakeClock();
    return S_OK;
}

HRESULT VirtualDevice::SetScheduledFrameCompletionCallback(IDeckLinkVideoOutputCallback* cb) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (cb) cb->AddRef();
    if (m_completionCb) m_completionCb->Release();
    m_completionCb = cb;
    VLOGD("SetScheduledFrameCompletionCallback %p", (void*)cb);
    return S_OK;
}

HRESULT VirtualDevice::GetBufferedVideoFrameCount(uint32_t* count) {
    if (!count) return E_POINTER;
    std::lock_guard<std::mutex> lk(m_mutex);
    *count = (uint32_t)m_queue.size();
    return S_OK;
}

HRESULT VirtualDevice::EnableAudioOutput(BMDAudioSampleRate rate, BMDAudioSampleType type, uint32_t channels,
                                         BMDAudioOutputStreamType streamType) {
    VLOGI("EnableAudioOutput rate=%u type=%u ch=%u stream=%u", rate, type, channels, streamType);
    if (rate != bmdAudioSampleRate48kHz || (type != 16 && type != 32) || channels == 0 ||
        channels > kMaxAudioChannels)
        return E_INVALIDARG;
    std::lock_guard<std::mutex> lk(m_mutex);
    m_audioEnabled = true;
    m_audioChannels = channels;
    m_audioBits = type;
    m_audioEndSample = 0;
    m_audioPrerollStartSample = -1;
    return S_OK;
}

HRESULT VirtualDevice::DisableAudioOutput() {
    VLOGI("DisableAudioOutput");
    std::lock_guard<std::mutex> lk(m_mutex);
    m_audioEnabled = false;
    m_audioPreroll = false;
    m_audioEndSample = 0;
    return S_OK;
}

HRESULT VirtualDevice::WriteAudioSamplesSync(void* buffer, uint32_t count, uint32_t* written) {
    FrameSink* sink;
    uint32_t ch, bits;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_audioEnabled) return E_ACCESSDENIED;
        sink = m_sink;
        ch = m_audioChannels;
        bits = m_audioBits;
    }
    if (sink && buffer) sink->onAudio(buffer, count, ch, bits, -1);
    if (written) *written = count;
    return S_OK;
}

HRESULT VirtualDevice::BeginAudioPreroll() {
    VLOGD("BeginAudioPreroll");
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_audioPreroll = true;
    }
    wakeClock();
    return S_OK;
}

HRESULT VirtualDevice::EndAudioPreroll() {
    VLOGD("EndAudioPreroll");
    std::lock_guard<std::mutex> lk(m_mutex);
    m_audioPreroll = false;
    return S_OK;
}

HRESULT VirtualDevice::ScheduleAudioSamples(void* buffer, uint32_t count, BMDTimeValue streamTime,
                                            BMDTimeScale timeScale, uint32_t* written) {
    FrameSink* sink;
    uint32_t ch, bits, accepted;
    int64_t streamNs;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_audioEnabled) return E_ACCESSDENIED;
        int64_t startSample = timeScale > 0 ? (int64_t)((__int128)streamTime * kAudioRate / timeScale) : m_audioEndSample;
        if (timeScale <= 0) streamTime = 0;
        int64_t nowSample = m_running ? streamNsAtHostLocked(nowNs()) * kAudioRate / kNsPerSec
                                      : (m_audioPrerollStartSample >= 0 ? m_audioPrerollStartSample : startSample);
        if (m_audioPrerollStartSample < 0) m_audioPrerollStartSample = startSample;
        int64_t buffered = std::max<int64_t>(0, m_audioEndSample - nowSample);
        int64_t room = std::max<int64_t>(0, kMaxBufferedAudioSamples - buffered);
        accepted = (uint32_t)std::min<int64_t>(count, room);
        m_audioEndSample = std::max(m_audioEndSample, startSample + accepted);
        streamNs = startSample * kNsPerSec / kAudioRate;
        sink = m_sink;
        ch = m_audioChannels;
        bits = m_audioBits;
    }
    if (sink && buffer && accepted) sink->onAudio(buffer, accepted, ch, bits, streamNs);
    if (written) *written = accepted;
    return S_OK;
}

HRESULT VirtualDevice::GetBufferedAudioSampleFrameCount(uint32_t* buffered) {
    if (!buffered) return E_POINTER;
    std::lock_guard<std::mutex> lk(m_mutex);
    int64_t nowSample = m_running ? streamNsAtHostLocked(nowNs()) * kAudioRate / kNsPerSec
                                  : (m_audioPrerollStartSample >= 0 ? m_audioPrerollStartSample : m_audioEndSample);
    *buffered = (uint32_t)std::max<int64_t>(0, m_audioEndSample - nowSample);
    return S_OK;
}

HRESULT VirtualDevice::FlushBufferedAudioSamples() {
    VLOGD("FlushBufferedAudioSamples");
    std::lock_guard<std::mutex> lk(m_mutex);
    m_audioEndSample = 0;
    m_audioPrerollStartSample = -1;
    return S_OK;
}

HRESULT VirtualDevice::SetAudioCallback(IDeckLinkAudioOutputCallback* cb) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (cb) cb->AddRef();
        if (m_audioCb) m_audioCb->Release();
        m_audioCb = cb;
    }
    VLOGD("SetAudioCallback %p", (void*)cb);
    wakeClock();
    return S_OK;
}

HRESULT VirtualDevice::StartScheduledPlayback(BMDTimeValue startTime, BMDTimeScale timeScale, double speed) {
    VLOGI("StartScheduledPlayback start=%lld scale=%lld speed=%f", (long long)startTime, (long long)timeScale, speed);
    if (timeScale <= 0) return E_INVALIDARG;
    FrameSink* sink;
    int64_t anchorHost, anchorStream;
    uint32_t gen;
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        if (!m_videoEnabled) return E_ACCESSDENIED;
        // Resolve restarts the stream immediately after stopping it; deliver the previous run's flush and
        // ScheduledPlaybackHasStopped first so callbacks never arrive out of order (bounded wait).
        if (m_flushAndNotifyStop || m_stopDelivering) {
            wakeClock();
            if (!m_cv.wait_for(lk, std::chrono::milliseconds(150),
                               [this] { return !m_flushAndNotifyStop && !m_stopDelivering; }))
                VLOGW("StartScheduledPlayback: previous stop still being delivered after 150 ms");
        }
        m_running = true;
        m_runGen++;
        m_speed = speed == 0 ? 1.0 : speed;
        m_anchorHostNs = nowNs();
        m_anchorStreamNs = toNs(startTime, timeScale);
        m_stopAtStreamNs = -1;
        m_flushAndNotifyStop = false;
        sink = m_sink;
        anchorHost = m_anchorHostNs;
        anchorStream = m_anchorStreamNs;
        gen = m_runGen;
    }
    if (sink) sink->onPlaybackStarted(anchorHost, anchorStream, speed, gen);
    wakeClock();
    return S_OK;
}

HRESULT VirtualDevice::StopScheduledPlayback(BMDTimeValue stopAt, BMDTimeValue* actualStop, BMDTimeScale timeScale) {
    VLOGI("StopScheduledPlayback at=%lld scale=%lld", (long long)stopAt, (long long)timeScale);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_running) {
            if (actualStop) *actualStop = timeScale > 0 ? fromNs(m_stoppedStreamNs, timeScale) : 0;
            return S_OK;
        }
        int64_t nowStream = streamNsAtHostLocked(nowNs());
        if (stopAt == 0 || timeScale <= 0 || toNs(stopAt, timeScale) <= nowStream) {
            m_stoppedStreamNs = nowStream;
            m_running = false;
            m_flushAndNotifyStop = true;
            m_stoppingGen = m_runGen;
        } else {
            m_stopAtStreamNs = toNs(stopAt, timeScale);
            m_stoppedStreamNs = m_stopAtStreamNs;
        }
        if (actualStop) *actualStop = timeScale > 0 ? fromNs(m_stoppedStreamNs, timeScale) : 0;
    }
    wakeClock();
    return S_OK;
}

HRESULT VirtualDevice::IsScheduledPlaybackRunning(bool* active) {
    if (!active) return E_POINTER;
    std::lock_guard<std::mutex> lk(m_mutex);
    *active = m_running;
    return S_OK;
}

HRESULT VirtualDevice::GetScheduledStreamTime(BMDTimeScale scale, BMDTimeValue* streamTime, double* speed) {
    if (scale <= 0) return E_INVALIDARG;
    std::lock_guard<std::mutex> lk(m_mutex);
    int64_t ns = m_running ? streamNsAtHostLocked(nowNs()) : m_stoppedStreamNs;
    if (streamTime) *streamTime = fromNs(ns, scale);
    if (speed) *speed = m_running ? m_speed : 0.0;
    return S_OK;
}

HRESULT VirtualDevice::GetReferenceStatus(BMDReferenceStatus* status) {
    if (status) *status = bmdReferenceUnlocked;
    return S_OK;
}

HRESULT VirtualDevice::GetHardwareReferenceClock(BMDTimeScale scale, BMDTimeValue* hardwareTime,
                                                 BMDTimeValue* timeInFrame, BMDTimeValue* ticksPerFrame) {
    if (scale <= 0) return E_INVALIDARG;
    int64_t now = nowNs();
    const ModeInfo* m;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m = m_mode;
    }
    BMDTimeValue hw = fromNs(now, scale);
    BMDTimeValue tpf = m ? (BMDTimeValue)((__int128)m->frameDuration * scale / m->timeScale) : scale / 25;
    if (tpf <= 0) tpf = 1;
    if (hardwareTime) *hardwareTime = hw;
    if (ticksPerFrame) *ticksPerFrame = tpf;
    if (timeInFrame) *timeInFrame = hw % tpf;
    return S_OK;
}

HRESULT VirtualDevice::GetFrameCompletionReferenceTimestamp(IDeckLinkVideoFrame* frame, BMDTimeScale scale,
                                                            BMDTimeValue* ts) {
    if (!ts || scale <= 0) return E_INVALIDARG;
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_completionTimes.find(frame);
    if (it == m_completionTimes.end()) return E_FAIL;
    *ts = fromNs(it->second, scale);
    return S_OK;
}

// ---------------------------------------------------------------------------------------------
// Virtual scan-out clock

int64_t VirtualDevice::streamNsAtHostLocked(int64_t hostNs) const {
    return m_anchorStreamNs + (int64_t)((double)(hostNs - m_anchorHostNs) * m_speed);
}

int64_t VirtualDevice::hostNsForStreamLocked(int64_t streamNs) const {
    return m_anchorHostNs + (int64_t)((double)(streamNs - m_anchorStreamNs) / m_speed);
}

void VirtualDevice::wakeClock() { m_cv.notify_all(); }

// The card's scan-out clock must tick on time even when Resolve saturates the CPU: ordinary threads see several ms of
// timer coalescing under load. A time-constraint (real-time) policy is what Core Audio uses for its IO threads; the
// kernel demotes the thread automatically if a tick ever overruns, so this cannot starve the system.
static void makeClockThreadRealtime() {
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    auto toAbs = [&](double ms) { return (uint32_t)(ms * 1e6 * tb.denom / tb.numer); };
    thread_time_constraint_policy_data_t p;
    p.period = 0;   // event driven, not periodic
    p.computation = toAbs(5.0);   // room for Resolve's DrawFrame and completion callbacks
    p.constraint = toAbs(10.0);
    p.preemptible = TRUE;
    kern_return_t kr = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                                         (thread_policy_t)&p, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    if (kr != KERN_SUCCESS) {
        VLOGW("clock thread: real-time policy refused (%d); using user-interactive QoS", kr);
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    }
}

void VirtualDevice::clockThreadMain() {
    pthread_setname_np("VRLink.clock");
    makeClockThreadRealtime();
    std::unique_lock<std::mutex> lk(m_mutex);
    int64_t nextAudioTick = 0;
    while (!m_quit) {
        int64_t now = nowNs();
        struct Completion {
            IDeckLinkVideoFrame* frame;
            BMDOutputFrameCompletionResult result;
            uint64_t seq;
            int64_t hostNs;
        };
        std::vector<Completion> done;
        bool notifyStopped = false;

        // Deferred stop reached?
        if (m_running && m_stopAtStreamNs >= 0 && streamNsAtHostLocked(now) >= m_stopAtStreamNs) {
            m_running = false;
            m_stopAtStreamNs = -1;
            m_flushAndNotifyStop = true;
            m_stoppingGen = m_runGen;
        }

        if (m_running) {
            int64_t frameNs = m_mode ? toNs(m_mode->frameDuration, m_mode->timeScale) : 40000000;
            while (!m_queue.empty()) {
                Pending& p = m_queue.front();
                int64_t due = hostNsForStreamLocked(p.streamNs);
                if (due > now) break;
                BMDOutputFrameCompletionResult r = bmdOutputFrameCompleted;
                if (now - due > 2 * frameNs) r = bmdOutputFrameDropped;
                else if (now - due > frameNs / 2) r = bmdOutputFrameDisplayedLate;
                done.push_back({p.frame, r, p.seq, now});
                m_queue.pop_front();
            }
        }
        uint32_t stoppedGen = 0;
        if (m_flushAndNotifyStop) {
            for (auto& p : m_queue) done.push_back({p.frame, bmdOutputFrameFlushed, p.seq, now});
            m_queue.clear();
            notifyStopped = true;
            m_flushAndNotifyStop = false;
            m_stopDelivering = true;
            stoppedGen = m_stoppingGen;
        }

        bool audioTick = false;
        bool prerollTick = false;
        if (m_audioCb && m_audioEnabled && now >= nextAudioTick && (m_running || m_audioPreroll)) {
            audioTick = true;
            prerollTick = m_audioPreroll && !m_running;
            nextAudioTick = now + kAudioTickNs;
        }

        IDeckLinkVideoOutputCallback* cb = m_completionCb;
        IDeckLinkAudioOutputCallback* acb = m_audioCb;
        IDeckLinkScreenPreviewCallback* preview = m_previewCb;
        FrameSink* sink = m_sink;
        if (cb) cb->AddRef();
        if (acb) acb->AddRef();
        if (preview) preview->AddRef();
        for (auto& d : done)
            if (d.result != bmdOutputFrameFlushed) m_completionTimes[d.frame] = d.hostNs;
        if (m_completionTimes.size() > 64) m_completionTimes.clear();

        lk.unlock();
        // Only the newest frame due this tick is "on screen"; earlier ones (if we fell behind) were dropped.
        const Completion* shown = nullptr;
        for (auto& d : done)
            if (d.result != bmdOutputFrameFlushed && d.result != bmdOutputFrameDropped) shown = &d;
        for (auto& d : done) {
            if (sink && d.result != bmdOutputFrameFlushed)
                sink->onFrameDisplayed(d.seq, d.hostNs, d.result != bmdOutputFrameCompleted);
            if (preview && &d == shown) preview->DrawFrame(d.frame);
            if (cb) cb->ScheduledFrameCompleted(d.frame, d.result);
            d.frame->Release();
        }
        if (notifyStopped) {
            if (sink) sink->onPlaybackStopped(stoppedGen);
            if (cb) cb->ScheduledPlaybackHasStopped();
            VLOGI("Scheduled playback stopped (run %u)", stoppedGen);
        }
        if (audioTick && acb) acb->RenderAudioSamples(prerollTick);
        if (cb) cb->Release();
        if (acb) acb->Release();
        if (preview) preview->Release();
        lk.lock();
        if (notifyStopped) {
            m_stopDelivering = false;
            m_cv.notify_all();   // StartScheduledPlayback / DisableVideoOutput may be waiting
        }

        // Sleep until the next event.
        int64_t wakeAt = nowNs() + 50 * 1000000LL;
        if (m_running && !m_queue.empty()) wakeAt = std::min(wakeAt, hostNsForStreamLocked(m_queue.front().streamNs));
        if (m_running && m_stopAtStreamNs >= 0) wakeAt = std::min(wakeAt, hostNsForStreamLocked(m_stopAtStreamNs));
        if (m_audioCb && m_audioEnabled && (m_running || m_audioPreroll)) wakeAt = std::min(wakeAt, nextAudioTick);
        if (m_flushAndNotifyStop) wakeAt = nowNs();
        int64_t waitNs = wakeAt - nowNs();
        if (waitNs > 0) m_cv.wait_for(lk, std::chrono::nanoseconds(waitNs));
    }
}

}  // namespace vrl
