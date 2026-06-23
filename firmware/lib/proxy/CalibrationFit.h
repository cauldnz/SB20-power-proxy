#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

#include "Correction.h"

namespace sb20proxy {

// On-device meter-to-meter calibration: pair a DUT meter (the one to correct, e.g. an XCadey
// crank) with a reference meter (e.g. Assioma pedals) read at the same time, then fit a
// correction the proxy applies so the DUT reads like the reference. The C++ mirror of the Python
// pipeline (code/src/sb20proxy/calibration.py) — same binning + factor-per-bin math — so the
// on-device fit matches the desk tool's golden values. Pure + header-only: host-unit-tested with
// no hardware, exactly like Correction.h / Cps.h.

// Plausibility window (mirror calibration.py MIN/MAX_POWER_W): drop meter glitches / coast zeros.
inline constexpr float kCalMinPowerW = 10.0f;
inline constexpr float kCalMaxPowerW = 2000.0f;

struct CalPair {
    float dut;  // power reported by the meter being corrected
    float ref;  // power reported by the reference meter, same moment
};

// Round helpers matching the Python profile's stored precision (center 1 dp, factor 4 dp, etc.)
// so firmware and desk fits agree to the saved digits.
inline float calRound1(float x) { return std::round(x * 10.0f) / 10.0f; }
inline float calRound4(float x) { return std::round(x * 10000.0f) / 10000.0f; }

// Time-aligns two live meter streams into (dut, ref) pairs. Holds the most recent reference
// reading; on each DUT reading, if a reference arrived within maxSkewMs and both powers are
// plausible, records a pair. One pair per DUT sample (deterministic; no double counting). Bounded
// so a long calibration can't exhaust heap. Pure — fed by the firmware seam, host-tested with
// direct calls.
class PairAccumulator {
public:
    explicit PairAccumulator(uint32_t maxSkewMs = 2000, size_t cap = 1200)
        : maxSkewMs_(maxSkewMs), cap_(cap) {}

    void onRef(float power, uint32_t t_ms) {
        refPower_ = power;
        refT_ = t_ms;
        haveRef_ = true;
    }

    void onDut(float power, uint32_t t_ms) {
        if (!haveRef_) return;
        const uint32_t age = (t_ms >= refT_) ? (t_ms - refT_) : (refT_ - t_ms);
        if (age > maxSkewMs_) return;  // streams drifted apart — skip rather than pair stale data
        if (!plausible(power) || !plausible(refPower_)) return;
        if (pairs_.size() < cap_) pairs_.push_back({power, refPower_});
    }

    const std::vector<CalPair>& pairs() const { return pairs_; }
    size_t count() const { return pairs_.size(); }
    bool full() const { return pairs_.size() >= cap_; }

    void clear() {
        pairs_.clear();
        haveRef_ = false;
    }

    // Sample count per fixed power band — the wizard shows this so the rider knows to cover the
    // range (a curve fit needs spread, not just one effort). Bands are [edges[i], edges[i+1]).
    std::vector<int> coverage(const std::vector<float>& edges) const {
        std::vector<int> counts(edges.size() > 0 ? edges.size() - 1 : 0, 0);
        for (const auto& p : pairs_) {
            for (size_t i = 0; i + 1 < edges.size(); ++i) {
                if (p.dut >= edges[i] && p.dut < edges[i + 1]) {
                    counts[i]++;
                    break;
                }
            }
        }
        return counts;
    }

private:
    static bool plausible(float p) { return p >= kCalMinPowerW && p <= kCalMaxPowerW; }

