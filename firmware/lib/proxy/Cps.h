#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// BLE Cycling Power Service + Device Information Service — UUIDs and the (pure,
// host-testable) measurement codec. The C++ mirror of the Python ANT+ page codec.
namespace sb20proxy {

constexpr const char* UUID_CPS          = "1818";
constexpr const char* UUID_CP_MEAS      = "2A63";  // Cycling Power Measurement (notify)
constexpr const char* UUID_CP_FEATURE   = "2A65";  // Cycling Power Feature (read)
constexpr const char* UUID_CP_SENSORLOC = "2A5D";  // Sensor Location (read)
constexpr const char* UUID_CP_CONTROL   = "2A66";  // Cycling Power Control Point (write+indicate)
constexpr const char* UUID_DIS          = "180A";
constexpr const char* UUID_DIS_MANUF    = "2A29";
constexpr const char* UUID_DIS_MODEL    = "2A24";
constexpr const char* UUID_DIS_FW       = "2A26";
constexpr const char* UUID_DIS_SERIAL   = "2A25";

// CPS Control Point op codes we care about.
constexpr uint8_t CP_OP_START_OFFSET_COMP = 0x0C;
constexpr uint8_t CP_OP_RESPONSE          = 0x20;
constexpr uint8_t CP_RESPONSE_SUCCESS     = 0x01;
constexpr uint8_t CP_RESPONSE_NOT_SUPPORTED = 0x02;

// CPS Measurement flags (uint16). We set only Crank Revolution Data for now; the full
// Stages set (0x2F: pedal balance + accumulated torque + crank rev) is gated on Session G.
constexpr uint16_t CPM_CRANK_REV_DATA_PRESENT = 0x0020;  // flags bit 5
// Data-bearing optional fields that sit BEFORE crank-rev on the wire (balance bit0, accumulated
// torque bit2, wheel-rev bit4). When none are set, crank-rev data is at bytes 4-7 and the simple
// decodeCrankRevs/decodeCrankEventTime offsets are valid (true for our meters' power+cadence frame).
constexpr uint16_t CPM_PRECEDING_DATA_BITS = 0x0015;

// CPS Feature bits (uint32). Crank Revolution Data Supported = bit 3.
constexpr uint32_t CP_FEATURE_CRANK_REV_SUPPORTED = 0x00000008;

// --- The REAL Stages SPM2 crank, captured 2026-06-17 (findings/captures/G-crankL-ble-recon).
// The spoof must reproduce this byte-for-byte: a minimal 0x20 frame paired with the SB20 but
// showed NO power, so the SB20 wants the full Stages frame shape. ---
constexpr uint16_t CPM_PEDAL_BALANCE_PRESENT  = 0x0001;  // bit0
constexpr uint16_t CPM_PEDAL_BALANCE_REF_LEFT = 0x0002;  // bit1 (1 = reference left)
constexpr uint16_t CPM_ACCUM_TORQUE_PRESENT   = 0x0004;  // bit2
constexpr uint16_t CPM_ACCUM_TORQUE_SRC_CRANK = 0x0008;  // bit3 (1 = crank-based)
constexpr uint16_t CPM_WHEEL_REV_DATA_PRESENT = 0x0010;  // bit4
// The captured measurement flags: balance + ref-left + torque + src-crank + crank-rev.
constexpr uint16_t CPM_STAGES_FLAGS = 0x002F;
// The captured CP Feature value (char 0x2A65, raw LE 0b 03 08 00).
constexpr uint32_t CP_FEATURE_STAGES = 0x0008030B;
// The crank reports Sensor Location 0 ("other") — NOT 5 (left crank), despite being a left crank.
constexpr uint8_t SENSOR_LOCATION_OTHER = 0x00;

// Encode a power-only Cycling Power Measurement: flags (uint16 LE) + instantaneous power
// (sint16 LE). Power is ALWAYS the first field after flags, so flags = 0 is valid.
inline std::vector<uint8_t> encodeCpsMeasurement(int16_t power_w) {
    const uint16_t flags = 0x0000;
    return {
        (uint8_t)(flags & 0xFF), (uint8_t)(flags >> 8),
        (uint8_t)(power_w & 0xFF), (uint8_t)((power_w >> 8) & 0xFF),
    };
}

// Encode a Cycling Power Measurement WITH Crank Revolution Data (cadence): flags + power +
// cumulative crank revolutions (uint16 LE) + last crank event time (uint16 LE, 1/1024 s).
// Field order is by ascending flag bit; since we set only the crank-rev bit, the crank
// data sits at bytes 4-7. (When the full Stages flags land, pedal-balance/torque fields
// precede it and these offsets shift — see Session G.)
inline std::vector<uint8_t> encodeCpsMeasurement(int16_t power_w, uint16_t crankRevs,
                                                 uint16_t lastCrankEventTime) {
    const uint16_t flags = CPM_CRANK_REV_DATA_PRESENT;
    return {
        (uint8_t)(flags & 0xFF), (uint8_t)(flags >> 8),
        (uint8_t)(power_w & 0xFF), (uint8_t)((power_w >> 8) & 0xFF),
        (uint8_t)(crankRevs & 0xFF), (uint8_t)((crankRevs >> 8) & 0xFF),
        (uint8_t)(lastCrankEventTime & 0xFF), (uint8_t)((lastCrankEventTime >> 8) & 0xFF),
    };
}

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

// Instantaneous power lives at bytes 2-3 (sint16 LE) regardless of flags.
inline int16_t decodeCpsPower(const uint8_t* d, size_t len) {
    if (len < 4) return 0;
    return (int16_t)(d[2] | (d[3] << 8));
}

inline uint16_t decodeCpsFlags(const uint8_t* d, size_t len) {
    if (len < 2) return 0;
    return (uint16_t)(d[0] | (d[1] << 8));
}

// Crank Revolution Data, valid only when no earlier-bit optional field precedes it (true
// for our crank-rev-only frame). Cumulative revs at 4-5, last crank event time at 6-7.
inline uint16_t decodeCrankRevs(const uint8_t* d, size_t len) {
    if (len < 8) return 0;
    return (uint16_t)(d[4] | (d[5] << 8));
}
inline uint16_t decodeCrankEventTime(const uint8_t* d, size_t len) {
    if (len < 8) return 0;
    return (uint16_t)(d[6] | (d[7] << 8));
}

// Byte offset of the Crank Revolution Data fields within a CPS Measurement of ARBITRARY flag
// layout: they sit after any pedal-balance (bit0, 1B), accumulated-torque (bit2, 2B), and
// wheel-rev (bit4, 6B) fields that precede them. 0 if crank-rev data isn't present. This lets
// us read cadence from a real meter (e.g. the Assioma) whatever optional fields it includes —
// the rigid decodeCrankRevs above only works when nothing precedes the crank-rev fields.
inline size_t crankRevDataOffset(uint16_t flags) {
    if (!(flags & CPM_CRANK_REV_DATA_PRESENT)) return 0;
    size_t off = 4;  // flags(2) + instantaneous power(2)
    if (flags & CPM_PEDAL_BALANCE_PRESENT) off += 1;
    if (flags & CPM_ACCUM_TORQUE_PRESENT) off += 2;
    if (flags & CPM_WHEEL_REV_DATA_PRESENT) off += 6;
    return off;
}

// Crank Revolution Data extracted from a CPS Measurement of any flag layout.
struct CpsCrankData {
    bool present = false;
    uint16_t cumulativeRevs = 0;
    uint16_t lastEventTime = 0;  // 1/1024 s
};
inline CpsCrankData decodeCrankData(const uint8_t* d, size_t len) {
    CpsCrankData c;
    const size_t off = crankRevDataOffset(decodeCpsFlags(d, len));
    if (off == 0 || off + 4 > len) return c;
    c.present = true;
    c.cumulativeRevs = (uint16_t)(d[off] | (d[off + 1] << 8));
    c.lastEventTime = (uint16_t)(d[off + 2] | (d[off + 3] << 8));
    return c;
}

// Recover cadence (rpm) from two Crank Revolution Data samples — what a head unit does.
// uint16 deltas wrap, which is correct. Returns 0 if no event-time elapsed.
inline float cadenceRpmFromCrank(uint16_t revs0, uint16_t t0, uint16_t revs1, uint16_t t1) {
    uint16_t dRevs = (uint16_t)(revs1 - revs0);
    uint16_t dT = (uint16_t)(t1 - t0);
    if (dT == 0) return 0.0f;
    return (float)dRevs * 60.0f * 1024.0f / (float)dT;
}

// The control-point reply to a Start Offset Compensation (zero-reset): success + offset.
inline std::vector<uint8_t> encodeCalibrationResponse(int16_t offset) {
    return {
        CP_OP_RESPONSE, CP_OP_START_OFFSET_COMP, CP_RESPONSE_SUCCESS,
        (uint8_t)(offset & 0xFF), (uint8_t)((offset >> 8) & 0xFF),
    };
}

// Advancing crank-revolution state for the CPS Crank Revolution Data fields (cadence). A
// head unit derives cadence from the DELTA of (cumulative revs, last crank event time)
// between notifications, so we advance the event time by exactly one revolution-period per
// completed revolution — the recovered cadence then equals the input rpm (no quantization
// jitter). Pure + host-tested; the BLE crank just calls advance() then reads the fields.
struct CrankCadence {
    uint16_t cumulativeRevs = 0;
    uint16_t lastEventTime = 0;  // 1/1024 s, wraps ~64 s (head units handle the wrap)
    double pendingRevs = 0.0;    // fractional revolutions not yet emitted

    void advance(float rpm, uint32_t dt_ms) {
        if (rpm <= 0.0f || dt_ms == 0) return;  // coasting / no time: no new events
        pendingRevs += rpm * (dt_ms / 60000.0);
        const double periodTicks = 60.0 * 1024.0 / rpm;  // 1/1024 s per revolution
        while (pendingRevs >= 1.0) {
            pendingRevs -= 1.0;
            cumulativeRevs = (uint16_t)(cumulativeRevs + 1);
            lastEventTime = (uint16_t)(lastEventTime + (uint16_t)(periodTicks + 0.5));
        }
    }
};

}  // namespace sb20proxy
