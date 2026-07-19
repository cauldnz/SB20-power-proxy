#pragma once
// MeterCompare — pure, host-tested LIVE comparison of two power-meter streams (A vs B).
//
// Feed timestamped watts from each meter; it pairs the freshest samples within a small time window
// and keeps rolling agreement stats: the latest paired readings + delta, the rolling mean ratio
// (B/A) and bias (%), a pair count, and the band views — by power, by TORQUE, and a power×cadence
// grid. This is the always-on "do these two agree?" surface for the head-unit + web — distinct from
// CalibrationSession, which fits a *correction* from paired samples. No Arduino, no BLE: it
// host-tests with synthetic streams exactly like the other pure cores.
//
// The rolling window is a FIXED RING in .bss — never the heap. A push_back/erase(begin()) vector
// grows its capacity past the cap and holds ~2x the live bytes for the whole ride, on boards where
// the no-PSRAM CYD already stalls its web UI near ~27 KB free.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sb20proxy {

// |bias| below this reads as "these meters agree" — the shared domain rule every Compare surface
// uses (the head-unit verdict, the web verdict, the Python twin). One threshold, one place.
constexpr float kAgreeBandPct = 2.0f;

// Per-band agreement. `loW` is the band's lower edge — watts for bands(), N·m for torqueBands();
// the width is the matching kBandW/kTorqueBandNm constant. meanRatio ~1.0 => they agree in-band.
struct MeterBand {
    int loW = 0;
    int nPairs = 0;
    float meanRatio = 0.0f;   // mean(b/a) in this band (0 if empty)
    float meanBiasPct = 0.0f; // mean((b-a)/a*100)
};

// One cell of the power×cadence agreement grid (the web heatmap).
struct MeterGridCell {
    int nPairs = 0;
    float meanBiasPct = 0.0f;
};

struct MeterCompareStats {
    bool valid = false;       // at least one usable pair?
    int aWatts = 0, bWatts = 0;   // latest paired readings
    int deltaW = 0;               // b - a (latest pair)
    float meanRatio = 1.0f;       // rolling mean of b/a  (1.0 = perfect agreement)
    float meanBiasPct = 0.0f;     // rolling mean of (b-a)/a * 100
    int nPairs = 0;               // pairs in the rolling window
    bool agrees() const { return meanBiasPct > -kAgreeBandPct && meanBiasPct < kAgreeBandPct; }
};

class MeterCompare {
public:
    static constexpr int kBandW = 50;    // power-band width (watts)
    static constexpr int kBands = 12;    // 0..600W
    static constexpr size_t kMaxPairs = 512;   // rolling-window depth (fixed ring, ~6 KB in .bss)
    // pairWindowMs: A and B samples closer than this in time form a pair. minWattsForRatio: guard the
    // ratio/bias math against tiny denominators (coasting).
    explicit MeterCompare(uint32_t pairWindowMs = 700, int minWattsForRatio = 20)
        : pairWindowMs_(pairWindowMs), minW_(minWattsForRatio) {}

    // cadence (rpm) is optional: -1 = unknown (keeps power-only behaviour). It enables the torque
    // and power×cadence views — torque is derived from meter A (the reference): Nm = W / (rpm·2π/60).
    void onA(int watts, uint32_t t_ms, int cadence = -1) { latestA_ = {watts, t_ms, cadence}; ++seqA_; tryPair(); }
    void onB(int watts, uint32_t t_ms, int cadence = -1) { latestB_ = {watts, t_ms, cadence}; ++seqB_; tryPair(); }

    void reset() {
        n_ = wr_ = 0;
        seqA_ = seqB_ = 0;
        pairedSeqA_ = pairedSeqB_ = 0;
    }

    MeterCompareStats stats() const {
        MeterCompareStats s;
        if (n_ == 0) return s;
        s.valid = true;
        const Pair& last = at(n_ - 1);
        s.aWatts = last.a;
        s.bWatts = last.b;
        s.deltaW = last.b - last.a;
        s.nPairs = (int)n_;
        Acc acc;
        for (size_t i = 0; i < n_; ++i) {
            const Pair& p = at(i);
            if (p.a < minW_) continue;   // ignore near-zero denominators
            acc.add(biasPct(p));
        }
        if (acc.n > 0) {
            s.meanBiasPct = acc.mean();
            s.meanRatio = ratioFromBias(s.meanBiasPct);   // mean(b/a) === 1 + mean(bias%)/100
        }
        return s;
    }

    // Per-power-band table (indexed by watts/kBandW), computed over the rolling window. Out-of-range
    // power is DROPPED (unlike torqueBands, which clamps) — a 700 W spike belongs in no band here.
    std::vector<MeterBand> bands() const {
        std::vector<MeterBand> b(kBands);
        std::vector<Acc> acc(kBands);
        for (int i = 0; i < kBands; ++i) b[i].loW = i * kBandW;
        for (size_t i = 0; i < n_; ++i) {
            const Pair& p = at(i);
            if (p.a < minW_) continue;
            const int idx = p.a / kBandW;
            if (idx < 0 || idx >= kBands) continue;
            acc[idx].add(biasPct(p));
        }
        finish(b, acc);
        return b;
    }

    // --- torque / cadence views (need cadence on meter A) --------------------------------------
    static constexpr int kTorqueBandNm = 5;   // torque-band width (N·m)
    static constexpr int kTorqueBands = 12;   // 0..60 N·m
    static constexpr int kGridPBins = 8;      // grid power axis: 0..400 W (50 W bins)
    static constexpr int kGridCBins = 6;      // grid cadence axis: <cBinLo, then cBinW-rpm bins

