// Entry points that DeckLinkAPIDispatch.cpp (compiled into client apps) looks up by name.
#include <cstdlib>
#include <mutex>

#include "Common.h"
#include "VirtualDevice.h"

namespace vrl {

namespace {

int64_t reportedApiVersion() {
    // Version the virtual driver reports as "Desktop Video". Override with VRLINK_API_VERSION=0x0c080000 etc.
    if (const char* e = getenv("VRLINK_API_VERSION")) return strtoll(e, nullptr, 0);
    return BLACKMAGIC_DECKLINK_API_VERSION;
}

class APIInformation final : public IDeckLinkAPIInformation, public RefCounted {
public:
    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (isIUnknown(iid) || iidEq(iid, IID_IDeckLinkAPIInformation)) {
            *ppv = static_cast<IDeckLinkAPIInformation*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        VLOGI("APIInformation QI %s (%s) -> E_NOINTERFACE", iidString(iid).c_str(), iidName(iid));
        return E_NOINTERFACE;
    }
    VRL_REFCOUNT_METHODS
    HRESULT GetFlag(BMDDeckLinkAPIInformationID id, bool* v) override {
        if (v) *v = false;
        VLOGD("APIInformation GetFlag %s", fourcc(id).c_str());
        return E_INVALIDARG;
    }
    HRESULT GetInt(BMDDeckLinkAPIInformationID id, int64_t* v) override {
        if (!v) return E_POINTER;
        if (id != BMDDeckLinkAPIVersion) return E_INVALIDARG;
        *v = reportedApiVersion();
        VLOGI("APIInformation GetInt version -> 0x%08llx", (unsigned long long)*v);
        return S_OK;
    }
    HRESULT GetFloat(BMDDeckLinkAPIInformationID, double* v) override {
        if (v) *v = 0;
        return E_INVALIDARG;
    }
    HRESULT GetString(BMDDeckLinkAPIInformationID id, CFStringRef* v) override {
        if (!v) return E_POINTER;
        if (id != BMDDeckLinkAPIVersion) return E_INVALIDARG;
        int64_t ver = reportedApiVersion();
        char buf[32];
        snprintf(buf, sizeof buf, "%d.%d.%d", (int)((ver >> 24) & 0xff), (int)((ver >> 16) & 0xff),
                 (int)((ver >> 8) & 0xff));
        *v = makeCFString(buf);
        VLOGI("APIInformation GetString version -> %s", buf);
        return S_OK;
    }
};

class Iterator final : public IDeckLinkIterator, public RefCounted {
public:
    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (isIUnknown(iid) || iidEq(iid, IID_IDeckLinkIterator)) {
            *ppv = static_cast<IDeckLinkIterator*>(this);
            AddRef();
            return S_OK;
        }
        if (iidEq(iid, IID_IDeckLinkAPIInformation)) {   // SDK idiom for querying the driver version
            *ppv = static_cast<IDeckLinkAPIInformation*>(new APIInformation());
            return S_OK;
        }
        *ppv = nullptr;
        VLOGI("Iterator QI %s (%s) -> E_NOINTERFACE", iidString(iid).c_str(), iidName(iid));
        return E_NOINTERFACE;
    }
    VRL_REFCOUNT_METHODS
    HRESULT Next(IDeckLink** out) override {
        if (!out) return E_POINTER;
        if (m_done) {
            *out = nullptr;
            return S_FALSE;
        }
        m_done = true;
        VirtualDevice* d = VirtualDevice::shared();
        d->AddRef();
        *out = static_cast<IDeckLink*>(d);
        VLOGI("Iterator Next -> VR Link device");
        return S_OK;
    }

private:
    bool m_done = false;
};

class Discovery final : public IDeckLinkDiscovery, public RefCounted {
public:
    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (isIUnknown(iid) || iidEq(iid, IID_IDeckLinkDiscovery)) {
            *ppv = static_cast<IDeckLinkDiscovery*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        VLOGI("Discovery QI %s (%s) -> E_NOINTERFACE", iidString(iid).c_str(), iidName(iid));
        return E_NOINTERFACE;
    }
    VRL_REFCOUNT_METHODS
    HRESULT InstallDeviceNotifications(IDeckLinkDeviceNotificationCallback* cb) override {
        if (!cb) return E_INVALIDARG;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (m_cb) return E_FAIL;
            cb->AddRef();
            m_cb = cb;
        }
        VLOGI("Discovery InstallDeviceNotifications -> announcing VR Link device");
        cb->DeckLinkDeviceArrived(static_cast<IDeckLink*>(VirtualDevice::shared()));
        return S_OK;
    }
    HRESULT UninstallDeviceNotifications() override {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_cb) {
            m_cb->Release();
            m_cb = nullptr;
        }
        VLOGI("Discovery UninstallDeviceNotifications");
        return S_OK;
    }

