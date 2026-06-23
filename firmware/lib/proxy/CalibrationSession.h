#pragma once
#include <cstdint>
#include <vector>

#include "CalibrationFit.h"
#include "Correction.h"

namespace sb20proxy {

// Drives an on-device meter-to-meter calibration: collect paired (DUT, reference) readings while the
// rider sweeps the power range, then fit a correction the corrector applies so the DUT reads like the
// reference. The orchestration the web wizard sits on top of and the seam (two BleMeterClients) feeds.
// Pure — no hardware, no I/O — so the whole state machine + the fit are host-unit-tested.
//
// State: Idle -> (start) Collecting -> (finish, enough pairs) Fitted. cancel() returns to Idle.
enum class CalState { Idle, Collecting, Fitted };

// Default power bands for the coverage display (watts). A good fit needs spread, so the wizard shows
// how many pairs landed in each band and nudges the rider to cover the empty ones.
inline std::vector<float> defaultCoverageEdges() {
    return {0, 100, 150, 200, 250, 300, 2000};  // <100, 100-150, 150-200, 200-250, 250-300, 300+
}

class CalibrationSession {
public:
    // Minimum pairs before a fit is allowed — enough that the per-bin means are stable (mirrors the
    // calibration.py >=3-per-bin intent across ~6 bands). Configurable for tests.
    explicit CalibrationSession(int minPairsToFit = 30) : minPairs_(minPairsToFit) {}

    void start() {
        acc_.clear();
        fit_ = Correction{};
        residualW_ = 0.0f;
        state_ = CalState::Collecting;
    }

    void cancel() {
        acc_.clear();
        fit_ = Correction{};
        residualW_ = 0.0f;
        state_ = CalState::Idle;
    }

    // Feed live readings from the two meters (no-ops unless collecting). The DUT is the meter being
    // corrected; the reference is the trusted meter we want the DUT to match.
    void onDut(float power, uint32_t t_ms) {
        if (state_ == CalState::Collecting) acc_.onDut(power, t_ms);
    }
    void onRef(float power, uint32_t t_ms) {
        if (state_ == CalState::Collecting) acc_.onRef(power, t_ms);
    }

    // Fit the correction from the collected pairs. Returns false (stays Collecting) if too few pairs —
    // the wizard keeps the rider going. On success -> Fitted, with fit() + residualW() populated.
    bool finish() {
        if ((int)acc_.count() < minPairs_) return false;
        fit_ = fitCorrection(acc_.pairs());
        residualW_ = residualMeanW(fit_, acc_.pairs());
        state_ = CalState::Fitted;
        return true;
    }

    CalState state() const { return state_; }
    bool collecting() const { return state_ == CalState::Collecting; }
    bool fitted() const { return state_ == CalState::Fitted; }
    size_t pairCount() const { return acc_.count(); }
    bool enoughToFit() const { return (int)acc_.count() >= minPairs_; }
    int minPairs() const { return minPairs_; }

    const Correction& fit() const { return fit_; }
    float residualW() const { return residualW_; }
    std::vector<int> coverage(const std::vector<float>& edges = defaultCoverageEdges()) const {
        return acc_.coverage(edges);
    }

private:
    int minPairs_;
    PairAccumulator acc_;
    Correction fit_;
    float residualW_ = 0.0f;
    CalState state_ = CalState::Idle;
};

}  // namespace sb20proxy
