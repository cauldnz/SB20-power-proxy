#pragma once
#include <stdint.h>

#include <vector>

#include "Correction.h"
#include "Cps.h"
#include "PowerReading.h"
#include "spoofs/StagesSpm2.h"

// The pure read -> correct -> re-frame relay for the nRF bridge.
//
// This is everything `measNotifyCb` used to do inline, minus the two impure calls it was wrapped
// around (`millis()` for the timestamp and `notifyClients()` for the write). Hand it the source
// meter's raw CPS bytes and the current time and it returns the decoded reading, the corrected
// reading, and the exact frame to notify — so the whole protocol path is host-testable with no
// radio, which it was not while it lived in a Bluefruit callback.
//
// WHY THIS IS NOT `sb20proxy::ProxyCore`
// --------------------------------------
// `ProxyCore` is the relay's *middle*: reading -> correction -> publish. It deliberately knows
// nothing about bytes, because on the ESP32 the decode lives in the source adapter and the framing
// lives in `BleCrankPeripheral`. This class is the nRF's two *ends* — decode and re-frame — and it
// exists because those ends behave differently here, not by accident:
//
//   * The ESP32 SYNTHESISES crank revolutions: `BleCrankPeripheral::publishPower` runs a
//     `CadenceState` forward from `cadence_rpm` and a wall-clock delta, generating its own
//     cumulative-rev counter and event time.
//   * The nRF PASSES THROUGH the source meter's own cumulative revs and last-event time.
//
// Both produce a valid Stages 0x2F frame, but the crank-rev series a consumer sees is not the same
// series, so they are not interchangeable and one cannot simply call the other. Keeping the nRF's
// behaviour in its own named, tested module makes that divergence explicit and pinned by tests
// instead of buried in two hardware files where it was host-testable in neither. Unifying them is a
// protocol decision that needs an on-bike A/B (see findings/decisions.md), not a refactor.
namespace nrfbridge {

// One source frame's worth of relay output.
struct RelayOutput {
    sb20proxy::PowerReading source;     // decoded, single-sided doubling applied, pre-correction
    sb20proxy::PowerReading corrected;  // after Correction::apply
    std::vector<uint8_t> frame;         // the bytes to notify on the outgoing CPS measurement
};

class SourceRelay {
public:
    // Drop the carried-over crank BASELINES. Call on a source disconnect: a new meter starts its
    // cumulative-rev counter wherever it likes, and differencing against the old meter's last value
    // injects a bogus revolution delta (and so a bogus torque accumulation).
    //
    // Deliberately does NOT zero accumTorque_. Accumulated torque is a free-running cumulative
    // field in the CPS frame - consumers difference successive values - so resetting it mid-stream
    // is a discontinuity the consumer reads as a huge negative (wrapping) delta. Only the baselines
    // we difference AGAINST are stale after a source swap; the accumulator itself is still valid.
    // (This mirrors the pre-extraction behaviour, which reset g_spoofHavePrev/g_cadHavePrev but left
    // g_spoofAccumTorque alone.)
    void reset() {
        cadHavePrev_ = false;
        cadPrevRevs_ = 0;
        cadPrevEvt_ = 0;
        spoofHavePrev_ = false;
        spoofPrevRevs_ = 0;
        spoofPrevEvt_ = 0;
    }

    // Observability (status/telemetry + tests).
    uint16_t accumTorque() const { return accumTorque_; }