    std::vector<CalPair> pairs_;
    uint32_t maxSkewMs_;
    size_t cap_;
    float refPower_ = 0.0f;
    uint32_t refT_ = 0;
    bool haveRef_ = false;
};

struct ScaleOffset {
    float scale = 1.0f;
    float offset = 0.0f;
};

// Least-squares ref = offset + scale*dut (mirror calibration.fit_scale_offset). Identity if too
// few samples or no spread.
inline ScaleOffset fitScaleOffset(const std::vector<CalPair>& pairs) {
    ScaleOffset so;
    if (pairs.size() < 3) return so;
    double mx = 0, my = 0;
    for (const auto& p : pairs) {
        mx += p.dut;
        my += p.ref;
    }
    mx /= pairs.size();
    my /= pairs.size();
    double sxx = 0, sxy = 0;
    for (const auto& p : pairs) {
        sxx += (p.dut - mx) * (p.dut - mx);
        sxy += (p.dut - mx) * (p.ref - my);
    }
    if (sxx == 0) return so;
    const double scale = sxy / sxx;
    so.scale = calRound4((float)scale);
    so.offset = (float)(std::round((my - scale * mx) * 100.0) / 100.0);
    return so;
}

// Piecewise power->factor curve (mirror calibration.fit_grid): bin by DUT power into nBins, and for
// each bin with >= minPerBin samples set a breakpoint at (mean DUT power, mean(ref/dut)). Returns an
// empty curve (no curve) if there aren't >= 2 usable bins — the caller then falls back to linear.
inline CorrectionCurve fitCurve(const std::vector<CalPair>& pairs, int nBins = 6,
                                int minPerBin = 3) {
    CorrectionCurve curve;
    if ((int)pairs.size() < minPerBin || nBins < 1) return curve;
    float lo = pairs[0].dut, hi = pairs[0].dut;
    for (const auto& p : pairs) {
        lo = std::min(lo, p.dut);
        hi = std::max(hi, p.dut);
    }
    if (hi <= lo) return curve;  // no spread — a curve is meaningless
    const float width = (hi - lo) / nBins;
    std::vector<int> cnt(nBins, 0);
    std::vector<double> sumDut(nBins, 0.0), sumRatio(nBins, 0.0);
    for (const auto& p : pairs) {
        int idx = (int)((p.dut - lo) / width);
        if (idx > nBins - 1) idx = nBins - 1;
        if (idx < 0) idx = 0;
        cnt[idx]++;
        sumDut[idx] += p.dut;
        if (p.dut > 0) sumRatio[idx] += (double)p.ref / p.dut;
    }
    for (int i = 0; i < nBins; ++i) {
        if (cnt[i] < minPerBin) continue;
        curve.add(calRound1((float)(sumDut[i] / cnt[i])),
                  calRound4((float)(sumRatio[i] / cnt[i])));
    }
    if (curve.points.size() < 2) return CorrectionCurve{};  // too sparse — caller uses linear
    return curve;
}

// The "auto" fit (mirror 09_fit_calibration --mode auto): prefer the non-linear curve; fall back to
// linear scale/offset when the data is too sparse for a curve. Returns a ready-to-apply Correction.
inline Correction fitCorrection(const std::vector<CalPair>& pairs, int nBins = 6,
                                int minPerBin = 3) {
    Correction c;
    CorrectionCurve curve = fitCurve(pairs, nBins, minPerBin);
    if (!curve.empty()) {
        c.curve = curve;
        return c;
    }
    const ScaleOffset so = fitScaleOffset(pairs);
    c.scale = so.scale;
    c.offset = so.offset;
    return c;
}

// Reduce any fitted Correction to a power->factor curve for storage in RuntimeConfig (the corrector
// run-mode applies a curve). A curve fit is stored as-is; a linear scale/offset fallback is sampled
// into a few breakpoints (factor = corrected/power at each), so the corrector always carries one
// representation. Powers chosen to span typical riding; factor flat-held outside by CorrectionCurve.
inline CorrectionCurve correctionToCurve(const Correction& c) {
    if (!c.curve.empty()) return c.curve;
    CorrectionCurve out;
    const float samples[] = {50.0f, 100.0f, 150.0f, 200.0f, 300.0f, 400.0f};
    for (float p : samples) {
        float corrected = p * c.scale + c.offset;
        if (corrected < 0.0f) corrected = 0.0f;
        out.add(p, p > 0.0f ? corrected / p : c.scale);
    }
    return out;
}

// Mean absolute residual (watts) of a correction over the pairs — the wizard shows it so the rider
// can judge the fit (mirror calibration.residual_watts mean_w).
inline float residualMeanW(const Correction& c, const std::vector<CalPair>& pairs) {
    if (pairs.empty()) return 0.0f;
    double sum = 0.0;
    for (const auto& p : pairs) {
        PowerReading r;
        r.power_w = (int16_t)std::lround(p.dut);
        sum += std::fabs((double)c.apply(r).power_w - p.ref);
    }
    return (float)(sum / pairs.size());
}

}  // namespace sb20proxy
