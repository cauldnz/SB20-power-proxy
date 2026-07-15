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
    int loW = 0, hiW = 0;
    int nPairs = 0;
    float meanRatio = 0.0f;   // mean(b/a) in this band (0 if empty)
    float meanBiasPct = 0.0f; // mean((b-a)/a*100)
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

    void onA(int watts, uint32_t t_ms) { latestA_ = {watts, t_ms, ++seqA_}; tryPair(); }
    void onB(int watts, uint32_t t_ms) { latestB_ = {watts, t_ms, ++seqB_}; tryPair(); }

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

    int pairCount() const { return (int)pairs_.size(); }

private:
    struct Sample { int w = 0; uint32_t t = 0; uint32_t seq = 0; };
    struct Pair { int a; int b; };

    void tryPair() {
        if (seqA_ == 0 || seqB_ == 0) return;                 // need at least one of each
        const uint32_t dt = latestA_.t > latestB_.t ? latestA_.t - latestB_.t : latestB_.t - latestA_.t;
        if (dt > pairWindowMs_) return;                       // not co-temporal
        if (latestA_.seq == pairedSeqA_ && latestB_.seq == pairedSeqB_) return;  // already paired this duo
        pairedSeqA_ = latestA_.seq;
        pairedSeqB_ = latestB_.seq;
        pairs_.push_back({latestA_.w, latestB_.w});
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
