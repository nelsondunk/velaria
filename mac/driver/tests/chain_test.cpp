// Unit test for src/Chain.cpp: the layer that merges Velaria's device with Blackmagic's devices.
// A fake "Blackmagic framework" stands in for the real one, so this runs anywhere, including Linux:
//   tests/chain_test.sh
// It proves the merge logic and the reference counting. It proves nothing about Blackmagic's real framework,
// CFBundle loading, Resolve or macOS: those need the test plan in docs/decklink-coexistence-test-plan.md.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "Coexist.h"

using namespace vrl;

static int g_fail = 0, g_ok = 0;
#define CHECK(cond, what)                                   \
    do {                                                    \
        if (cond) { g_ok++; }                               \
        else { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what); } \
    } while (0)

// ---- the few helpers Chain.cpp expects from the rest of the driver ----
namespace vrl {
void log(LogLevel, const char* fmt, ...) {
    if (!getenv("CHAIN_TEST_VERBOSE")) return;
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); printf("\n");
}
bool traceEnabled() { return false; }
std::string iidString(const REFIID&) { return "iid"; }
const char* iidName(const REFIID&) { return "?"; }
std::string fourcc(uint32_t) { return "fcc"; }
CFStringRef makeCFString(const std::string&) { return nullptr; }
}  // namespace vrl

// ---- fakes ----
struct Counted {
    int refs = 1;
    bool dead = false;
};
struct FakeDevice : IDeckLink, Counted {
    HRESULT QueryInterface(REFIID, LPVOID* p) override { *p = nullptr; return E_NOINTERFACE; }
    ULONG AddRef() override { return ++refs; }
    ULONG Release() override { if (--refs == 0) dead = true; return refs; }
    HRESULT GetModelName(CFStringRef*) override { return S_OK; }
    HRESULT GetDisplayName(CFStringRef*) override { return S_OK; }
};
struct FakeInfo : IDeckLinkAPIInformation, Counted {
    HRESULT QueryInterface(REFIID, LPVOID* p) override { *p = nullptr; return E_NOINTERFACE; }
    ULONG AddRef() override { return ++refs; }
    ULONG Release() override { if (--refs == 0) dead = true; return refs; }
    HRESULT GetFlag(BMDDeckLinkAPIInformationID, bool*) override { return E_INVALIDARG; }
    HRESULT GetInt(BMDDeckLinkAPIInformationID, int64_t* v) override { *v = 0x10050000; return S_OK; }
    HRESULT GetFloat(BMDDeckLinkAPIInformationID, double*) override { return E_INVALIDARG; }
    HRESULT GetString(BMDDeckLinkAPIInformationID, CFStringRef*) override { return E_INVALIDARG; }
};
static FakeInfo g_bmInfo;
struct FakeIterator : IDeckLinkIterator, Counted {
    std::vector<FakeDevice*> devs;
    size_t pos = 0;
    HRESULT failAfter = S_FALSE;   // what Next returns when the list is exhausted
    HRESULT QueryInterface(REFIID iid, LPVOID* p) override {
        if (iidEq(iid, IID_IDeckLinkAPIInformation)) { g_bmInfo.AddRef(); *p = static_cast<IDeckLinkAPIInformation*>(&g_bmInfo); return S_OK; }
        *p = nullptr; return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++refs; }
    ULONG Release() override { if (--refs == 0) dead = true; return refs; }
    HRESULT Next(IDeckLink** out) override {
        if (pos >= devs.size()) { *out = (IDeckLink*)0x1; return failAfter; }   // junk on purpose: Chain must clear it
        devs[pos]->AddRef(); *out = devs[pos++]; return S_OK;
    }
};
static std::vector<std::string> g_events;
struct FakeDiscovery : IDeckLinkDiscovery, Counted {
    IDeckLinkDeviceNotificationCallback* cb = nullptr;
    HRESULT installResult = S_OK;
    FakeDevice* announce = nullptr;
    HRESULT QueryInterface(REFIID, LPVOID* p) override { *p = nullptr; return E_NOINTERFACE; }
    ULONG AddRef() override { return ++refs; }
    ULONG Release() override { if (--refs == 0) dead = true; return refs; }
    HRESULT InstallDeviceNotifications(IDeckLinkDeviceNotificationCallback* c) override {
        g_events.push_back("bm-install");
        if (installResult != S_OK) return installResult;
        cb = c; c->AddRef();
        if (announce) c->DeckLinkDeviceArrived(announce);   // synchronously, as a real driver may
        return S_OK;
    }
    HRESULT UninstallDeviceNotifications() override {
        g_events.push_back("bm-uninstall");
        if (cb) { cb->Release(); cb = nullptr; }
        return S_OK;
    }
};

