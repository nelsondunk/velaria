#include "Config.h"

#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <unistd.h>

#include "Common.h"

namespace vrl {

namespace {
std::string cfToString(CFTypeRef v) {
    if (!v || CFGetTypeID(v) != CFStringGetTypeID()) return {};
    char buf[512];
    if (!CFStringGetCString((CFStringRef)v, buf, sizeof buf, kCFStringEncodingUTF8)) return {};
    return buf;
}
bool cfToInt(CFTypeRef v, int& out) {
    if (!v) return false;
    if (CFGetTypeID(v) == CFNumberGetTypeID()) return CFNumberGetValue((CFNumberRef)v, kCFNumberIntType, &out);
    if (CFGetTypeID(v) == CFBooleanGetTypeID()) {
        out = CFBooleanGetValue((CFBooleanRef)v) ? 1 : 0;
        return true;
    }
    return false;
}

Config load() {
    Config c;
    const char* home = getenv("HOME");
    std::string path = std::string(home ? home : "/tmp") + "/Library/Application Support/Velaria/config.plist";
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8*)path.c_str(), (CFIndex)path.size(), false);
    CFReadStreamRef rs = url ? CFReadStreamCreateWithFile(nullptr, url) : nullptr;
    if (rs && CFReadStreamOpen(rs)) {
        CFPropertyListRef pl = CFPropertyListCreateWithStream(nullptr, rs, 0, kCFPropertyListImmutable, nullptr, nullptr);
        if (pl && CFGetTypeID(pl) == CFDictionaryGetTypeID()) {
            CFDictionaryRef d = (CFDictionaryRef)pl;
            auto str = [&](const char* k, std::string& dst) {
                CFStringRef key = CFStringCreateWithCString(nullptr, k, kCFStringEncodingUTF8);
                std::string s = cfToString(CFDictionaryGetValue(d, key));
                if (!s.empty()) dst = s;
                CFRelease(key);
            };
            auto num = [&](const char* k, int& dst) {
                CFStringRef key = CFStringCreateWithCString(nullptr, k, kCFStringEncodingUTF8);
                cfToInt(CFDictionaryGetValue(d, key), dst);
                CFRelease(key);
            };
            str("Layout", c.layout);
            str("Projection", c.projection);
            str("Crop", c.crop);
            str("Name", c.name);
            num("MaxEncodeWidth", c.maxEncodeWidth);
            num("MaxEncodePixels", c.maxEncodePixels);
            num("BitrateMbps", c.bitrateMbps);
            num("KeyframeIntervalSec", c.keyframeIntervalSec);
            num("Port", c.port);
            int t = 0;
            num("Trace", t);
            c.trace = t != 0;
            int s = 0;
            num("StreamWhenNoClient", s);
            c.streamWhenNoClient = s != 0;
        }
        if (pl) CFRelease(pl);
        CFReadStreamClose(rs);
    }
    if (rs) CFRelease(rs);
    if (url) CFRelease(url);
    // Environment overrides (used by the test tools so they don't collide with a running Resolve).
    if (const char* e = getenv("VRLINK_PORT")) c.port = atoi(e);
    if (const char* e = getenv("VRLINK_TRACE")) c.trace = atoi(e) != 0;
    if (const char* e = getenv("VRLINK_LAYOUT")) c.layout = e;
    if (const char* e = getenv("VRLINK_BITRATE")) c.bitrateMbps = atoi(e);
    if (c.name.empty()) {
        CFStringRef cn = SCDynamicStoreCopyComputerName(nullptr, nullptr);
        c.name = cfToString(cn);
        if (cn) CFRelease(cn);
        if (c.name.empty()) c.name = "Resolve";
    }
    if (c.maxEncodeWidth < 640) c.maxEncodeWidth = 640;
    if (c.keyframeIntervalSec < 1) c.keyframeIntervalSec = 1;
    VLOGI("config: layout=%s projection=%s crop=%s maxW=%d bitrate=%d port=%d name=%s trace=%d", c.layout.c_str(),
          c.projection.c_str(), c.crop.c_str(), c.maxEncodeWidth, c.bitrateMbps, c.port, c.name.c_str(), c.trace);
    return c;
}
}  // namespace

const Config& Config::get() {
    static Config c = load();
    return c;
}

}  // namespace vrl
