#pragma once
// MeterCompare — pure, host-tested LIVE comparison of two power-meter streams (A vs B).
//
// Feed timestamped watts from each meter; it pairs the freshest samples within a small time window
// and keeps rolling agreement stats: the latest paired readings + delta, the rolling mean ratio
// (B/A) and bias (%), a pair count, and a per-power-band bias table (do they diverge more at high
// power?). This is the always-on "do these two agree?" surface for the head-unit + web — distinct
// from CalibrationSession, which fits a *correction* from paired samples. No Arduino, no BLE: it
// host-tests with synthetic streams exactly like the other pure cores.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sb20proxy {

// Per-power-band agreement (bands of `kBandW` watts). meanRatio ~1.0 => the meters agree in that band.
struct MeterBand {
    int loW = 0, hiW = 0;     // band edges — watts, or N·m for torqueBands(), or rpm for cadenceBands()
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
};

class MeterCompare {
public:
    static constexpr int kBandW = 50;    // power-band width (watts)
    static constexpr int kBands = 12;    // 0..600W
    // pairWindowMs: A and B samples closer than this in time form a pair. minWattsForRatio: guard the
    // ratio/bias math against tiny denominators (coasting). maxPairs: rolling-window depth.
    explicit MeterCompare(uint32_t pairWindowMs = 700, int minWattsForRatio = 20, size_t maxPairs = 512)
        : pairWindowMs_(pairWindowMs), minW_(minWattsForRatio), maxPairs_(maxPairs) {}

    // cadence (rpm) is optional: -1 = unknown (keeps power-only behaviour). It enables the torque
    // and power×cadence views — torque is derived from meter A (the reference): Nm = W / (rpm·2π/60).
    void onA(int watts, uint32_t t_ms, int cadence = -1) { latestA_ = {watts, t_ms, ++seqA_, cadence}; tryPair(); }
    void onB(int watts, uint32_t t_ms, int cadence = -1) { latestB_ = {watts, t_ms, ++seqB_, cadence}; tryPair(); }

    void reset() {
        pairs_.clear();
        haveA_ = haveB_ = false;
        seqA_ = seqB_ = 0;
        pairedSeqA_ = pairedSeqB_ = 0;
    }

    MeterCompareStats stats() const {
        MeterCompareStats s;
        if (pairs_.empty()) return s;
        s.valid = true;
        const Pair& last = pairs_.back();
        s.aWatts = last.a;
        s.bWatts = last.b;
        s.deltaW = last.b - last.a;
        double sumRatio = 0.0, sumBias = 0.0;
        int m = 0;
        for (const auto& p : pairs_) {
            if (p.a < minW_) continue;  // ignore near-zero denominators
            sumRatio += (double)p.b / (double)p.a;
            sumBias += (double)(p.b - p.a) / (double)p.a * 100.0;
            ++m;
        }
        s.nPairs = (int)pairs_.size();
        if (m > 0) {
            s.meanRatio = (float)(sumRatio / m);
            s.meanBiasPct = (float)(sumBias / m);
        }
        return s;
    }

    // Per-power-band table (indexed by watts/kBandW), computed over the rolling window.
    std::vector<MeterBand> bands() const {
        std::vector<MeterBand> b(kBands);
        std::vector<double> sr(kBands, 0.0), sb(kBands, 0.0);
        for (int i = 0; i < kBands; ++i) { b[i].loW = i * kBandW; b[i].hiW = (i + 1) * kBandW; }
        for (const auto& p : pairs_) {
            if (p.a < minW_) continue;
            int idx = p.a / kBandW;
            if (idx < 0 || idx >= kBands) continue;
            b[idx].nPairs++;
            sr[idx] += (double)p.b / (double)p.a;
            sb[idx] += (double)(p.b - p.a) / (double)p.a * 100.0;
        }
        for (int i = 0; i < kBands; ++i)
            if (b[i].nPairs > 0) {
                b[i].meanRatio = (float)(sr[i] / b[i].nPairs);
                b[i].meanBiasPct = (float)(sb[i] / b[i].nPairs);
            }
        return b;
    }

    // --- torque / cadence views (need cadence on meter A) --------------------------------------
    static constexpr int kTorqueBandNm = 5;   // torque-band width (N·m)
    static constexpr int kTorqueBands = 12;   // 0..60 N·m
    static constexpr int kGridPBins = 8;      // grid power axis: 0..400 W (50 W bins)
    static constexpr int kGridCBins = 6;      // grid cadence axis: <cBinLo, then cBinW-rpm bins