static FakeDevice g_velaria;
static IDeckLink* velariaDevice() { return &g_velaria; }

// The fake framework: a table of exported creators, as underlyingSymbol() would find them.
static std::map<std::string, void*> g_exports;
static std::vector<std::string> g_lookups;
static FakeIterator* g_nextIterator = nullptr;
static FakeDiscovery* g_nextDiscovery = nullptr;
static IDeckLinkIterator* g_nestedResult = (IDeckLinkIterator*)0x1;
static IDeckLinkIterator* bmCreateIterator() { return g_nextIterator; }
static IDeckLinkDiscovery* bmCreateDiscovery() { return g_nextDiscovery; }
static IDeckLinkAPIInformation* bmCreateInfo() { g_bmInfo.AddRef(); return &g_bmInfo; }
// A Blackmagic creator that asks /Library/Frameworks/DeckLinkAPI.framework (us) for an iterator: the loop to break.
static IDeckLinkIterator* bmCreateIteratorReentrant() {
    g_nestedResult = coexist::createIterator(velariaDevice, "CreateDeckLinkIteratorInstance_0003");
    return g_nextIterator;
}
namespace vrl { namespace coexist {
void* underlyingSymbol(const char* name) {
    g_lookups.push_back(name);
    auto it = g_exports.find(name);
    return it == g_exports.end() ? nullptr : it->second;
}
std::string deviceLabel(IDeckLink*) { return "fake"; }
}}

struct ClientCallback : IDeckLinkDeviceNotificationCallback, Counted {
    std::vector<IDeckLink*> arrived;
    bool askForIteratorInside = false;
    IDeckLinkIterator* insideResult = nullptr;
    HRESULT QueryInterface(REFIID, LPVOID* p) override { *p = nullptr; return E_NOINTERFACE; }
    ULONG AddRef() override { return ++refs; }
    ULONG Release() override { if (--refs == 0) dead = true; return refs; }
    HRESULT DeckLinkDeviceArrived(IDeckLink* d) override {
        arrived.push_back(d);
        if (askForIteratorInside && d != &g_velaria)
            insideResult = coexist::createIterator(velariaDevice, "CreateDeckLinkIteratorInstance_0003");
        return S_OK;
    }
    HRESULT DeckLinkDeviceRemoved(IDeckLink*) override { return S_OK; }
};

