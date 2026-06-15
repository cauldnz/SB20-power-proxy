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

// Encode a Cycling Power Measurement: flags (uint16 LE) + instantaneous power
// (sint16 LE). Power is ALWAYS the first field after flags, so a power-only frame
// (flags = 0) is valid. TODO: match the real Stages flags (0x2F: pedal balance +
// accumulated torque + crank-revolution data for cadence).
inline std::vector<uint8_t> encodeCpsMeasurement(int16_t power_w) {
    const uint16_t flags = 0x0000;
    return {
        (uint8_t)(flags & 0xFF), (uint8_t)(flags >> 8),
        (uint8_t)(power_w & 0xFF), (uint8_t)((power_w >> 8) & 0xFF),
    };
}

// Instantaneous power lives at bytes 2-3 (sint16 LE) regardless of flags.
inline int16_t decodeCpsPower(const uint8_t* d, size_t len) {
    if (len < 4) return 0;
    return (int16_t)(d[2] | (d[3] << 8));
}

// The control-point reply to a Start Offset Compensation (zero-reset): success + offset.
inline std::vector<uint8_t> encodeCalibrationResponse(int16_t offset) {
    return {
        CP_OP_RESPONSE, CP_OP_START_OFFSET_COMP, CP_RESPONSE_SUCCESS,
        (uint8_t)(offset & 0xFF), (uint8_t)((offset >> 8) & 0xFF),
    };
}

}  // namespace sb20proxy
