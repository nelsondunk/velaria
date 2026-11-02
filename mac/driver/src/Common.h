// VR Link virtual DeckLink driver — shared helpers.
// Copyright (c) 2026 Skylight Visual Media Ltd. Internal tool.
#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "DeckLinkAPI.h"
#include "DeckLinkAPIVersion.h"

#define VRL_EXPORT extern "C" __attribute__((visibility("default")))

namespace vrl {

// ---------- logging ----------
enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };
void log(LogLevel lvl, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
#define VLOGD(...) ::vrl::log(::vrl::LogLevel::Debug, __VA_ARGS__)
#define VLOGI(...) ::vrl::log(::vrl::LogLevel::Info, __VA_ARGS__)
#define VLOGW(...) ::vrl::log(::vrl::LogLevel::Warn, __VA_ARGS__)
#define VLOGE(...) ::vrl::log(::vrl::LogLevel::Error, __VA_ARGS__)
bool traceEnabled();

// ---------- time ----------
int64_t nowNs();                       // host monotonic clock (mach_absolute_time) in ns

// ---------- COM helpers ----------
inline bool iidEq(const REFIID& a, const REFIID& b) { return std::memcmp(&a, &b, sizeof(CFUUIDBytes)) == 0; }
inline bool isIUnknown(const REFIID& iid) {
    CFUUIDBytes unk = CFUUIDGetUUIDBytes(IUnknownUUID);
    return iidEq(iid, unk);
}
std::string iidString(const REFIID& iid);
const char* iidName(const REFIID& iid);   // friendly name if known, else "?"
std::string fourcc(uint32_t v);
CFStringRef makeCFString(const std::string& s);   // +1 retained; caller (DeckLink client) releases

// Shared refcount implementation for multiply-inherited COM classes.
class RefCounted {
public:
    ULONG addRefImpl() { return ++m_refs; }
    ULONG releaseImpl() {
        ULONG r = --m_refs;
        if (r == 0) destroy();
        return r;
    }
protected:
    virtual ~RefCounted() = default;
    virtual void destroy() { delete this; }
    std::atomic<ULONG> m_refs{1};
};

#define VRL_REFCOUNT_METHODS                                        \
    ULONG AddRef() override { return addRefImpl(); }                \
    ULONG Release() override { return releaseImpl(); }

}  // namespace vrl
