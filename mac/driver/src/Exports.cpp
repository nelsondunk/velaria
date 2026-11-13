// Entry points that DeckLinkAPIDispatch.cpp (compiled into client apps) looks up by name.
//
// Each one answers for Velaria and, when Blackmagic's own framework has been set aside by `install.sh --coexist`
// and loads underneath (Coexist.h), for Blackmagic as well. With nothing set aside the behaviour is what it was
// before coexistence: one Velaria device, and nullptr from the helpers Velaria does not provide.
// The coexistence path is UNTESTED (never compiled for or run on macOS as of 13 Nov 2026).
#include <unistd.h>

#include "Coexist.h"
#include "Common.h"
#include "VirtualDevice.h"

using namespace vrl;

namespace {

IDeckLink* velariaDevice() { return static_cast<IDeckLink*>(VirtualDevice::shared()); }

// Helpers Velaria has no implementation of: Blackmagic's when available, otherwise nullptr as before.
template <class T>
T* passThrough(const char* symbol) {
    T* obj = coexist::createFromUnderlying<T>(symbol);
    VLOGI("%s -> %s", symbol, obj ? "Blackmagic's" : "unsupported (nullptr)");
    return obj;
}

}  // namespace

VRL_EXPORT IDeckLinkIterator* CreateDeckLinkIteratorInstance_0004(void) {
    return coexist::createIterator(velariaDevice, "CreateDeckLinkIteratorInstance_0004");
}
VRL_EXPORT IDeckLinkIterator* CreateDeckLinkIteratorInstance_0003(void) {
    return coexist::createIterator(velariaDevice, "CreateDeckLinkIteratorInstance_0003");
}
VRL_EXPORT IDeckLinkIterator* CreateDeckLinkIteratorInstance_0002(void) {
    return coexist::createIterator(velariaDevice, "CreateDeckLinkIteratorInstance_0002");
}
VRL_EXPORT IDeckLinkDiscovery* CreateDeckLinkDiscoveryInstance_0003(void) {
    return coexist::createDiscovery(velariaDevice, "CreateDeckLinkDiscoveryInstance_0003");
}
VRL_EXPORT IDeckLinkDiscovery* CreateDeckLinkDiscoveryInstance_0002(void) {
    return coexist::createDiscovery(velariaDevice, "CreateDeckLinkDiscoveryInstance_0002");
}
VRL_EXPORT IDeckLinkDiscovery* CreateDeckLinkDiscoveryInstance_0001(void) {
    return coexist::createDiscovery(velariaDevice, "CreateDeckLinkDiscoveryInstance_0001");
}
VRL_EXPORT IDeckLinkAPIInformation* CreateDeckLinkAPIInformationInstance_0001(void) {
    return coexist::createAPIInformation();
}
VRL_EXPORT IDeckLinkVideoConversion* CreateVideoConversionInstance_0001(void) {
    return passThrough<IDeckLinkVideoConversion>("CreateVideoConversionInstance_0001");
}
VRL_EXPORT IDeckLinkVideoFrameAncillaryPackets* CreateVideoFrameAncillaryPacketsInstance_0001(void) {
    return passThrough<IDeckLinkVideoFrameAncillaryPackets>("CreateVideoFrameAncillaryPacketsInstance_0001");
}
VRL_EXPORT IDeckLinkGLScreenPreviewHelper* CreateOpenGLScreenPreviewHelper_0001(void) {
    return passThrough<IDeckLinkGLScreenPreviewHelper>("CreateOpenGLScreenPreviewHelper_0001");
}
VRL_EXPORT IDeckLinkGLScreenPreviewHelper* CreateOpenGL3ScreenPreviewHelper_0001(void) {
    return passThrough<IDeckLinkGLScreenPreviewHelper>("CreateOpenGL3ScreenPreviewHelper_0001");
}
VRL_EXPORT IDeckLinkCocoaScreenPreviewCallback* CreateCocoaScreenPreview_0001(void* parentView) {
    static const char* kSym = "CreateCocoaScreenPreview_0001";
    IDeckLinkCocoaScreenPreviewCallback* obj = nullptr;
    if (!coexist::Scope::entered()) {
        auto fn = reinterpret_cast<IDeckLinkCocoaScreenPreviewCallback* (*)(void*)>(coexist::underlyingSymbol(kSym));
        if (fn) {
            coexist::Scope s;
            obj = fn(parentView);
        }
    }
    VLOGI("%s -> %s", kSym, obj ? "Blackmagic's" : "unsupported (nullptr)");
    return obj;
}

__attribute__((constructor)) static void vrlinkLoaded() { VLOGI("Velaria DeckLinkAPI loaded into pid %d", getpid()); }