    // Bias by TORQUE band (5 N·m) — reveals torque-dependent error that power bins mix away. Torque
    // from meter A (the reference): Nm = W / (rpm · 2π/60). Pairs with unknown cadence are skipped.
    std::vector<MeterBand> torqueBands() const {
        std::vector<MeterBand> b(kTorqueBands);
        std::vector<double> sr(kTorqueBands, 0.0), sb(kTorqueBands, 0.0);
        for (int i = 0; i < kTorqueBands; ++i) { b[i].loW = i * kTorqueBandNm; b[i].hiW = (i + 1) * kTorqueBandNm; }
        for (const auto& p : pairs_) {
            if (p.a < minW_ || p.aCad <= 0) continue;
            const float torque = (float)p.a / ((float)p.aCad * 0.10471976f);
            int idx = (int)(torque / kTorqueBandNm);
            if (idx < 0) idx = 0;
            if (idx >= kTorqueBands) idx = kTorqueBands - 1;   // clamp sprints to the top band
            b[idx].nPairs++;
            sr[idx] += (double)p.b / (double)p.a;
            sb[idx] += (double)(p.b - p.a) / (double)p.a * 100.0;
        }
        for (int i = 0; i < kTorqueBands; ++i)
            if (b[i].nPairs > 0) {
                b[i].meanRatio = (float)(sr[i] / b[i].nPairs);
                b[i].meanBiasPct = (float)(sb[i] / b[i].nPairs);
            }
        return b;
    }

    // Bias by CADENCE band (10 rpm, 40..140) — the other 1-D slice.
    std::vector<MeterBand> cadenceBands() const {
        constexpr int NB = 10, W = 10, LO = 40;
        std::vector<MeterBand> b(NB);
        std::vector<double> sr(NB, 0.0), sb(NB, 0.0);
        for (int i = 0; i < NB; ++i) { b[i].loW = LO + i * W; b[i].hiW = LO + (i + 1) * W; }
        for (const auto& p : pairs_) {
            if (p.a < minW_ || p.aCad <= 0) continue;
            int idx = (p.aCad - LO) / W;
            if (idx < 0) idx = 0;
            if (idx >= NB) idx = NB - 1;
            b[idx].nPairs++;
            sr[idx] += (double)p.b / (double)p.a;
            sb[idx] += (double)(p.b - p.a) / (double)p.a * 100.0;
        }
        for (int i = 0; i < NB; ++i)
            if (b[i].nPairs > 0) {
                b[i].meanRatio = (float)(sr[i] / b[i].nPairs);
                b[i].meanBiasPct = (float)(sb[i] / b[i].nPairs);
            }
        return b;
    }

    // The power×cadence agreement grid (the web heatmap): cell[powerBin][cadenceBin].meanBiasPct.
    struct Grid2D {
        MeterGridCell cell[kGridPBins][kGridCBins];
        int pBinW = 50, cBinLo = 45, cBinW = 15;   // axes: power 0..400 W; cadence <45, then 15 rpm
    };
    Grid2D grid2d() const {
        Grid2D g;
        double sb[kGridPBins][kGridCBins] = {};
        for (const auto& p : pairs_) {
            if (p.a < minW_ || p.aCad <= 0) continue;
            int pi = p.a / g.pBinW;
            int ci = (p.aCad - g.cBinLo) / g.cBinW;
            if (pi < 0) pi = 0;
            if (pi >= kGridPBins) pi = kGridPBins - 1;
            if (ci < 0) ci = 0;
            if (ci >= kGridCBins) ci = kGridCBins - 1;
            g.cell[pi][ci].nPairs++;
            sb[pi][ci] += (double)(p.b - p.a) / (double)p.a * 100.0;
        }
        for (int pi = 0; pi < kGridPBins; ++pi)
            for (int ci = 0; ci < kGridCBins; ++ci)
                if (g.cell[pi][ci].nPairs > 0)
                    g.cell[pi][ci].meanBiasPct = (float)(sb[pi][ci] / g.cell[pi][ci].nPairs);
        return g;
    }

    // Downsampled (a,b) pairs for a Bland-Altman scatter (every k-th pair, up to maxN).
    struct SamplePair { int a, b; };
    std::vector<SamplePair> samplePairs(int maxN = 120) const {
        std::vector<SamplePair> out;
        if (pairs_.empty() || maxN <= 0) return out;
        int step = (int)(pairs_.size() / (size_t)maxN);
        if (step < 1) step = 1;
        for (size_t i = 0; i < pairs_.size(); i += (size_t)step) out.push_back({pairs_[i].a, pairs_[i].b});
        return out;
    }

    int pairCount() const { return (int)pairs_.size(); }

private:
    struct Sample { int w = 0; uint32_t t = 0; uint32_t seq = 0; int cad = -1; };
    struct Pair { int a; int b; int aCad; };

    void tryPair() {
        if (seqA_ == 0 || seqB_ == 0) return;                 // need at least one of each
        const uint32_t dt = latestA_.t > latestB_.t ? latestA_.t - latestB_.t : latestB_.t - latestA_.t;
        if (dt > pairWindowMs_) return;                       // not co-temporal
        if (latestA_.seq == pairedSeqA_ && latestB_.seq == pairedSeqB_) return;  // already paired this duo
        pairedSeqA_ = latestA_.seq;
        pairedSeqB_ = latestB_.seq;
        pairs_.push_back({latestA_.w, latestB_.w, latestA_.cad});
        if (pairs_.size() > maxPairs_) pairs_.erase(pairs_.begin());
    }

    uint32_t pairWindowMs_;
    int minW_;
    size_t maxPairs_;
    Sample latestA_, latestB_;
    bool haveA_ = false, haveB_ = false;
    uint32_t seqA_ = 0, seqB_ = 0, pairedSeqA_ = 0, pairedSeqB_ = 0;
    std::vector<Pair> pairs_;
};

}  // namespace sb20proxy