protected:
    ~Discovery() override {
        if (m_cb) m_cb->Release();
    }

private:
    std::mutex m_mutex;
    IDeckLinkDeviceNotificationCallback* m_cb = nullptr;
};

}  // namespace
}  // namespace vrl

using namespace vrl;

VRL_EXPORT IDeckLinkIterator* CreateDeckLinkIteratorInstance_0004(void) {
    VLOGI("CreateDeckLinkIteratorInstance_0004");
    return new Iterator();
}
VRL_EXPORT IDeckLinkIterator* CreateDeckLinkIteratorInstance_0003(void) {
    VLOGI("CreateDeckLinkIteratorInstance_0003");
    return new Iterator();
}
VRL_EXPORT IDeckLinkIterator* CreateDeckLinkIteratorInstance_0002(void) {
    VLOGI("CreateDeckLinkIteratorInstance_0002");
    return new Iterator();
}
VRL_EXPORT IDeckLinkDiscovery* CreateDeckLinkDiscoveryInstance_0003(void) {
    VLOGI("CreateDeckLinkDiscoveryInstance_0003");
    return new Discovery();
}
VRL_EXPORT IDeckLinkDiscovery* CreateDeckLinkDiscoveryInstance_0002(void) {
    VLOGI("CreateDeckLinkDiscoveryInstance_0002");
    return new Discovery();
}
VRL_EXPORT IDeckLinkDiscovery* CreateDeckLinkDiscoveryInstance_0001(void) {
    VLOGI("CreateDeckLinkDiscoveryInstance_0001");
    return new Discovery();
}
VRL_EXPORT IDeckLinkAPIInformation* CreateDeckLinkAPIInformationInstance_0001(void) {
    VLOGI("CreateDeckLinkAPIInformationInstance_0001");
    return new APIInformation();
}
VRL_EXPORT IDeckLinkVideoConversion* CreateVideoConversionInstance_0001(void) {
    VLOGI("CreateVideoConversionInstance_0001 (unsupported)");
    return nullptr;
}
VRL_EXPORT IDeckLinkVideoFrameAncillaryPackets* CreateVideoFrameAncillaryPacketsInstance_0001(void) {
    VLOGI("CreateVideoFrameAncillaryPacketsInstance_0001 (unsupported)");
    return nullptr;
}
VRL_EXPORT IDeckLinkGLScreenPreviewHelper* CreateOpenGLScreenPreviewHelper_0001(void) {
    VLOGI("CreateOpenGLScreenPreviewHelper_0001 (unsupported)");
    return nullptr;
}
VRL_EXPORT IDeckLinkGLScreenPreviewHelper* CreateOpenGL3ScreenPreviewHelper_0001(void) {
    VLOGI("CreateOpenGL3ScreenPreviewHelper_0001 (unsupported)");
    return nullptr;
}
VRL_EXPORT IDeckLinkCocoaScreenPreviewCallback* CreateCocoaScreenPreview_0001(void*) {
    VLOGI("CreateCocoaScreenPreview_0001 (unsupported)");
    return nullptr;
}

__attribute__((constructor)) static void vrlinkLoaded() { VLOGI("VR Link DeckLinkAPI loaded into pid %d", getpid()); }
