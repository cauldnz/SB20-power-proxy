#pragma once
// CompareService — the #10 A/B meter-compare lifecycle behind a 4-call interface.
//
// WHY THIS EXISTS: the compare wiring was accreting into main.cpp (a MeterCompare global, a feed
// function that faked meter B, a projection loop inside buildLcdViews, a JSON provider lambda) —
// exactly the main.cpp-as-dumping-ground the R-series calls "the real liability"
// (code/findings/architecture-remediation.md). This owns all of it; main.cpp just wires adapters.
//
// THE SEAM IS THE SOURCE. Both meters arrive as an injected Source, so "live meter" vs "bench ramp"
// vs "test fake" is *which adapter you pass*, never a branch in here. On the A side the seam is
// already real: main.cpp passes the live proxy reading (falling back to rampSource) and the host
// tests inject their own. The B side is the pending work — every B adapter that exists today
// FABRICATES B from A, so a BSource carries a `simulated` bit the surfaces must honour (below).
//
// Pure: no Arduino, no BLE, no LVGL. The interface IS the test surface — inject fake Sources, tick(),
// then assert fillView()/json(). Everything deep sits behind it: the rolling MeterCompare (pairing
// window, torque derivation, band + power×cadence-grid math), the feed throttle, the view projection,
// and the /compare JSON emission.
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "MeterCompare.h"
#include "PowerReading.h"  // the only thing we need from the source side
#include "UiModel.h"       // CompareView
#include "WebJson.h"       // renderCompareJson

namespace sb20proxy {

class CompareService {
public:
    // The latest reading from one meter, as of the tick's clock. power_w <= 0 means "nothing to pair
    // yet" — the service skips those, so a disconnected meter simply stops contributing (no
    // special-casing by callers). CONTRACT: a Source must be idempotent within a tick — same nowMs,
    // same reading — because a derived source (scaledSource) reads its upstream again in that tick.
    using Source = std::function<PowerReading(uint32_t nowMs)>;

    // Meter B plus the one fact a caller must not be free to get wrong: is B an independent meter,
    // or fabricated from A? A fabricated B makes every statistic a restatement of the fabrication,
    // so the bit rides WITH the adapter that fabricates it rather than being a ctor flag someone
    // forgets to set. A hand-written Source (a real meter) converts implicitly and is not simulated.
    struct BSource {
        Source fn;
        bool simulated = false;
        // Templated so a bare lambda converts in ONE step (lambda -> Source -> BSource would be two
        // user-defined conversions, which C++ won't do implicitly). The enable_if keeps it from
        // hijacking the copy constructor.
        template <typename F, typename = typename std::enable_if<
                                  !std::is_same<typename std::decay<F>::type, BSource>::value>::type>
        BSource(F&& f, bool sim = false) : fn(std::forward<F>(f)), simulated(sim) {}
    };

    CompareService(Source a, BSource b, std::string aName = "Meter A", std::string bName = "Meter B",
                   uint32_t feedIntervalMs = 500)
        : a_(std::move(a)), b_(std::move(b.fn)), simulated_(b.simulated), aName_(std::move(aName)),
          bName_(std::move(bName)), feedMs_(feedIntervalMs) {}

    // Pull one paired sample. Safe to call every loop — it self-throttles to feedIntervalMs (real
    // meters tick ~1 Hz; flooding the window would bias the rolling stats toward whatever is idle).
    void tick(uint32_t nowMs) {
        if (lastFeed_ && (nowMs - lastFeed_) < feedMs_) return;
        lastFeed_ = nowMs ? nowMs : 1;
        if (!a_ || !b_) return;
        const PowerReading ra = a_(nowMs), rb = b_(nowMs);
        if (ra.power_w <= 0 || rb.power_w <= 0) return;   // a meter is quiet -> nothing to pair
        cmp_.onA(ra.power_w, nowMs, ra.cadence_rpm);
        cmp_.onB(rb.power_w, nowMs, rb.cadence_rpm);
    }

    // Project for the head-unit Compare screen (bias by TORQUE band — the whole point of #10).
    // Not free: it walks the rolling window. Callers should skip it when the screen isn't visible.
    void fillView(CompareView& v) const {
        v.aName = aName_;
        v.bName = bName_;
        v.simulated = simulated_;
        const MeterCompareStats s = cmp_.stats();
        v.valid = s.valid;
        v.aWatts = (int16_t)s.aWatts;
        v.bWatts = (int16_t)s.bWatts;
        v.deltaW = (int16_t)s.deltaW;
        v.ratio = s.meanRatio;
        v.biasPct = s.meanBiasPct;
        v.nPairs = (uint16_t)s.nPairs;
        const std::vector<MeterBand> tb = cmp_.torqueBands();   // NBANDS === kTorqueBands: a full copy
        for (int i = 0; i < CompareView::NBANDS && i < (int)tb.size(); ++i) v.bands[i] = tb[i];
    }

    // The GET /compare payload the web Compare view renders.
    std::string json() const { return renderCompareJson(cmp_, aName_, bName_, simulated_); }

    bool simulated() const { return simulated_; }

    void reset() { cmp_.reset(); lastFeed_ = 0; }

private:
    Source a_, b_;
    bool simulated_;
    std::string aName_, bName_;
    uint32_t feedMs_;
    uint32_t lastFeed_ = 0;
    MeterCompare cmp_;
};

// ---- B-source adapters -------------------------------------------------------------------------
// Bench: derive meter B from meter A by a fixed ratio (B reads ratioMilli/1000 of A). This is NOT a
// measurement — B is A restated, so the bias equals the ratio BY CONSTRUCTION and every torque band
// and grid cell reads identically. Marked `simulated` so the surfaces report a stand-in instead of
// printing a metrology verdict ("flat across torque — a clean scale error") about a number this
// function invented: whether the real SB20's ~+11% is flat or torque-dependent is precisely the OPEN
// question these views exist to answer (code/findings/meter-compare-visualization.md).
inline CompareService::BSource scaledSource(CompareService::Source a, const int* ratioMilli) {
    return CompareService::BSource(
        [a, ratioMilli](uint32_t nowMs) {
            PowerReading r = a ? a(nowMs) : PowerReading{};
            if (r.power_w > 0 && ratioMilli)
                r.power_w = (int16_t)((int)r.power_w * (*ratioMilli) / 1000);
            return r;
        },
        /*simulated=*/true);
}

// Bench: a synthetic rider (power sweeps, cadence drifts) so the torque bands fill with no meter
// attached. Advances at most once per stepMs, so repeated reads within a tick are idempotent (the
// Source contract) — a derived scaledSource() therefore sees exactly the reading A was paired on.
inline CompareService::Source rampSource(uint32_t stepMs = 500) {
    struct State { int w = 100, dir = 8, cad = 90, cdir = 3; uint32_t last = 0; };
    auto st = std::make_shared<State>();
    return [st, stepMs](uint32_t nowMs) {
        if (st->last == 0 || (nowMs - st->last) >= stepMs) {
            st->last = nowMs ? nowMs : 1;
            st->w += st->dir;
            if (st->w >= 340 || st->w <= 90) st->dir = -st->dir;
            st->cad += st->cdir;
            if (st->cad >= 105 || st->cad <= 70) st->cdir = -st->cdir;
        }
        PowerReading r;
        r.power_w = (int16_t)st->w;
        r.cadence_rpm = (int16_t)st->cad;
        return r;
    };
}

}  // namespace sb20proxy
