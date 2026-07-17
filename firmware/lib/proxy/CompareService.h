#pragma once
// CompareService — the #10 A/B meter-compare lifecycle behind a 4-call interface.
//
// WHY THIS EXISTS: the compare wiring was accreting into main.cpp (a MeterCompare global, a feed
// function that faked meter B, a projection loop inside buildLcdViews, a JSON provider lambda) —
// exactly the main.cpp-as-dumping-ground the R-series calls "the real liability"
// (code/findings/architecture-remediation.md). This owns all of it; main.cpp just wires adapters.
//
// THE SEAM IS THE B-SOURCE. Both meters arrive as an injected `Source`, so "real second meter" vs
// "simulated on the bench" is *which adapter you pass*, never a branch in here. Two adapters exist
// today — the real second BLE central (refMeter) and scaledSource() below — so the seam is real.
//
// Pure: no Arduino, no BLE, no LVGL. The interface IS the test surface — inject fake Sources, tick(),
// then assert fillView()/json(). Everything deep sits behind it: the rolling MeterCompare (pairing
// window, torque derivation, band + power×cadence-grid math), the feed throttle, the view projection,
// and the /compare JSON emission.
#include <functional>
#include <memory>
#include <string>

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

    CompareService(Source a, Source b, std::string aName = "Meter A", std::string bName = "Meter B",
                   uint32_t feedIntervalMs = 500)
        : a_(std::move(a)), b_(std::move(b)), aName_(std::move(aName)), bName_(std::move(bName)),
          feedMs_(feedIntervalMs) {}

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
    void fillView(CompareView& v) const {
        v.aName = aName_;
        v.bName = bName_;
        const MeterCompareStats s = cmp_.stats();
        v.valid = s.valid;
        v.aWatts = (int16_t)s.aWatts;
        v.bWatts = (int16_t)s.bWatts;
        v.deltaW = (int16_t)s.deltaW;
        v.ratio = s.meanRatio;
        v.biasPct = s.meanBiasPct;
        v.nPairs = (uint16_t)s.nPairs;
        const std::vector<MeterBand> tb = cmp_.torqueBands();
        for (int i = 0; i < CompareView::NBANDS && i < (int)tb.size(); ++i)
            v.bandBiasPct10[i] = tb[i].nPairs > 0 ? (int16_t)(tb[i].meanBiasPct * 10.0f) : INT16_MIN;
    }

    // The GET /compare payload the web Compare view renders.
    std::string json() const { return renderCompareJson(cmp_, aName_, bName_); }

    void reset() { cmp_.reset(); lastFeed_ = 0; }

private:
    Source a_, b_;
    std::string aName_, bName_;
    uint32_t feedMs_;
    uint32_t lastFeed_ = 0;
    MeterCompare cmp_;
};

// ---- B-source adapters (the seam's two sides) -------------------------------------------------
// Bench: derive meter B from meter A by a fixed ratio (B reads ratioMilli/1000 of A). Stands in
// until a real second meter feeds the service; the real side is just a Source over that meter.
inline CompareService::Source scaledSource(CompareService::Source a, const int* ratioMilli) {
    return [a, ratioMilli](uint32_t nowMs) {
        PowerReading r = a ? a(nowMs) : PowerReading{};
        if (r.power_w > 0 && ratioMilli)
            r.power_w = (int16_t)((int)r.power_w * (*ratioMilli) / 1000);
        return r;
    };
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
