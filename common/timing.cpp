#include "timing.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace vrltime {

// ------------------------------------------------------------------------------------------------ ClockSync

namespace {
constexpr size_t kRawWindow = 8;        // pick the min-RTT sample out of every 8 exchanges
constexpr size_t kFilteredMax = 90;     // ~3 min of history at 4 pings/s
constexpr int64_t kMinDriftSpanNs = 20LL * 1000000000LL;
constexpr double kMaxDrift = 300e-6;    // crystals are ±50 ppm; anything larger is noise
}  // namespace

void ClockSync::reset() {
    m_window.clear();
    m_filtered.clear();
    m_sinceFilter = 0;
    m_haveEstimate = false;
    m_drift = 0;
    m_minRtt = INT64_MAX;
    m_lastRtt = 0;
}

void ClockSync::addSample(int64_t t1, int64_t t2, int64_t t3, int64_t t4) {
    int64_t rtt = (t4 - t1) - (t3 - t2);
    if (rtt < 0 || t4 < t1) return;   // impossible; ignore
    Sample s;
    s.rx = t1 + (t4 - t1) / 2;
    s.offset = ((t2 - t1) + (t3 - t4)) / 2;
    s.rtt = rtt;
    m_lastRtt = rtt;
    m_minRtt = std::min(m_minRtt, rtt);
    m_window.push_back(s);
    if (m_window.size() > kRawWindow) m_window.pop_front();
    if (++m_sinceFilter >= (int)kRawWindow || m_filtered.empty()) {
        m_sinceFilter = 0;
        auto best = std::min_element(m_window.begin(), m_window.end(),
                                     [](const Sample& a, const Sample& b) { return a.rtt < b.rtt; });
        if (!m_filtered.empty() && m_filtered.size() == 1 && m_filtered.front().rx == best->rx) {
            // nothing new
        } else {
            m_filtered.push_back(*best);
        }
        if (m_filtered.size() > kFilteredMax) m_filtered.pop_front();
    }
    recompute();
}

void ClockSync::recompute() {
    // Current best estimate: min-RTT sample among the latest raw window, blended with the drift model.
    auto best = std::min_element(m_window.begin(), m_window.end(),
                                 [](const Sample& a, const Sample& b) { return a.rtt < b.rtt; });
    if (best == m_window.end()) return;

    if (m_filtered.size() >= 4 && m_filtered.back().rx - m_filtered.front().rx >= kMinDriftSpanNs) {
        // Least-squares fit offset = a + b*(rx - rx0), weighting low-RTT samples more.
        double rx0 = (double)m_filtered.front().rx;
        double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        double minR = (double)m_minRtt;
        for (const auto& s : m_filtered) {
            double w = 1.0 / (1.0 + std::max(0.0, ((double)s.rtt - minR) / 1e6));   // down-weight slow pings
            double x = (double)s.rx - rx0;
            double y = (double)s.offset;
            sw += w;
            sx += w * x;
            sy += w * y;
            sxx += w * x * x;
            sxy += w * x * y;
        }
        double den = sw * sxx - sx * sx;
        double b = den != 0 ? (sw * sxy - sx * sy) / den : 0;
        b = std::max(-kMaxDrift, std::min(kMaxDrift, b));
        double a = (sy - b * sx) / sw;
        m_baseRx = (int64_t)rx0;
        m_baseOffset = a;
        m_drift = b;
    } else {
        m_baseRx = best->rx;
        m_baseOffset = (double)best->offset;
        m_drift = 0;
    }
    m_haveEstimate = true;
}

int64_t ClockSync::offsetAt(int64_t rxNs) const {
    if (!m_haveEstimate) return 0;
    return (int64_t)std::llround(m_baseOffset + m_drift * (double)(rxNs - m_baseRx));
}

// ------------------------------------------------------------------------------------------------ FrameScheduler

