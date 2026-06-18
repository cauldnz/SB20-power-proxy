#pragma once
#include <cstdint>

namespace sb20proxy {

// Pure loop-timing accumulator: fed one timestamp (µs) per loop(), it tracks the loop-period
// distribution (bucketed -> approx p50/p95), the max, the mean, and stall counts. No esp_timer /
// FreeRTOS here — main.cpp passes esp_timer_get_time() — so the percentile/threshold logic is
// host-tested with synthetic timestamps, like Status.h/OledScreen.h. Memory-bounded (fixed
// buckets) and cheap (a subtract + a few compares per loop), so the monitor can't become the
// stall it measures. The #1 signal for a cooperative loop: max + stall counts spiking = something
// blocked (I2C, a synchronous WiFi op, a BLE callback).
struct LoopStats {
    uint32_t count = 0;
    uint32_t maxUs = 0;
    uint32_t meanUs = 0;
    uint32_t p50Us = 0;
    uint32_t p95Us = 0;
    uint32_t stalls50 = 0;   // loop periods > 50 ms
    uint32_t stalls200 = 0;  // loop periods > 200 ms
};

class PerfMonitor {
public:
    // Upper edges (µs) of the period buckets; the final bucket catches everything above the last.
    static constexpr uint32_t kEdgesUs[] = {500,    1000,   2000,   5000,    10000,  20000,
                                            50000,  100000, 200000, 500000,  1000000};
    static constexpr int kNumEdges = 11;
    static constexpr int kNumBuckets = kNumEdges + 1;  // 12

    void sample(uint64_t nowUs) {
        if (havePrev_) {
            uint32_t dt = (uint32_t)(nowUs - prevUs_);
            ++count_;
            sumUs_ += dt;
            if (dt > maxUs_) maxUs_ = dt;
            if (dt > 50000) ++stalls50_;
            if (dt > 200000) ++stalls200_;
            ++buckets_[bucketFor(dt)];
        }
        prevUs_ = nowUs;
        havePrev_ = true;
    }

    LoopStats summary() const {
        LoopStats s;
        s.count = count_;
        s.maxUs = maxUs_;
        s.meanUs = count_ ? (uint32_t)(sumUs_ / count_) : 0;
        s.stalls50 = stalls50_;
        s.stalls200 = stalls200_;
        s.p50Us = percentileUs(50);
        s.p95Us = percentileUs(95);
        return s;
    }

    // Zero the window (perf_soak resets at soak start so a run is a clean window). Keep prevUs_ so
    // the next dt isn't a spurious spike spanning the reset.
    void reset() {
        count_ = 0;
        maxUs_ = 0;
        sumUs_ = 0;
        stalls50_ = 0;
        stalls200_ = 0;
        for (int i = 0; i < kNumBuckets; ++i) buckets_[i] = 0;
    }

private:
    static int bucketFor(uint32_t dt) {
        for (int i = 0; i < kNumEdges; ++i)
            if (dt < kEdgesUs[i]) return i;
        return kNumBuckets - 1;
    }

    // Approx percentile = the upper edge of the bucket where the cumulative count crosses p%
    // (conservative — rounds toward the slower edge). Coarse but consistent, which is what A/B
    // comparison needs. The overflow bucket reports the real max.
    uint32_t percentileUs(int p) const {
        if (count_ == 0) return 0;
        uint32_t target = (uint32_t)((uint64_t)count_ * (uint64_t)p / 100);
        if (target == 0) target = 1;
        uint32_t cum = 0;
        for (int i = 0; i < kNumBuckets; ++i) {
            cum += buckets_[i];
            if (cum >= target) return i < kNumEdges ? kEdgesUs[i] : maxUs_;
        }
        return maxUs_;
    }

    uint64_t prevUs_ = 0;
    bool havePrev_ = false;
    uint32_t count_ = 0, maxUs_ = 0, stalls50_ = 0, stalls200_ = 0;
    uint64_t sumUs_ = 0;
    uint32_t buckets_[kNumBuckets] = {0};
};

}  // namespace sb20proxy
