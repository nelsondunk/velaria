#include "Common.h"

#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace vrl {

namespace {
std::mutex g_logMutex;
FILE* g_logFile = nullptr;
bool g_logInit = false;
bool g_trace = false;
int g_minLevel = 1;

std::string homeDir() {
    const char* h = getenv("HOME");
    return h ? std::string(h) : std::string("/tmp");
}

void initLocked() {
    if (g_logInit) return;
    g_logInit = true;
    std::string dir = homeDir() + "/Library/Logs/VRLink";
    mkdir((homeDir() + "/Library/Logs").c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    std::string path = dir + "/driver.log";
    struct stat st{};
    if (stat(path.c_str(), &st) == 0 && st.st_size > 20 * 1024 * 1024) {
        rename(path.c_str(), (dir + "/driver.old.log").c_str());
    }
    std::string traceFlag = homeDir() + "/Library/Application Support/VRLink/trace";
    g_trace = access(traceFlag.c_str(), F_OK) == 0 || getenv("VRLINK_DEBUGLOG") != nullptr;
    if (const char* lf = getenv("VRLINK_LOGFILE")) {
        g_logFile = fopen(lf, "a");
        g_minLevel = g_trace ? 0 : 1;
        return;
    }
    g_minLevel = g_trace ? 0 : 1;
    g_logFile = fopen(path.c_str(), "a");
}
}  // namespace

bool traceEnabled() {
    std::lock_guard<std::mutex> lk(g_logMutex);
    initLocked();
    return g_trace;
}

int64_t nowNs() {
    static mach_timebase_info_data_t tb = [] {
        mach_timebase_info_data_t t;
        mach_timebase_info(&t);
        return t;
    }();
    uint64_t t = mach_absolute_time();
    return (int64_t)((__uint128_t)t * tb.numer / tb.denom);
}

void log(LogLevel lvl, const char* fmt, ...) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    initLocked();
    if ((int)lvl < g_minLevel || !g_logFile) return;
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char ts[32];
    strftime(ts, sizeof ts, "%H:%M:%S", &tmv);
    static const char* L = "DIWE";
    uint64_t tid = 0;
    pthread_threadid_np(nullptr, &tid);
    fprintf(g_logFile, "%s.%03lld %c [%llx] %s\n", ts, (long long)((nowNs() / 1000000) % 1000), L[(int)lvl],
            (unsigned long long)tid, msg);
    fflush(g_logFile);
}

std::string iidString(const REFIID& iid) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&iid);
    char s[40];
    snprintf(s, sizeof s, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", b[0], b[1], b[2],
             b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return s;
}

std::string fourcc(uint32_t v) {
    char s[5] = {(char)(v >> 24), (char)(v >> 16), (char)(v >> 8), (char)v, 0};
    for (int i = 0; i < 4; i++)
        if (s[i] < 32 || s[i] > 126) {
            char n[16];
            snprintf(n, sizeof n, "0x%08X", v);
            return n;
        }
    return std::string("'") + s + "'";
}

CFStringRef makeCFString(const std::string& s) {
    return CFStringCreateWithCString(kCFAllocatorDefault, s.c_str(), kCFStringEncodingUTF8);
}

}  // namespace vrl