int FrameScheduler::nominalRefreshesPerFrame(int64_t frameDurationNs) const {
    if (m_refreshNs <= 0 || frameDurationNs <= 0) return 1;
    return std::max<int>(1, (int)std::llround((double)frameDurationNs / (double)m_refreshNs));
}

int FrameScheduler::pick(const std::vector<ScheduledItem>& ready, int64_t displayNs, int64_t pendingDueNs) {
    // A frame belongs to the refresh whose display time is nearest its due time.
    const int64_t threshold = displayNs + m_refreshNs / 2;
    // Latest frame (in sequence order) that is due. A frame with a bogus far-future due time must not block the
    // frames behind it, so scan the whole list rather than stopping at the first frame that isn't due.
    int chosen = -1;
    for (int i = 0; i < (int)ready.size(); i++)
        if (ready[i].sync || ready[i].dueNs <= threshold) chosen = i;
    if (pendingDueNs != INT64_MAX && pendingDueNs <= threshold) {
        bool covered = chosen >= 0 && ready[chosen].dueNs >= pendingDueNs;
        if (!covered) m_stats.holds++;
    }
    return chosen;
}

void FrameScheduler::finish() {
    if (!m_haveCurrent) return;
    int c = m_currentCount;
    m_stats.refreshHistogram[(c >= 1 && c <= 7) ? c : 0]++;
    m_stats.framesShown++;
    m_haveCurrent = false;
}

void FrameScheduler::onRefresh(uint64_t seqOnScreen, int64_t frameDurationNs, bool steadyPlayback) {
    if (m_haveCurrent && seqOnScreen == m_currentSeq) {
        m_currentCount++;
        return;
    }
    if (m_haveCurrent) {
        int c = m_currentCount;
        m_stats.refreshHistogram[(c >= 1 && c <= 7) ? c : 0]++;
        m_stats.framesShown++;
        if (m_currentSteady && steadyPlayback && m_currentDuration > 0 &&
            c != nominalRefreshesPerFrame(m_currentDuration))
            m_stats.cadenceErrors++;
    }
    if (seqOnScreen > m_lastShownSeq + 1 && m_lastShownSeq != 0 && seqOnScreen - m_lastShownSeq < 1000000)
        m_stats.framesSkipped += seqOnScreen - m_lastShownSeq - 1;
    m_lastShownSeq = seqOnScreen;
    m_currentSeq = seqOnScreen;
    m_haveCurrent = true;
    m_currentCount = 1;
    m_currentDuration = frameDurationNs;
    m_currentSteady = steadyPlayback;
}

// ------------------------------------------------------------------------------------------------ refresh rate

float pickRefreshRate(const std::vector<float>& available, double fps, double* errorOut) {
    if (available.empty()) return 0;
    float bestRate = available.front();
    double bestErr = 1e9;
    for (float r : available) {
        if (r <= 0 || fps <= 0) continue;
        double ratio = r / fps;
        double k = std::round(ratio);
        if (k < 1) continue;
        double err = std::fabs(ratio - k) / k;
        bool better = err < bestErr - 1e-4 ||
                      (std::fabs(err - bestErr) <= 1e-4 && std::fabs(r - 90.0f) < std::fabs(bestRate - 90.0f));
        if (better) {
            bestErr = err;
            bestRate = r;
        }
    }
    if (errorOut) *errorOut = bestErr;
    return bestRate;
}

// ------------------------------------------------------------------------------------------------ RollingStat

void RollingStat::add(double v) {
    m_v.push_back(v);
    if (m_v.size() > m_cap) m_v.pop_front();
}

double RollingStat::percentile(double p) const {
    if (m_v.empty()) return 0;
    std::vector<double> s(m_v.begin(), m_v.end());
    size_t idx = (size_t)std::min<double>((double)s.size() - 1, std::max(0.0, p / 100.0 * (double)(s.size() - 1) + 0.5));
    std::nth_element(s.begin(), s.begin() + (long)idx, s.end());
    return s[idx];
}

}  // namespace vrltime
