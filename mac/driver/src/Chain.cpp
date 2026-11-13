// Device enumeration objects: Velaria's own device plus, when Blackmagic's framework is loaded underneath, its
// devices. UNTESTED on macOS (logic exercised on Linux by tests/chain_test.cpp against a fake Blackmagic framework).
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "Coexist.h"

namespace vrl {
namespace coexist {

namespace {

thread_local int t_depth = 0;

int64_t velariaApiVersion() {
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
        *v = velariaApiVersion();
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
        int64_t ver = velariaApiVersion();
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
    Iterator(VelariaDeviceFn velaria, IDeckLinkIterator* under) : m_velaria(velaria), m_under(under) {}

    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (isIUnknown(iid) || iidEq(iid, IID_IDeckLinkIterator)) {
            *ppv = static_cast<IDeckLinkIterator*>(this);
            AddRef();
            return S_OK;
        }
        if (iidEq(iid, IID_IDeckLinkAPIInformation)) {   // SDK idiom for querying the driver version
            // Ask Blackmagic's iterator first: with a real card present, the client should read the real version.
            if (m_under && !getenv("VRLINK_API_VERSION")) {
                if (m_under->QueryInterface(iid, ppv) == S_OK && *ppv) return S_OK;
            }
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
        *out = nullptr;
        if (!m_velariaDone) {
            m_velariaDone = true;
            IDeckLink* d = m_velaria();
            d->AddRef();
            *out = d;
            VLOGI("Iterator Next -> Velaria device");
            return S_OK;
        }
        if (!m_under) return S_FALSE;
        HRESULT hr = m_under->Next(out);
        if (hr == S_OK && *out) {
            VLOGI("Iterator Next -> Blackmagic device #%d: %s", ++m_underCount, deviceLabel(*out).c_str());
            return S_OK;
        }
        // S_FALSE is the normal end of the list. Anything else is Blackmagic's error: log it, and end the list
        // cleanly so the client still keeps the Velaria device it already has.
        if (hr != S_FALSE) VLOGW("Iterator: Blackmagic's Next returned 0x%08x after %d device(s)", (unsigned)hr, m_underCount);
        else VLOGI("Iterator: end of list (Velaria + %d Blackmagic device(s))", m_underCount);
        *out = nullptr;
        return S_FALSE;
    }

protected:
    ~Iterator() override {
        if (m_under) m_under->Release();
    }

private:
    VelariaDeviceFn m_velaria;
    IDeckLinkIterator* m_under;   // +1, or nullptr when Blackmagic's framework is not loaded
    bool m_velariaDone = false;
    int m_underCount = 0;
};

class Discovery final : public IDeckLinkDiscovery, public RefCounted {
public:
    Discovery(VelariaDeviceFn velaria, IDeckLinkDiscovery* under) : m_velaria(velaria), m_under(under) {}

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
        VLOGI("Discovery InstallDeviceNotifications -> announcing Velaria device");
        cb->DeckLinkDeviceArrived(m_velaria());
        if (m_under) {
            // The client's own callback goes straight to Blackmagic's discovery: their arrivals and removals reach
            // the client on their thread, exactly as without Velaria. A failure here must not take Velaria down.
            // (No Scope here: their Install may call the client's callback before it returns, and the client is
            // free to ask us for other objects from inside that callback.)
            HRESULT hr = m_under->InstallDeviceNotifications(cb);
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_underInstalled = (hr == S_OK);
            }
            if (hr == S_OK) VLOGI("Discovery: callback installed on Blackmagic's discovery");
            else VLOGW("Discovery: Blackmagic's InstallDeviceNotifications returned 0x%08x (Velaria device only)", (unsigned)hr);
        }
        return S_OK;
    }

    HRESULT UninstallDeviceNotifications() override {
        IDeckLinkDeviceNotificationCallback* cb = nullptr;
        bool under = false;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            cb = m_cb;
            m_cb = nullptr;
            under = m_underInstalled;
            m_underInstalled = false;
        }
        // Blackmagic's side first, and outside our lock: their uninstall may wait for a callback that is running,
        // and that callback is the client's code, which may call back into this object.
        if (under) {
            HRESULT hr = m_under->UninstallDeviceNotifications();
            if (hr != S_OK) VLOGW("Discovery: Blackmagic's UninstallDeviceNotifications returned 0x%08x", (unsigned)hr);
        }
        if (cb) cb->Release();
        VLOGI("Discovery UninstallDeviceNotifications");
        return S_OK;
    }

protected:
    ~Discovery() override {
        UninstallDeviceNotifications();
        if (m_under) m_under->Release();
    }

private:
    VelariaDeviceFn m_velaria;
    IDeckLinkDiscovery* m_under;   // +1, or nullptr
    std::mutex m_mutex;
    IDeckLinkDeviceNotificationCallback* m_cb = nullptr;
    bool m_underInstalled = false;
};

}  // namespace

Scope::Scope() { ++t_depth; }
Scope::~Scope() { --t_depth; }
bool Scope::entered() { return t_depth > 0; }

IDeckLinkIterator* createIterator(VelariaDeviceFn velaria, const char* symbol) {
    if (Scope::entered()) {
        VLOGE("%s requested from inside a call into Blackmagic's framework: refusing (would recurse)", symbol);
        return nullptr;
    }
    IDeckLinkIterator* under = createFromUnderlying<IDeckLinkIterator>(symbol);
    VLOGI("%s -> Velaria%s", symbol, under ? " + Blackmagic" : " only");
    return new Iterator(velaria, under);
}

IDeckLinkDiscovery* createDiscovery(VelariaDeviceFn velaria, const char* symbol) {
    if (Scope::entered()) {
        VLOGE("%s requested from inside a call into Blackmagic's framework: refusing (would recurse)", symbol);
        return nullptr;
    }
    IDeckLinkDiscovery* under = createFromUnderlying<IDeckLinkDiscovery>(symbol);
    VLOGI("%s -> Velaria%s", symbol, under ? " + Blackmagic" : " only");
    return new Discovery(velaria, under);
}

IDeckLinkAPIInformation* createAPIInformation() {
    static const char* kSym = "CreateDeckLinkAPIInformationInstance_0001";
    if (Scope::entered()) {
        VLOGE("%s requested from inside a call into Blackmagic's framework: refusing (would recurse)", kSym);
        return nullptr;
    }
    if (!getenv("VRLINK_API_VERSION")) {
        if (IDeckLinkAPIInformation* under = createFromUnderlying<IDeckLinkAPIInformation>(kSym)) {
            VLOGI("%s -> Blackmagic's", kSym);
            return under;
        }
    }
    VLOGI("%s -> Velaria's", kSym);
    return new APIInformation();
}

}  // namespace coexist
}  // namespace vrl