int main() {
    IDeckLink* d = nullptr;

    // 1. Nothing set aside: exactly the old behaviour.
    {
        g_exports.clear(); g_velaria.refs = 1;
        IDeckLinkIterator* it = coexist::createIterator(velariaDevice, "CreateDeckLinkIteratorInstance_0003");
        CHECK(it != nullptr, "iterator without Blackmagic");
        CHECK(it->Next(&d) == S_OK && d == &g_velaria, "first device is Velaria");
        CHECK(g_velaria.refs == 2, "Velaria device retained once for the client");
        CHECK(it->Next(&d) == S_FALSE && d == nullptr, "then the list ends with a null device");
        CHECK(it->Next(&d) == S_FALSE, "and stays ended");
        CHECK(it->Next(nullptr) == E_POINTER, "null out pointer refused");
        CHECK(it->Release() == 0, "iterator freed by its one Release");
        IDeckLinkAPIInformation* info = coexist::createAPIInformation();
        int64_t v = 0;
        CHECK(info && info->GetInt(BMDDeckLinkAPIVersion, &v) == S_OK && v == BLACKMAGIC_DECKLINK_API_VERSION, "Velaria's own API version");
        info->Release();
    }

    // 2. Blackmagic underneath with two devices.
    {
        g_exports.clear(); g_lookups.clear(); g_velaria.refs = 1;
        FakeDevice b1, b2; FakeIterator bm; bm.devs = {&b1, &b2};
        g_nextIterator = &bm;
        g_exports["CreateDeckLinkIteratorInstance_0003"] = (void*)bmCreateIterator;
        IDeckLinkIterator* it = coexist::createIterator(velariaDevice, "CreateDeckLinkIteratorInstance_0003");
        CHECK(g_lookups.size() == 1 && g_lookups[0] == "CreateDeckLinkIteratorInstance_0003", "asks Blackmagic for the same suffixed creator");
        CHECK(it->Next(&d) == S_OK && d == &g_velaria, "Velaria first");
        CHECK(it->Next(&d) == S_OK && d == &b1, "then Blackmagic's first device, untouched pointer");
        CHECK(it->Next(&d) == S_OK && d == &b2, "then its second");
        CHECK(b1.refs == 2 && b2.refs == 2, "Blackmagic devices carry exactly Blackmagic's own +1");
        CHECK(it->Next(&d) == S_FALSE && d == nullptr, "end of list, out cleared even if Blackmagic left junk in it");
        LPVOID p = nullptr;
        CHECK(it->QueryInterface(IID_IDeckLinkAPIInformation, &p) == S_OK && p == static_cast<IDeckLinkAPIInformation*>(&g_bmInfo), "version query answered by Blackmagic");
        g_bmInfo.Release();
        CHECK(bm.refs == 1 && !bm.dead, "Blackmagic's iterator alive while ours is");
        CHECK(it->Release() == 0 && bm.dead, "releasing ours releases Blackmagic's");
    }

    // 3. A different suffix that Blackmagic does not export: Velaria only, no crash.
    {
        g_lookups.clear(); g_velaria.refs = 1;
        IDeckLinkIterator* it = coexist::createIterator(velariaDevice, "CreateDeckLinkIteratorInstance_0004");
        CHECK(g_lookups.size() == 1 && g_lookups[0] == "CreateDeckLinkIteratorInstance_0004", "no fallback to another suffix");
        CHECK(it->Next(&d) == S_OK && it->Next(&d) == S_FALSE, "Velaria only when the export is missing");
        it->Release();
    }

    // 4. Blackmagic's Next fails: the client keeps Velaria and gets a clean end of list.
    {
        g_velaria.refs = 1;
        FakeIterator bm; bm.failAfter = E_FAIL; g_nextIterator = &bm;
        IDeckLinkIterator* it = coexist::createIterator(velariaDevice, "CreateDeckLinkIteratorInstance_0003");
        CHECK(it->Next(&d) == S_OK && d == &g_velaria, "Velaria still listed");
        CHECK(it->Next(&d) == S_FALSE && d == nullptr, "Blackmagic's error becomes end of list");
        it->Release();
    }

    // 5. Blackmagic's creator returns nullptr (driver not running).
    {
        g_velaria.refs = 1; g_nextIterator = nullptr;
        IDeckLinkIterator* it = coexist::createIterator(velariaDevice, "CreateDeckLinkIteratorInstance_0003");
        CHECK(it && it->Next(&d) == S_OK && it->Next(&d) == S_FALSE, "null Blackmagic iterator: Velaria only");
        it->Release();
    }

    // 6. Discovery with Blackmagic underneath.
    {
        g_events.clear(); g_velaria.refs = 1;
        FakeDevice b1; FakeDiscovery bm; bm.announce = &b1; g_nextDiscovery = &bm;
        g_exports["CreateDeckLinkDiscoveryInstance_0002"] = (void*)bmCreateDiscovery;
        ClientCallback cb; cb.askForIteratorInside = true;
        FakeIterator inner; g_nextIterator = &inner;
        IDeckLinkDiscovery* disc = coexist::createDiscovery(velariaDevice, "CreateDeckLinkDiscoveryInstance_0002");
        CHECK(disc->InstallDeviceNotifications(nullptr) == E_INVALIDARG, "null callback refused");
        CHECK(disc->InstallDeviceNotifications(&cb) == S_OK, "install");
        CHECK(cb.arrived.size() == 2 && cb.arrived[0] == &g_velaria && cb.arrived[1] == &b1, "Velaria announced first, then Blackmagic's device");
        CHECK(bm.cb == &cb, "Blackmagic holds the client's own callback, not a wrapper");
        CHECK(cb.insideResult != nullptr, "a client may ask for an iterator from inside Blackmagic's callback");
        if (cb.insideResult) cb.insideResult->Release();
        CHECK(disc->InstallDeviceNotifications(&cb) == E_FAIL, "second install refused");
        CHECK(cb.refs == 3, "callback held once by us and once by Blackmagic");
        CHECK(disc->UninstallDeviceNotifications() == S_OK, "uninstall");
        CHECK(g_events.size() == 2 && g_events[1] == "bm-uninstall", "Blackmagic's uninstall called once");
        CHECK(cb.refs == 1, "callback references balanced");
        CHECK(disc->UninstallDeviceNotifications() == S_OK && g_events.size() == 2, "second uninstall is harmless and does not reach Blackmagic");
        CHECK(disc->Release() == 0 && bm.dead, "releasing ours releases Blackmagic's discovery");
    }

    // 7. Discovery released without an uninstall: still cleans up on both sides.
    {
        g_events.clear();
        FakeDiscovery bm; g_nextDiscovery = &bm; ClientCallback cb;
        IDeckLinkDiscovery* disc = coexist::createDiscovery(velariaDevice, "CreateDeckLinkDiscoveryInstance_0002");
        disc->InstallDeviceNotifications(&cb);
        disc->Release();
        CHECK(cb.refs == 1 && bm.cb == nullptr && bm.dead, "destructor uninstalls from Blackmagic and drops the callback");
    }

    // 8. Blackmagic's install fails: Velaria still works; no uninstall is sent to Blackmagic.
    {
        g_events.clear();
        FakeDiscovery bm; bm.installResult = E_FAIL; g_nextDiscovery = &bm; ClientCallback cb;
        IDeckLinkDiscovery* disc = coexist::createDiscovery(velariaDevice, "CreateDeckLinkDiscoveryInstance_0002");
        CHECK(disc->InstallDeviceNotifications(&cb) == S_OK && cb.arrived.size() == 1, "Velaria announced although Blackmagic's install failed");
        disc->UninstallDeviceNotifications();
        CHECK(g_events.size() == 1 && g_events[0] == "bm-install", "no uninstall sent after a failed install");
        CHECK(cb.refs == 1, "callback balanced");
        disc->Release();
    }

    // 9. API information: Blackmagic's when present, Velaria's when forced.
    {
        g_exports["CreateDeckLinkAPIInformationInstance_0001"] = (void*)bmCreateInfo;
        IDeckLinkAPIInformation* info = coexist::createAPIInformation();
        CHECK(info == &g_bmInfo, "Blackmagic's API information handed over as is");
        info->Release();
        setenv("VRLINK_API_VERSION", "0x0b000000", 1);
        info = coexist::createAPIInformation();
        int64_t v = 0;
        CHECK(info != &g_bmInfo && info->GetInt(BMDDeckLinkAPIVersion, &v) == S_OK && v == 0x0b000000, "VRLINK_API_VERSION forces Velaria's answer");
        info->Release();
        unsetenv("VRLINK_API_VERSION");
    }

    // 10. A Blackmagic creator that calls back into us: refused once, no endless recursion.
    {
        g_velaria.refs = 1;
        FakeIterator bm; g_nextIterator = &bm; g_nestedResult = (IDeckLinkIterator*)0x1;
        g_exports["CreateDeckLinkIteratorInstance_0003"] = (void*)bmCreateIteratorReentrant;
        IDeckLinkIterator* it = coexist::createIterator(velariaDevice, "CreateDeckLinkIteratorInstance_0003");
        CHECK(g_nestedResult == nullptr, "nested request from inside Blackmagic's creator is refused");
        CHECK(it && it->Next(&d) == S_OK && d == &g_velaria, "the outer request still works");
        CHECK(!coexist::Scope::entered(), "guard released afterwards");
        it->Release();
    }

    printf("%d checks passed, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
