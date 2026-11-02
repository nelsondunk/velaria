#pragma once
#include "Common.h"
#include "DisplayModes.h"
#include "FrameSink.h"
#include "CompatV10_11.h"

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace vrl {

// The virtual DeckLink: one output-only device whose "scan-out" is a software clock.
class VirtualDevice;

// Attributes, status and configuration all use GetFlag/GetInt/... with uint32_t IDs whose FourCCs overlap
// (e.g. 'vocn'), so each is a separate tear-off object that forwards lifetime to the device.
class DeviceAttributes final : public IDeckLinkProfileAttributes {
public:
    explicit DeviceAttributes(VirtualDevice* d) : m_dev(d) {}
    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override;
    ULONG AddRef() override;
    ULONG Release() override;
    HRESULT GetFlag(BMDDeckLinkAttributeID id, bool* value) override;
    HRESULT GetInt(BMDDeckLinkAttributeID id, int64_t* value) override;
    HRESULT GetFloat(BMDDeckLinkAttributeID id, double* value) override;
    HRESULT GetString(BMDDeckLinkAttributeID id, CFStringRef* value) override;
private:
    VirtualDevice* m_dev;
};

class DeviceStatus final : public IDeckLinkStatus {
public:
    explicit DeviceStatus(VirtualDevice* d) : m_dev(d) {}
    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override;
    ULONG AddRef() override;
    ULONG Release() override;
    HRESULT GetFlag(BMDDeckLinkStatusID id, bool* value) override;
    HRESULT GetInt(BMDDeckLinkStatusID id, int64_t* value) override;
    HRESULT GetFloat(BMDDeckLinkStatusID id, double* value) override;
    HRESULT GetString(BMDDeckLinkStatusID id, CFStringRef* value) override;
    HRESULT GetBytes(BMDDeckLinkStatusID id, void* buffer, uint32_t* bufferSize) override;
private:
    VirtualDevice* m_dev;
};

class DeviceConfiguration final : public IDeckLinkConfiguration {
public:
    explicit DeviceConfiguration(VirtualDevice* d) : m_dev(d) {}
    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override;
    ULONG AddRef() override;
    ULONG Release() override;
    HRESULT SetFlag(BMDDeckLinkConfigurationID id, bool value) override;
    HRESULT GetFlag(BMDDeckLinkConfigurationID id, bool* value) override;
    HRESULT SetInt(BMDDeckLinkConfigurationID id, int64_t value) override;
    HRESULT GetInt(BMDDeckLinkConfigurationID id, int64_t* value) override;
    HRESULT SetFloat(BMDDeckLinkConfigurationID id, double value) override;
    HRESULT GetFloat(BMDDeckLinkConfigurationID id, double* value) override;
    HRESULT SetString(BMDDeckLinkConfigurationID id, CFStringRef value) override;
    HRESULT GetString(BMDDeckLinkConfigurationID id, CFStringRef* value) override;
    HRESULT WriteConfigurationToPreferences() override;
private:
    VirtualDevice* m_dev;
};

class VirtualDevice final : public IDeckLink,
                            public IDeckLinkOutput,
                            public IDeckLinkNotification,
                            public RefCounted {
public:
    static VirtualDevice* shared();   // +0; process-wide singleton (never destroyed)

    // IUnknown
    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override;
    VRL_REFCOUNT_METHODS

    // IDeckLink
    HRESULT GetModelName(CFStringRef* modelName) override;
    HRESULT GetDisplayName(CFStringRef* displayName) override;

    // IDeckLinkNotification
    HRESULT Subscribe(BMDNotifications topic, IDeckLinkNotificationCallback* cb) override;
    HRESULT Unsubscribe(BMDNotifications topic, IDeckLinkNotificationCallback* cb) override;

    // IDeckLinkOutput
    HRESULT DoesSupportVideoMode(BMDVideoConnection connection, BMDDisplayMode requestedMode,
                                 BMDPixelFormat requestedPixelFormat, BMDVideoOutputConversionMode conversionMode,
                                 BMDSupportedVideoModeFlags flags, BMDDisplayMode* actualMode,
                                 bool* supported) override;
    HRESULT GetDisplayMode(BMDDisplayMode displayMode, IDeckLinkDisplayMode** resultDisplayMode) override;
    HRESULT GetDisplayModeIterator(IDeckLinkDisplayModeIterator** iterator) override;
    HRESULT SetScreenPreviewCallback(IDeckLinkScreenPreviewCallback* previewCallback) override;
    HRESULT EnableVideoOutput(BMDDisplayMode displayMode, BMDVideoOutputFlags flags) override;
    HRESULT DisableVideoOutput() override;
    HRESULT SetVideoOutputFrameMemoryAllocator(IDeckLinkMemoryAllocator* theAllocator) override;
    HRESULT CreateVideoFrame(int32_t width, int32_t height, int32_t rowBytes, BMDPixelFormat pixelFormat,
                             BMDFrameFlags flags, IDeckLinkMutableVideoFrame** outFrame) override;
    HRESULT CreateAncillaryData(BMDPixelFormat pixelFormat, IDeckLinkVideoFrameAncillary** outBuffer) override;
    HRESULT DisplayVideoFrameSync(IDeckLinkVideoFrame* theFrame) override;
    HRESULT ScheduleVideoFrame(IDeckLinkVideoFrame* theFrame, BMDTimeValue displayTime, BMDTimeValue displayDuration,
                               BMDTimeScale timeScale) override;
    HRESULT SetScheduledFrameCompletionCallback(IDeckLinkVideoOutputCallback* theCallback) override;
    HRESULT GetBufferedVideoFrameCount(uint32_t* bufferedFrameCount) override;
    HRESULT EnableAudioOutput(BMDAudioSampleRate sampleRate, BMDAudioSampleType sampleType, uint32_t channelCount,
                              BMDAudioOutputStreamType streamType) override;
    HRESULT DisableAudioOutput() override;
    HRESULT WriteAudioSamplesSync(void* buffer, uint32_t sampleFrameCount, uint32_t* sampleFramesWritten) override;
    HRESULT BeginAudioPreroll() override;
    HRESULT EndAudioPreroll() override;
    HRESULT ScheduleAudioSamples(void* buffer, uint32_t sampleFrameCount, BMDTimeValue streamTime,
                                 BMDTimeScale timeScale, uint32_t* sampleFramesWritten) override;
    HRESULT GetBufferedAudioSampleFrameCount(uint32_t* bufferedSampleFrameCount) override;
    HRESULT FlushBufferedAudioSamples() override;
    HRESULT SetAudioCallback(IDeckLinkAudioOutputCallback* theCallback) override;
    HRESULT StartScheduledPlayback(BMDTimeValue playbackStartTime, BMDTimeScale timeScale,
                                   double playbackSpeed) override;
    HRESULT StopScheduledPlayback(BMDTimeValue stopPlaybackAtTime, BMDTimeValue* actualStopTime,
                                  BMDTimeScale timeScale) override;
    HRESULT IsScheduledPlaybackRunning(bool* active) override;
    HRESULT GetScheduledStreamTime(BMDTimeScale desiredTimeScale, BMDTimeValue* streamTime,
                                   double* playbackSpeed) override;
    HRESULT GetReferenceStatus(BMDReferenceStatus* referenceStatus) override;
    HRESULT GetHardwareReferenceClock(BMDTimeScale desiredTimeScale, BMDTimeValue* hardwareTime,
                                      BMDTimeValue* timeInFrame, BMDTimeValue* ticksPerFrame) override;
    HRESULT GetFrameCompletionReferenceTimestamp(IDeckLinkVideoFrame* theFrame, BMDTimeScale desiredTimeScale,
                                                 BMDTimeValue* frameCompletionTimestamp) override;

    void setSink(FrameSink* sink);

    // used by tear-offs
    struct ConfigValue {
        int kind = 0;   // 1 flag, 2 int, 3 float, 4 string
        int64_t i = 0;
        double f = 0;
        std::string s;
    };
    bool configGet(uint32_t id, ConfigValue& out);
    void configSet(uint32_t id, const ConfigValue& v);
    const ModeInfo* currentMode();
    bool isRunning();

protected:
    void destroy() override {}   // singleton

private:
    VirtualDevice();
    struct Pending {
        IDeckLinkVideoFrame* frame;
        int64_t streamNs;
        int64_t durationNs;
        uint64_t seq;
    };
    void clockThreadMain();
    int64_t streamNsAtHostLocked(int64_t hostNs) const;
    int64_t hostNsForStreamLocked(int64_t streamNs) const;
    void wakeClock();

    DeviceAttributes m_attributes{this};
    DeviceStatus m_status{this};
    DeviceConfiguration m_configuration{this};
    OutputV10_11 m_outputV10_11{this};

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_clockThread;
    bool m_quit = false;

    FrameSink* m_sink = nullptr;
    const ModeInfo* m_mode = nullptr;
    BMDVideoOutputFlags m_outputFlags = 0;
    bool m_videoEnabled = false;

    IDeckLinkVideoOutputCallback* m_completionCb = nullptr;
    IDeckLinkAudioOutputCallback* m_audioCb = nullptr;
    IDeckLinkMemoryAllocator* m_allocator = nullptr;
    IDeckLinkScreenPreviewCallback* m_previewCb = nullptr;
    std::vector<std::pair<BMDNotifications, IDeckLinkNotificationCallback*>> m_notify;

    std::deque<Pending> m_queue;          // sorted by streamNs
    uint64_t m_seq = 0;
    bool m_running = false;
    double m_speed = 1.0;
    int64_t m_anchorHostNs = 0;
    int64_t m_anchorStreamNs = 0;
    int64_t m_stoppedStreamNs = 0;
    bool m_stopPending = false;
    int64_t m_stopAtStreamNs = -1;        // deferred stop
    bool m_flushAndNotifyStop = false;
    bool m_stopDelivering = false;       // clock thread is delivering a stop's callbacks (outside the lock)
    uint32_t m_runGen = 0;               // incremented per StartScheduledPlayback
    uint32_t m_stoppingGen = 0;          // generation whose stop is pending delivery
    std::map<IDeckLinkVideoFrame*, int64_t> m_completionTimes;

    // audio
    bool m_audioEnabled = false;
    uint32_t m_audioChannels = 2;
    uint32_t m_audioBits = 16;
    bool m_audioPreroll = false;
    int64_t m_audioEndSample = 0;         // end of scheduled audio, in samples of stream time
    int64_t m_audioPrerollStartSample = -1;

    std::map<uint32_t, ConfigValue> m_config;
};

}  // namespace vrl
