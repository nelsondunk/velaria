// Host test for spec item 4 (the strip says a problem only while there is one): drives stripWorry() and DropMonitor
// (src/hud.cpp) with injected numbers and checks the done-when: nothing on a clean session, each condition raises
// its pill within 2 s, each clears within 5 s of recovery.
//   g++ -std=c++17 -O2 -I../common -Isrc tools/striptest.cpp src/hud.cpp src/panels.cpp src/edit.cpp -lpthread -o /tmp/striptest && /tmp/striptest
#include <cctype>
#include <cstdio>
#include <string>

#include "hud.h"

using namespace vrq;
static int fails = 0;
static void check(bool ok, const std::string& what) {
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) fails++;
}
static bool hasDigit(const std::string& s) {
    for (char c : s) if (isdigit((unsigned char)c)) return true;
    return false;
}

int main() {
    const int64_t S = 1000000000LL;
    {
        HudInfo in;
        in.connected = true; in.hasVideo = true; in.usb = true; in.delayMs = 120; in.renderMs = 400;
        check(stripWorry(in).empty(), "clean session (120 ms delay, no drops, one insert) -> no pill");
        in.delayMs = 250;
        check(stripWorry(in).empty(), "delay exactly 250 ms -> no pill");
        in.delayMs = 251;
        check(stripWorry(in) == "Delay 251 ms", "delay 251 ms -> 'Delay 251 ms'");
        in.hasVideo = false;
        check(stripWorry(in).empty(), "delay over 250 but no picture arriving -> no pill (delay means nothing yet)");
        in.hasVideo = true; in.delayMs = 120;
        in.audioAmbiguous = true;
        check(stripWorry(in).rfind("Two Velaria Sound inserts", 0) == 0 && !hasDigit(stripWorry(in)), "two inserts -> the remove-one pill, no numbers");
        in.audioAmbiguous = false;
        check(stripWorry(in).empty(), "ambiguity resolved -> pill gone on the next frame");
        in.warning = "Dropping frames";
        in.audioAmbiguous = true; in.delayMs = 400;
        check(stripWorry(in) == "Dropping frames", "drops outrank inserts and delay (one pill at a time, worst first)");
    }
    {
        // frame drops: 72 Hz calls, skips injected, Wi-Fi
        DropMonitor dm;
        uint64_t skipped = 0;
        int64_t t = 0, raisedAt = -1, clearedAt = -1;
        const int64_t frame = S / 72;
        // 4 s clean, then drops at 3/s for 4 s, then clean
        for (; t < 4 * S; t += frame) dm.update(skipped, true, false, t);
        check(dm.update(skipped, true, false, t).empty(), "4 s of clean playback -> no warning");
        int64_t dropStart = t;
        int n = 0;
        for (; t < dropStart + 4 * S; t += frame, n++) {
            if (n % 24 == 0) skipped++;   // 3 per second
            const std::string& w = dm.update(skipped, true, false, t);
            if (!w.empty() && raisedAt < 0) raisedAt = t;
        }
        char buf[160];
        snprintf(buf, sizeof buf, "drops at 3/s on Wi-Fi -> warning raised after %.2f s (<= 2 s), says 'try the USB cable'", raisedAt < 0 ? -1.0 : (raisedAt - dropStart) / 1e9);
        check(raisedAt >= 0 && raisedAt - dropStart <= 2 * S && dm.warning.find("USB cable") != std::string::npos, buf);
        int64_t dropEnd = t;
        for (; t < dropEnd + 6 * S; t += frame) {
            const std::string& w = dm.update(skipped, true, false, t);
            if (w.empty() && clearedAt < 0) clearedAt = t;
        }
        snprintf(buf, sizeof buf, "drops stop -> warning cleared after %.2f s (<= 5 s)", clearedAt < 0 ? -1.0 : (clearedAt - dropEnd) / 1e9);
        check(clearedAt >= 0 && clearedAt - dropEnd <= 5 * S, buf);
    }
    {
        // the same drops while paused are a seek, not a problem; on USB the wording drops the cable advice
        DropMonitor dm;
        uint64_t skipped = 0;
        int64_t t = 0;
        const int64_t frame = S / 72;
        bool any = false;
        for (int n = 0; t < 6 * S; t += frame, n++) {
            if (n % 24 == 0) skipped++;
            if (!dm.update(skipped, false, false, t).empty()) any = true;
        }
        check(!any, "drops while not playing -> no warning");
        DropMonitor dm2;
        skipped = 0; t = 0;
        for (int n = 0; t < 6 * S; t += frame, n++) {
            if (n % 24 == 0) skipped++;
            dm2.update(skipped, true, true, t);
        }
        check(dm2.warning == "Dropping frames", "drops on USB -> 'Dropping frames' without the cable advice");
        // a single skipped frame in a window (a cut, a seek) is not a problem
        DropMonitor dm3;
        skipped = 0; t = 0; any = false;
        for (int n = 0; t < 6 * S; t += frame, n++) {
            if (n % 144 == 0) skipped++;   // one every 2 s
            if (!dm3.update(skipped, true, false, t).empty()) any = true;
        }
        check(!any, "one skipped frame per 2 s -> no warning");
    }
    printf("%s\n", fails ? "FAILURES" : "all passed");
    return fails ? 1 : 0;
}
