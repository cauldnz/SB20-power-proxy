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
constexpr const char* UUID_DIS_SERIAL   = "2A25";

// CPS Control Point op codes we care about.
constexpr uint8_t CP_OP_START_OFFSET_COMP = 0x0C;
constexpr uint8_t CP_OP_RESPONSE          = 0x20;
constexpr uint8_t CP_RESPONSE_SUCCESS     = 0x01;
constexpr uint8_t CP_RESPONSE_NOT_SUPPORTED = 0x02;

// CPS Measurement flags (uint16). We set only Crank Revolution Data for now; the full
// Stages set (0x2F: pedal balance + accumulated torque + crank rev) is gated on Session G.
constexpr uint16_t CPM_CRANK_REV_DATA_PRESENT = 0x0020;  // flags bit 5

// CPS Feature bits (uint32). Crank Revolution Data Supported = bit 3.
constexpr uint32_t CP_FEATURE_CRANK_REV_SUPPORTED = 0x00000008;

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
