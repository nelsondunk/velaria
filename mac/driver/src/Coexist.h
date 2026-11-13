// Coexistence with Blackmagic Desktop Video. UNTESTED: written on Linux, never compiled for or run on macOS.
//
// When the installer's --coexist path has set Blackmagic's own DeckLinkAPI.framework aside (kUnderlyingPath), this
// framework loads it underneath and merges the two: client apps see "Velaria (Quest)" plus every device Blackmagic's
// framework reports, through the same entry points. When nothing is set aside, every function here answers "no" and
// the driver behaves exactly as it did before coexistence existed.
//
// Rules this layer keeps (see docs/decklink-coexistence.md):
//   - Blackmagic's objects are handed to the client untouched. Nothing of theirs is wrapped, so their interfaces,
//     reference counts and threads behave as if Velaria were not there.
//   - The only calls made into their framework are the suffixed creator functions, IUnknown, IDeckLinkIterator::Next,
//     IDeckLinkDiscovery::Install/UninstallDeviceNotifications, IDeckLinkAPIInformation::GetInt and
//     IDeckLink::GetModelName/GetDisplayName. All have been layout-stable across Desktop Video releases.
//   - Their framework is loaded once and never unloaded.
#pragma once
#include "Common.h"

namespace vrl {
namespace coexist {

// Fixed, root-owned location. Deliberately not configurable: a user-writable override would let any process on the
// Mac choose a library for Resolve to load.
constexpr const char* kUnderlyingPath = "/Library/Application Support/Velaria/Blackmagic/DeckLinkAPI.framework";

// ---- loader (CoexistLoader.cpp; macOS only) ----
// Address of an exported function in Blackmagic's framework, or nullptr when coexistence is off, nothing is set
// aside, the framework failed to load, or it has no such export. First use loads the framework (once per process).
void* underlyingSymbol(const char* name);
// "model / display name" of a device, for the log.
std::string deviceLabel(IDeckLink* dev);

// ---- re-entrancy guard ----
// If a Blackmagic component loaded underneath asks /Library/Frameworks/DeckLinkAPI.framework (that is, us) for an
// object while we are inside a call into Blackmagic's framework, forwarding it again could recurse without end.
// Every call into one of their creator functions runs inside a Scope; entered() tells the exports to refuse the
// nested request. Only creator calls are scoped: Next, Install and QueryInterface are not, because the client's own
// code can run inside those (a device-arrived callback) and may legitimately ask us for more objects.
class Scope {
public:
    Scope();
    ~Scope();
    static bool entered();
};

// ---- merged objects (Chain.cpp; portable) ----
using VelariaDeviceFn = IDeckLink* (*)();   // returns the Velaria device, +0 (not retained)

// Iterator that yields the Velaria device first, then whatever the creator named `symbol` in Blackmagic's
// framework yields. `symbol` must be the same suffixed name the client asked us for.
IDeckLinkIterator* createIterator(VelariaDeviceFn velaria, const char* symbol);
// Discovery that announces the Velaria device, then installs the client's callback on Blackmagic's discovery.
IDeckLinkDiscovery* createDiscovery(VelariaDeviceFn velaria, const char* symbol);
// Blackmagic's API information object when their framework is loaded (so a real card sees the Desktop Video version
// it would see without Velaria), otherwise Velaria's own. VRLINK_API_VERSION forces Velaria's own.
IDeckLinkAPIInformation* createAPIInformation();

// Calls a no-argument creator in Blackmagic's framework and returns its result, or nullptr.
template <class T>
T* createFromUnderlying(const char* symbol) {
    if (Scope::entered()) return nullptr;
    auto fn = reinterpret_cast<T* (*)()>(underlyingSymbol(symbol));
    if (!fn) return nullptr;
    Scope s;
    return fn();
}

}  // namespace coexist
}  // namespace vrl