    // Bias by TORQUE band (5 N·m) — reveals torque-dependent error that power bins mix away. Torque
    // from meter A (the reference): Nm = W / (rpm · 2π/60). Pairs with unknown cadence are skipped.
    // Sprints past the top edge CLAMP into the last band rather than vanishing from the view.
    std::vector<MeterBand> torqueBands() const {
        std::vector<MeterBand> b(kTorqueBands);
        std::vector<Acc> acc(kTorqueBands);
        for (int i = 0; i < kTorqueBands; ++i) b[i].loW = i * kTorqueBandNm;
        for (size_t i = 0; i < n_; ++i) {
            const Pair& p = at(i);
            if (p.a < minW_ || p.aCad <= 0) continue;
            const float torque = (float)p.a / ((float)p.aCad * 0.10471976f);
            acc[clampIdx((int)(torque / kTorqueBandNm), kTorqueBands)].add(biasPct(p));
        }
        finish(b, acc);
        return b;
    }

    // The power×cadence agreement grid (the web heatmap): cell[powerBin][cadenceBin].meanBiasPct.
    struct Grid2D {
        MeterGridCell cell[kGridPBins][kGridCBins];
        int pBinW = 50, cBinLo = 45, cBinW = 15;   // axes: power 0..400 W; cadence <45, then 15 rpm
    };
    Grid2D grid2d() const {
        Grid2D g;
        Acc acc[kGridPBins][kGridCBins];
        for (size_t i = 0; i < n_; ++i) {
            const Pair& p = at(i);
            if (p.a < minW_ || p.aCad <= 0) continue;
            const int pi = clampIdx(p.a / g.pBinW, kGridPBins);
            const int ci = clampIdx((p.aCad - g.cBinLo) / g.cBinW, kGridCBins);
            acc[pi][ci].add(biasPct(p));
        }
        for (int pi = 0; pi < kGridPBins; ++pi)
            for (int ci = 0; ci < kGridCBins; ++ci) {
                g.cell[pi][ci].nPairs = acc[pi][ci].n;
                g.cell[pi][ci].meanBiasPct = acc[pi][ci].mean();
            }
        return g;
    }

    // Downsampled (a,b) pairs for a Bland-Altman scatter (every k-th pair, up to maxN).
    struct SamplePair { int a, b; };
    std::vector<SamplePair> samplePairs(int maxN = 120) const {
        std::vector<SamplePair> out;
        if (n_ == 0 || maxN <= 0) return out;
        size_t step = n_ / (size_t)maxN;
        if (step < 1) step = 1;
        out.reserve(n_ / step + 1);
        for (size_t i = 0; i < n_; i += step) out.push_back({at(i).a, at(i).b});
        return out;
    }

    int pairCount() const { return (int)n_; }

private:
    struct Sample { int w = 0; uint32_t t = 0; int cad = -1; };
    struct Pair { int a; int b; int aCad; };

    // Every band/grid table reduces to this: count pairs, sum their bias, divide.
    struct Acc {
        int n = 0;
        float sumBias = 0.0f;
        void add(float bias) { ++n; sumBias += bias; }
        float mean() const { return n > 0 ? sumBias / (float)n : 0.0f; }
    };

    static float biasPct(const Pair& p) { return (float)(p.b - p.a) / (float)p.a * 100.0f; }
    static float ratioFromBias(float bias) { return 1.0f + bias / 100.0f; }
    static int clampIdx(int idx, int n) { return idx < 0 ? 0 : (idx >= n ? n - 1 : idx); }

    static void finish(std::vector<MeterBand>& b, const std::vector<Acc>& acc) {
        for (size_t i = 0; i < b.size(); ++i) {
            b[i].nPairs = acc[i].n;
            if (acc[i].n > 0) {
                b[i].meanBiasPct = acc[i].mean();
                b[i].meanRatio = ratioFromBias(b[i].meanBiasPct);
            }
        }
    }

    // Ring access: i == 0 is the OLDEST live pair, i == n_-1 the newest.
    const Pair& at(size_t i) const {
        const size_t oldest = (n_ == kMaxPairs) ? wr_ : 0;
        return ring_[(oldest + i) % kMaxPairs];
    }

    void tryPair() {
        if (seqA_ == 0 || seqB_ == 0) return;                 // need at least one of each
        const uint32_t dt = latestA_.t > latestB_.t ? latestA_.t - latestB_.t : latestB_.t - latestA_.t;
        if (dt > pairWindowMs_) return;                       // not co-temporal
        if (seqA_ == pairedSeqA_ && seqB_ == pairedSeqB_) return;  // already paired this duo
        pairedSeqA_ = seqA_;
        pairedSeqB_ = seqB_;
        ring_[wr_] = {latestA_.w, latestB_.w, latestA_.cad};
        wr_ = (wr_ + 1) % kMaxPairs;
        if (n_ < kMaxPairs) ++n_;                             // full ring: the oldest pair is overwritten
    }

    uint32_t pairWindowMs_;
    int minW_;
    Sample latestA_, latestB_;
    uint32_t seqA_ = 0, seqB_ = 0, pairedSeqA_ = 0, pairedSeqB_ = 0;
    Pair ring_[kMaxPairs] = {};
    size_t n_ = 0;    // live pairs (<= kMaxPairs)
    size_t wr_ = 0;   // next write slot
};

}  // namespace sb20proxy
