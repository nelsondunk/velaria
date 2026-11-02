#include "CompatV10_11.h"

#include "VirtualDevice.h"

namespace vrl {

IDeckLinkOutput* OutputV10_11::out() { return static_cast<IDeckLinkOutput*>(m_dev); }

HRESULT OutputV10_11::QueryInterface(REFIID iid, LPVOID* ppv) {
    if (!ppv) return E_POINTER;
    if (iidEq(iid, IID_IDeckLinkOutput_v10_11)) {
        *ppv = static_cast<IDeckLinkOutput_v10_11*>(this);
        AddRef();
        return S_OK;
    }
    return m_dev->QueryInterface(iid, ppv);
}
ULONG OutputV10_11::AddRef() { return m_dev->AddRef(); }
ULONG OutputV10_11::Release() { return m_dev->Release(); }

HRESULT OutputV10_11::DoesSupportVideoMode(BMDDisplayMode displayMode, BMDPixelFormat pixelFormat,
                                           BMDVideoOutputFlags flags, BMDDisplayModeSupport_v10_11* result,
                                           IDeckLinkDisplayMode** resultDisplayMode) {
    bool supported = false;
    BMDDisplayMode actual = bmdModeUnknown;
    out()->DoesSupportVideoMode(bmdVideoConnectionUnspecified, displayMode, pixelFormat, bmdNoVideoOutputConversion,
                                bmdSupportedVideoModeDefault, &actual, &supported);
    if (result) *result = supported ? bmdDisplayModeSupported_v10_11 : bmdDisplayModeNotSupported_v10_11;
    if (resultDisplayMode) {
        *resultDisplayMode = nullptr;
        if (supported) out()->GetDisplayMode(displayMode, resultDisplayMode);
    }
    VLOGD("v10_11 DoesSupportVideoMode %s pf=%s flags=0x%x -> %d", fourcc(displayMode).c_str(),
          fourcc(pixelFormat).c_str(), flags, supported);
    return S_OK;
}

HRESULT OutputV10_11::GetDisplayModeIterator(IDeckLinkDisplayModeIterator** it) { return out()->GetDisplayModeIterator(it); }
HRESULT OutputV10_11::SetScreenPreviewCallback(IDeckLinkScreenPreviewCallback* cb) { return out()->SetScreenPreviewCallback(cb); }
HRESULT OutputV10_11::EnableVideoOutput(BMDDisplayMode m, BMDVideoOutputFlags f) { return out()->EnableVideoOutput(m, f); }
HRESULT OutputV10_11::DisableVideoOutput() { return out()->DisableVideoOutput(); }
HRESULT OutputV10_11::SetVideoOutputFrameMemoryAllocator(IDeckLinkMemoryAllocator* a) {
    return out()->SetVideoOutputFrameMemoryAllocator(a);
}
HRESULT OutputV10_11::CreateVideoFrame(int32_t w, int32_t h, int32_t rb, BMDPixelFormat pf, BMDFrameFlags fl,
                                       IDeckLinkMutableVideoFrame** f) {
    return out()->CreateVideoFrame(w, h, rb, pf, fl, f);
}
HRESULT OutputV10_11::CreateAncillaryData(BMDPixelFormat pf, IDeckLinkVideoFrameAncillary** b) {
    return out()->CreateAncillaryData(pf, b);
}
HRESULT OutputV10_11::DisplayVideoFrameSync(IDeckLinkVideoFrame* f) { return out()->DisplayVideoFrameSync(f); }
HRESULT OutputV10_11::ScheduleVideoFrame(IDeckLinkVideoFrame* f, BMDTimeValue t, BMDTimeValue d, BMDTimeScale s) {
    return out()->ScheduleVideoFrame(f, t, d, s);
}
HRESULT OutputV10_11::SetScheduledFrameCompletionCallback(IDeckLinkVideoOutputCallback* cb) {
    return out()->SetScheduledFrameCompletionCallback(cb);
}
HRESULT OutputV10_11::GetBufferedVideoFrameCount(uint32_t* c) { return out()->GetBufferedVideoFrameCount(c); }
HRESULT OutputV10_11::EnableAudioOutput(BMDAudioSampleRate r, BMDAudioSampleType t, uint32_t c, BMDAudioOutputStreamType s) {
    return out()->EnableAudioOutput(r, t, c, s);
}
HRESULT OutputV10_11::DisableAudioOutput() { return out()->DisableAudioOutput(); }
HRESULT OutputV10_11::WriteAudioSamplesSync(void* b, uint32_t n, uint32_t* w) { return out()->WriteAudioSamplesSync(b, n, w); }
HRESULT OutputV10_11::BeginAudioPreroll() { return out()->BeginAudioPreroll(); }
HRESULT OutputV10_11::EndAudioPreroll() { return out()->EndAudioPreroll(); }
HRESULT OutputV10_11::ScheduleAudioSamples(void* b, uint32_t n, BMDTimeValue t, BMDTimeScale s, uint32_t* w) {
    return out()->ScheduleAudioSamples(b, n, t, s, w);
}
HRESULT OutputV10_11::GetBufferedAudioSampleFrameCount(uint32_t* c) { return out()->GetBufferedAudioSampleFrameCount(c); }
HRESULT OutputV10_11::FlushBufferedAudioSamples() { return out()->FlushBufferedAudioSamples(); }
HRESULT OutputV10_11::SetAudioCallback(IDeckLinkAudioOutputCallback* cb) { return out()->SetAudioCallback(cb); }
HRESULT OutputV10_11::StartScheduledPlayback(BMDTimeValue t, BMDTimeScale s, double sp) {
    return out()->StartScheduledPlayback(t, s, sp);
}
HRESULT OutputV10_11::StopScheduledPlayback(BMDTimeValue t, BMDTimeValue* a, BMDTimeScale s) {
    return out()->StopScheduledPlayback(t, a, s);
}
HRESULT OutputV10_11::IsScheduledPlaybackRunning(bool* a) { return out()->IsScheduledPlaybackRunning(a); }
HRESULT OutputV10_11::GetScheduledStreamTime(BMDTimeScale s, BMDTimeValue* t, double* sp) {
    return out()->GetScheduledStreamTime(s, t, sp);
}
HRESULT OutputV10_11::GetReferenceStatus(BMDReferenceStatus* r) { return out()->GetReferenceStatus(r); }
HRESULT OutputV10_11::GetHardwareReferenceClock(BMDTimeScale s, BMDTimeValue* h, BMDTimeValue* t, BMDTimeValue* p) {
    return out()->GetHardwareReferenceClock(s, h, t, p);
}
HRESULT OutputV10_11::GetFrameCompletionReferenceTimestamp(IDeckLinkVideoFrame* f, BMDTimeScale s, BMDTimeValue* t) {
    return out()->GetFrameCompletionReferenceTimestamp(f, s, t);
}

}  // namespace vrl
