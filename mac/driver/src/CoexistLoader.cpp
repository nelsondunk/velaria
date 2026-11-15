// Loads Blackmagic's own DeckLinkAPI.framework from where `install.sh --coexist` set it aside, and answers symbol
// lookups in it. macOS only. UNTESTED: written on Linux, never compiled or run (13 Nov 2026).
//
// Everything this file learns goes to ~/Library/Logs/Velaria/driver.log on lines starting "coexist:", so the first
// test on a real Mac can be read from one `grep coexist: driver.log`.
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#include "Coexist.h"
#include "Config.h"

namespace vrl {
namespace coexist {

namespace {

std::string cfStr(CFTypeRef v) {
    if (!v || CFGetTypeID(v) != CFStringGetTypeID()) return "?";
    char buf[256];
    if (!CFStringGetCString((CFStringRef)v, buf, sizeof buf, kCFStringEncodingUTF8)) return "?";
    return buf;
}

// The creator functions a DeckLink client can ask for, as far as SDK 10.11 to 12.8 name them. Logged once at load
// so a release of Desktop Video that drops or adds one shows up in the log, not as a silent missing device.
const char* const kKnownCreators[] = {
    "CreateDeckLinkIteratorInstance_0004", "CreateDeckLinkIteratorInstance_0003", "CreateDeckLinkIteratorInstance_0002",
    "CreateDeckLinkDiscoveryInstance_0003", "CreateDeckLinkDiscoveryInstance_0002", "CreateDeckLinkDiscoveryInstance_0001",
    "CreateDeckLinkAPIInformationInstance_0001", "CreateVideoConversionInstance_0001",
    "CreateVideoFrameAncillaryPacketsInstance_0001", "CreateOpenGLScreenPreviewHelper_0001",
    "CreateOpenGL3ScreenPreviewHelper_0001", "CreateCocoaScreenPreview_0001",
};

struct State {
    std::once_flag once;
    CFBundleRef bundle = nullptr;   // retained for the life of the process; never unloaded
    std::mutex mutex;
    std::map<std::string, void*> cache;
};
State& state() {
    static State* s = new State();   // leaked on purpose: lookups can happen during process exit
    return *s;
}

void* lookup(CFBundleRef b, const char* name) {
    CFStringRef n = CFStringCreateWithCString(kCFAllocatorDefault, name, kCFStringEncodingUTF8);
    if (!n) return nullptr;
    void* p = CFBundleGetFunctionPointerForName(b, n);
    CFRelease(n);
    return p;
}

// One pass over Blackmagic's framework right after loading it: its API version and the devices it reports now.
// This is what tells a first test apart: "loaded, 0 devices" (no hardware, or driver not running) from
// "loaded, 1 device" from "did not load".
void census(CFBundleRef b) {
    // This runs inside the std::call_once that loads the framework. If anything Blackmagic's code does below (a
    // creator, Next, a name lookup) asked /Library/Frameworks/DeckLinkAPI.framework, which is us, for an object on
    // this thread, the request would re-enter that call_once and hang the host for good. Held for the whole census,
    // the Scope makes our exports refuse such a request before they reach the loader. No client code runs in here,
    // so the reason Next is left unscoped elsewhere (Coexist.h) does not apply.
    Scope whole;
    if (auto mk = reinterpret_cast<IDeckLinkAPIInformation* (*)()>(lookup(b, "CreateDeckLinkAPIInformationInstance_0001"))) {
        IDeckLinkAPIInformation* info;
        {
            Scope s;
            info = mk();
        }
        if (info) {
            int64_t v = 0;
            HRESULT hr = info->GetInt(BMDDeckLinkAPIVersion, &v);
            VLOGI("coexist: Blackmagic API version 0x%08llx (%d.%d.%d), hr=0x%08x; Velaria is built against %s",
                  (unsigned long long)v, (int)((v >> 24) & 0xff), (int)((v >> 16) & 0xff), (int)((v >> 8) & 0xff),
                  (unsigned)hr, BLACKMAGIC_DECKLINK_API_VERSION_STRING);
            info->Release();
        } else {
            VLOGW("coexist: Blackmagic's API information creator returned nullptr (Desktop Video driver not running?)");
        }
    }
    const char* itSym = "CreateDeckLinkIteratorInstance_0003";
    auto mkIt = reinterpret_cast<IDeckLinkIterator* (*)()>(lookup(b, itSym));
    if (!mkIt) {
        VLOGW("coexist: no %s in Blackmagic's framework; device census skipped", itSym);
        return;
    }
    IDeckLinkIterator* it;
    {
        Scope s;
        it = mkIt();
    }
    if (!it) {
        VLOGW("coexist: Blackmagic's iterator creator returned nullptr (Desktop Video driver not running?)");
        return;
    }
    int n = 0;
    IDeckLink* dev = nullptr;
    while (n < 64 && it->Next(&dev) == S_OK && dev) {
        VLOGI("coexist: Blackmagic device #%d at load: %s", ++n, deviceLabel(dev).c_str());
        dev->Release();
        dev = nullptr;
    }
    it->Release();
    VLOGI("coexist: Blackmagic's framework reports %d device(s) at load", n);
}

void load() {
    State& st = state();
    const char* host = getprogname();
    if (const char* e = getenv("VELARIA_COEXIST")) {
        if (atoi(e) == 0) {
            VLOGI("coexist: off (VELARIA_COEXIST=0); Velaria device only [host %s]", host ? host : "?");
            return;
        }
    }
    if (!Config::get().coexist) {
        VLOGI("coexist: off (Coexist=false in config.plist); Velaria device only [host %s]", host ? host : "?");
        return;
    }
    struct stat sb{};
    if (stat(kUnderlyingPath, &sb) != 0) {
        VLOGI("coexist: nothing set aside at %s; Velaria device only [host %s]", kUnderlyingPath, host ? host : "?");
        return;
    }
    // Refuse anything a non-root user could have put there or could change.
    if (sb.st_uid != 0 || (sb.st_mode & (S_IWGRP | S_IWOTH))) {
        VLOGE("coexist: %s is not root-owned or is group/world-writable (uid %d, mode %o); not loading it",
              kUnderlyingPath, (int)sb.st_uid, (unsigned)(sb.st_mode & 07777));
        return;
    }
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, (const UInt8*)kUnderlyingPath,
                                                           (CFIndex)strlen(kUnderlyingPath), true);
    CFBundleRef b = url ? CFBundleCreate(kCFAllocatorDefault, url) : nullptr;
    if (url) CFRelease(url);
    if (!b) {
        VLOGE("coexist: %s exists but CFBundleCreate failed; Velaria device only", kUnderlyingPath);
        return;
    }
    std::string ident = cfStr(CFBundleGetIdentifier(b));
    std::string shortVer = cfStr(CFBundleGetValueForInfoDictionaryKey(b, CFSTR("CFBundleShortVersionString")));
    std::string bundleVer = cfStr(CFBundleGetValueForInfoDictionaryKey(b, kCFBundleVersionKey));
    // The set-aside framework must be Blackmagic's, never a copy of this one: loading ourselves underneath would
    // forward every call back into this file.
    if (ident.find("skylightproductions") != std::string::npos || ident.find("velaria") != std::string::npos) {
        VLOGE("coexist: the framework at %s is Velaria's own (%s), not Blackmagic's; not loading it. "
              "Run: sudo ./install.sh --status", kUnderlyingPath, ident.c_str());
        CFRelease(b);
        return;
    }
    CFErrorRef err = nullptr;
    if (!CFBundleLoadExecutableAndReturnError(b, &err)) {
        CFStringRef d = err ? CFErrorCopyDescription(err) : nullptr;
        VLOGE("coexist: could not load Blackmagic's framework (%s %s): %s; Velaria device only", ident.c_str(),
              shortVer.c_str(), cfStr(d).c_str());
        if (d) CFRelease(d);
        if (err) CFRelease(err);
        CFRelease(b);
        return;
    }
    VLOGI("coexist: loaded Blackmagic's framework %s version %s (%s) from %s [host %s, pid %d]", ident.c_str(),
          shortVer.c_str(), bundleVer.c_str(), kUnderlyingPath, host ? host : "?", (int)getpid());
    std::string have, missing;
    for (const char* c : kKnownCreators) (lookup(b, c) ? have : missing).append(" ").append(c);
    VLOGI("coexist: Blackmagic exports:%s", have.empty() ? " (none of the known creators)" : have.c_str());
    if (!missing.empty()) VLOGW("coexist: Blackmagic does not export:%s", missing.c_str());
    census(b);
    st.bundle = b;   // published last: lookups made before this line (there are none) would see "not loaded"
}

}  // namespace

void* underlyingSymbol(const char* name) {
    State& st = state();
    std::call_once(st.once, load);
    if (!st.bundle) return nullptr;
    std::lock_guard<std::mutex> lk(st.mutex);
    auto it = st.cache.find(name);
    if (it != st.cache.end()) return it->second;
    void* p = lookup(st.bundle, name);
    st.cache[name] = p;
    if (!p) VLOGW("coexist: a client asked for %s, which Blackmagic's framework does not export", name);
    return p;
}

std::string deviceLabel(IDeckLink* dev) {
    if (!dev) return "(null)";
    CFStringRef model = nullptr, display = nullptr;
    dev->GetModelName(&model);
    dev->GetDisplayName(&display);
    std::string s = cfStr(model) + " / " + cfStr(display);
    if (model) CFRelease(model);
    if (display) CFRelease(display);
    return s;
}

}  // namespace coexist
}  // namespace vrl