    RelayOutput onFrame(const uint8_t* data, uint16_t len, uint32_t now_ms, bool spoof,
                        bool singleSided, const sb20proxy::Correction& corr) {
        using namespace sb20proxy;
        RelayOutput o;

        PowerReading r;
        r.power_w = decodeCpsPower(data, len);
        const uint16_t flags = decodeCpsFlags(data, len);
        const CpsBalance bal = decodeCpsBalance(data, len);
        r.balance_half_pct = bal.present ? bal.halfPct : -1;
        r.t_ms = now_ms;

        // Cadence (rpm) from the crank-rev delta (last-event ticks are 1/1024 s). Feeds the ANT
        // master so it broadcasts real cadence; 0 = new sample but no new revs (coasting),
        // -1 = the frame carried no crank data at all.
        const bool haveCrank = (flags & CPM_CRANK_REV_DATA_PRESENT) != 0;
        CpsCrankData cd{};
        if (haveCrank) {
            cd = decodeCrankData(data, len);
            if (cadHavePrev_) {
                const uint16_t dRevs = (uint16_t)(cd.cumulativeRevs - cadPrevRevs_);
                const uint16_t dTicks = (uint16_t)(cd.lastEventTime - cadPrevEvt_);
                if (dRevs > 0 && dTicks > 0)
                    r.cadence_rpm = (int16_t)((float)dRevs * 1024.0f * 60.0f / (float)dTicks + 0.5f);
                else if (dRevs == 0)
                    r.cadence_rpm = 0;  // coasting
            }
            cadPrevRevs_ = cd.cumulativeRevs;
            cadPrevEvt_ = cd.lastEventTime;
            cadHavePrev_ = true;
        }

        // Single-sided x2: a left/right-only crank reports half of total; double it BEFORE the
        // correction so scale/curve operate on total power (ESP32 semantics).
        if (singleSided) r.power_w = (int16_t)(r.power_w * 2);
        o.source = r;

        const PowerReading out = corr.apply(r);  // curve wins over scale/offset when populated
        o.corrected = out;

        if (spoof) {
            // SB20 crank spoof: re-frame as the Stages 0x2F measurement. Crank-rev fields pass
            // through from the SOURCE meter; accumulated torque is integrated per completed
            // revolution from the CORRECTED power.
            uint16_t curRevs = 0, curEvt = 0;
            if (haveCrank) {
                curRevs = cd.cumulativeRevs;
                curEvt = cd.lastEventTime;
                if (spoofHavePrev_ && out.power_w > 0) {
                    const uint16_t dRevs = (uint16_t)(curRevs - spoofPrevRevs_);
                    const uint16_t dTicks = (uint16_t)(curEvt - spoofPrevEvt_);
                    if (dRevs > 0 && dTicks > 0) {
                        // rpm from the source crank delta (1/1024 s ticks), then accumulated
                        // torque in 1/32 Nm units per rev: T = P*60 / (2*pi*rpm).
                        const float rpm = (float)dRevs * 1024.0f * 60.0f / (float)dTicks;
                        const float torqueNm = (float)out.power_w * 60.0f / (6.2831853f * rpm);
                        accumTorque_ = (uint16_t)(accumTorque_ +
                                                  (uint16_t)((float)dRevs * torqueNm * 32.0f + 0.5f));
                    }
                }
                spoofPrevRevs_ = curRevs;
                spoofPrevEvt_ = curEvt;
                spoofHavePrev_ = true;
            }
            // Forward the source's real left-referenced L/R split; 100 (=50 %) when it has none.
            const uint8_t balanceOut =
                (r.balance_half_pct >= 0) ? (uint8_t)r.balance_half_pct : (uint8_t)100;
            o.frame = encodeStagesCpsMeasurement(out.power_w, balanceOut, accumTorque_, curRevs,
                                                 curEvt);
        } else if (haveCrank) {
            o.frame = encodeCpsMeasurement(out.power_w, cd.cumulativeRevs, cd.lastEventTime);
        } else {
            o.frame = encodeCpsMeasurement(out.power_w);
        }
        return o;
    }

private:
    // General cadence tracker (both modes).
    uint16_t cadPrevRevs_ = 0;
    uint16_t cadPrevEvt_ = 0;
    bool cadHavePrev_ = false;

    // Stages-spoof (0x2F) accumulated torque + the previous source crank sample it integrates
    // against. Only touched in spoof mode.
    uint16_t accumTorque_ = 0;
    uint16_t spoofPrevRevs_ = 0;
    uint16_t spoofPrevEvt_ = 0;
    bool spoofHavePrev_ = false;
};

}  // namespace nrfbridge
