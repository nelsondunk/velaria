// DaVinci Resolve (as of 21.1) talks to DeckLink devices through the SDK 10.11 interface generation.
// IDeckLinkOutput_v10_11 differs from the current IDeckLinkOutput only in DoesSupportVideoMode (and has no
// GetDisplayMode); everything else forwards to the device. The 10.11 attributes/configuration/notification
// interfaces are vtable-identical to the current ones and are served by the same objects.
#pragma once
#include "Common.h"
#include "DeckLinkAPIVideoOutput_v10_11.h"
#include "DeckLinkAPI_v10_11.h"
#include "DeckLinkAPIConfiguration_v10_11.h"

namespace vrl {

class VirtualDevice;

class OutputV10_11 final : public IDeckLinkOutput_v10_11 {
public:
    explicit OutputV10_11(VirtualDevice* d) : m_dev(d) {}
    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override;
    ULONG AddRef() override;
    ULONG Release() override;

    HRESULT DoesSupportVideoMode(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat, BMDVideoOutputFlags flags,
                                 BMDDisplayModeSupport_v10_11* result,
                                 IDeckLinkDisplayMode** resultDisplayMode) override;
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

private:
    IDeckLinkOutput* out();
    VirtualDevice* m_dev;
};

}  // namespace vrl
