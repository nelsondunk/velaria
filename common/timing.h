// Timing core shared by the Quest receiver and the Mac test receiver.
//  - ClockSync: maps the sender (Mac) clock onto the receiver clock from NTP-style ping/pong samples,
//    tracking offset and drift so long sessions don't wander.
//  - FrameScheduler: given frames stamped with sender scan-out times, picks the frame for each display
//    refresh (at its predicted display time) and measures cadence: every frame should be shown for the same
//    whole number of refreshes.
//  - pickRefreshRate: chooses the headset refresh rate that is the closest whole multiple of the video rate.
#pragma once
#include <cstdint>
#include <deque>
#include <vector>

namespace vrltime {

class ClockSync {
public:
    // One exchange: t1 receiver send, t2 sender receive, t3 sender send, t4 receiver receive (all ns).
    void addSample(int64_t t1, int64_t t2, int64_t t3, int64_t t4);
    bool valid() const { return m_haveEstimate; }
    // offset = senderClock - receiverClock at receiver time `rxNs`
    int64_t offsetAt(int64_t rxNs) const;
    int64_t senderToReceiver(int64_t senderNs, int64_t rxNowNs) const { return senderNs - offsetAt(rxNowNs); }
    int64_t receiverToSender(int64_t rxNs) const { return rxNs + offsetAt(rxNs); }
    int64_t lastRttNs() const { return m_lastRtt; }
    int64_t minRttNs() const { return m_minRtt; }
    double driftPpm() const { return m_drift * 1e6; }
    void reset();

private:
    struct Sample {
        int64_t rx;        // receiver time (midpoint)
        int64_t offset;
        int64_t rtt;
    };
    void recompute();
    std::deque<Sample> m_window;     // recent raw samples (min-RTT filter)
    std::deque<Sample> m_filtered;   // one best sample per window, for drift fit
    int m_sinceFilter = 0;
    bool m_haveEstimate = false;
    int64_t m_baseRx = 0;
    double m_baseOffset = 0;
    double m_drift = 0;               // d(offset)/d(rx)
    int64_t m_lastRtt = 0;
    int64_t m_minRtt = INT64_MAX;
};

struct ScheduledItem {
    uint64_t seq = 0;
    int64_t dueNs = 0;          // receiver clock: when it should be on screen
    int64_t durationNs = 0;
    bool sync = false;          // paused/scrub frame: show as soon as available
};

struct CadenceStats {
    uint64_t framesShown = 0;
    uint64_t framesSkipped = 0;     // never shown (superseded before display)
    uint64_t cadenceErrors = 0;     // shown for != nominal refresh count during steady playback
    uint64_t holds = 0;             // refreshes where a due frame was not yet decoded
    std::vector<uint64_t> refreshHistogram = std::vector<uint64_t>(8, 0);   // refreshes per frame (1..7, 0 = other)
};

class FrameScheduler {
public:
    void setRefreshPeriod(int64_t ns) { m_refreshNs = ns; }
    int64_t refreshPeriod() const { return m_refreshNs; }
    // Candidate frames currently decoded and available (sorted by due time ascending, caller-maintained).
    // Returns index into `ready` of the frame to display for a refresh at `displayNs`, or -1 to keep current.
    // `pendingDueNs` = due time of the next frame not yet decoded (or INT64_MAX) — used to count holds.
    int pick(const std::vector<ScheduledItem>& ready, int64_t displayNs, int64_t pendingDueNs);
    // Call once per refresh after pick() with the frame actually on screen.
    void onRefresh(uint64_t seqOnScreen, int64_t frameDurationNs, bool steadyPlayback);
    // Finalize accounting for the frame currently on screen (end of playback / test).
    void finish();
    const CadenceStats& stats() const { return m_stats; }
    void resetStats() { m_stats = CadenceStats(); }
    int nominalRefreshesPerFrame(int64_t frameDurationNs) const;

private:
    int64_t m_refreshNs = 13888889;   // 72 Hz
    CadenceStats m_stats;
    uint64_t m_currentSeq = 0;
    bool m_haveCurrent = false;
    int m_currentCount = 0;
    int64_t m_currentDuration = 0;
    bool m_currentSteady = false;
    uint64_t m_lastShownSeq = 0;
};

// Choose a refresh rate from `available` (Hz) for video at fps. Prefers exact multiples; among equally good
// candidates prefers the one closest to 90 Hz. Returns the chosen rate (or the first entry if none).
float pickRefreshRate(const std::vector<float>& available, double fps, double* errorOut = nullptr);

// Rolling percentile helper (small windows).
class RollingStat {
public:
    explicit RollingStat(size_t cap = 256) : m_cap(cap) {}
    void add(double v);
    double percentile(double p) const;
    size_t count() const { return m_v.size(); }
    void clear() { m_v.clear(); }

private:
    size_t m_cap;
    std::deque<double> m_v;
};

}  // namespace vrltime
