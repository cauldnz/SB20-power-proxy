#pragma once
#include "../Cps.h"

// ONE spoof target: the Stages SPM2 left crank, as measured on the wire.
//
// Everything here is a CAPTURED value or a captured frame shape — not a spec default. It lives
// apart from `Cps.h` (the general Cycling Power Service codec) so the codec stays honest about
// what is standard and what is one meter's observed behaviour. `Cps.h` does not include this
// header; the dependency runs one way only, so a build that never spoofs a Stages crank (the
// meter-to-meter corrector — see findings/meter-to-meter-proxy.md) never compiles these bytes in.
//
// Source capture: findings/captures/G-crankL-ble-recon (2026-06-17). The spoof must reproduce
// this byte-for-byte: a minimal 0x20 frame paired with the SB20 but showed NO power, so the SB20
// wants the full Stages frame shape.
//
// Adding a second spoof target? Add `spoofs/<Target>.h` beside this one rather than growing
// `Cps.h` — that is the whole point of the split.
namespace sb20proxy {

// The captured measurement flags: balance + ref-left + torque + src-crank + crank-rev.
// (The individual bits are generic CPS spec bits and stay in `Cps.h`; the specific COMBINATION
// below is what this crank was observed to send.)
constexpr uint16_t CPM_STAGES_FLAGS = 0x002F;

// The captured CP Feature value (char 0x2A65, raw LE 0b 03 08 00).
constexpr uint32_t CP_FEATURE_STAGES = 0x0008030B;

// The crank reports Sensor Location 0 ("other") — NOT 5 (left crank), despite being a left crank.
constexpr uint8_t SENSOR_LOCATION_OTHER = 0x00;

// Encode the REAL Stages SPM2 measurement (flags 0x2F), byte-identical to the capture:
//   flags(2) | power sint16 | balance uint8 (1/2 %) | accum torque uint16 (1/32 Nm) |
//   cumulative crank revs uint16 | last crank event time uint16 (1/1024 s)  = 11 bytes.
// Field order is fixed by ascending flag bit, matching how the real crank lays it out on the
// wire; the SB20 parses power from this exact shape. Golden-tested against the captured bytes.
inline std::vector<uint8_t> encodeStagesCpsMeasurement(int16_t power_w, uint8_t balanceHalfPct,
                                                       uint16_t accumTorque, uint16_t crankRevs,
                                                       uint16_t lastCrankEventTime) {
    const uint16_t flags = CPM_STAGES_FLAGS;
    return {
        (uint8_t)(flags & 0xFF),     (uint8_t)(flags >> 8),
        (uint8_t)(power_w & 0xFF),   (uint8_t)((power_w >> 8) & 0xFF),
        balanceHalfPct,
        (uint8_t)(accumTorque & 0xFF), (uint8_t)((accumTorque >> 8) & 0xFF),
        (uint8_t)(crankRevs & 0xFF),   (uint8_t)((crankRevs >> 8) & 0xFF),
        (uint8_t)(lastCrankEventTime & 0xFF), (uint8_t)((lastCrankEventTime >> 8) & 0xFF),
    };
}

}  // namespace sb20proxy
