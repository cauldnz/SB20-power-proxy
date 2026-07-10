#pragma once
// ANT+ Bicycle Power page codec (profile D00001086) — the C++ mirror of the Phase-0-validated Python
// codec `code/src/sb20proxy/ant/pages.py`. It ENCODES the 8-byte broadcast pages the nRF sends as an
// ANT+ master (power-only 0x10, crank-torque 0x12, common 0x50/0x51/0x52, calibration 0x01) and DECODES
// an incoming ANT+ power meter's pages for the source-read path. Pure + header-only (no SoftDevice, no
// Arduino) → host-tested in CI, byte-for-byte against the same golden vectors as the Python.
//
// Byte offsets are NOT from memory — they mirror pages.py, itself round-tripped against 3,209 real
// Stages records (`encode(decode(raw)) == raw`). Reserved positions were verified constant 0xFF on air;
// the encoders refill them so a re-broadcast is byte-identical. Do not "simplify" an offset.
#include <cstddef>
#include <cstdint>

namespace nrfant {

// Page IDs (mask the page byte with 0x7F before matching — bit 7 is a toggle in some profiles).
inline constexpr uint8_t PAGE_CALIBRATION = 0x01;
inline constexpr uint8_t PAGE_POWER_ONLY = 0x10;
inline constexpr uint8_t PAGE_CRANK_TORQUE = 0x12;
inline constexpr uint8_t PAGE_TORQUE_EFFECTIVENESS = 0x13;
inline constexpr uint8_t PAGE_MANUFACTURER_INFO = 0x50;
inline constexpr uint8_t PAGE_PRODUCT_INFO = 0x51;
inline constexpr uint8_t PAGE_BATTERY_STATUS = 0x52;
inline constexpr uint8_t ANT_RESERVED = 0xFF;  // the fill byte for every reserved position

// Calibration response IDs (page 0x01, byte 1).
inline constexpr uint8_t CAL_ID_MANUAL_ZERO_SUCCESS = 0xAC;
inline constexpr uint8_t CAL_ID_MANUAL_ZERO_FAIL = 0xAF;
inline constexpr uint8_t CAL_ID_MANUAL_ZERO_REQUEST = 0xAA;

inline void putU16le(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void putU24le(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); }
inline void putU32le(uint8_t* p, uint32_t v) { putU16le(p, (uint16_t)v); putU16le(p + 2, (uint16_t)(v >> 16)); }
inline uint16_t rdU16le(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// Page 0x10 pedal-power (balance) byte: out-of-range → 0xFF (not provided); else (balance & 0x7F) plus
// bit7 = differentiated (left/right known). Mirrors pages.pedal_power_byte.
inline uint8_t pedalPowerByte(int balancePct, bool differentiated = true) {
    if (balancePct < 0 || balancePct > 100) return ANT_RESERVED;
    return (uint8_t)((balancePct & 0x7F) | (differentiated ? 0x80 : 0x00));
}

// ---- Encoders (fill out[8], return 8). cadenceRpm < 0 or > 255 → 0xFF (none). ----
inline size_t encodePowerOnly(uint8_t eventCount, uint16_t powerW, uint16_t accumPower,
                              int cadenceRpm, uint8_t pedalPowerRaw, uint8_t out[8]) {
    out[0] = PAGE_POWER_ONLY;
    out[1] = eventCount;
    out[2] = pedalPowerRaw;
    out[3] = (cadenceRpm < 0 || cadenceRpm > 255) ? ANT_RESERVED : (uint8_t)cadenceRpm;
    putU16le(out + 4, accumPower);
    putU16le(out + 6, powerW);
    return 8;
}
inline size_t encodeCrankTorque(uint8_t eventCount, uint8_t crankTicks, uint16_t accumPeriod,
                                uint16_t accumTorque, int cadenceRpm, uint8_t out[8]) {
    out[0] = PAGE_CRANK_TORQUE;
    out[1] = eventCount;
    out[2] = crankTicks;
    out[3] = (cadenceRpm < 0 || cadenceRpm > 255) ? ANT_RESERVED : (uint8_t)cadenceRpm;
    putU16le(out + 4, accumPeriod);
    putU16le(out + 6, accumTorque);
    return 8;
}
inline size_t encodeManufacturerInfo(uint8_t hwRev, uint16_t mfgId, uint16_t model, uint8_t out[8]) {
    out[0] = PAGE_MANUFACTURER_INFO;
    out[1] = ANT_RESERVED;
    out[2] = ANT_RESERVED;
    out[3] = hwRev;
    putU16le(out + 4, mfgId);
    putU16le(out + 6, model);
    return 8;
}
inline size_t encodeProductInfo(uint8_t swMain, uint32_t serial, uint8_t swSupp, uint8_t out[8]) {
    out[0] = PAGE_PRODUCT_INFO;
    out[1] = ANT_RESERVED;
    out[2] = swSupp;
    out[3] = swMain;
    putU32le(out + 4, serial);
    return 8;
}
inline size_t encodeBatteryStatus(uint8_t batteryId, uint32_t opTimeLsb, uint8_t voltFrac,
                                  uint8_t statusByte, uint8_t out[8]) {
    out[0] = PAGE_BATTERY_STATUS;
    out[1] = ANT_RESERVED;
    out[2] = batteryId;
    putU24le(out + 3, opTimeLsb);
    out[6] = voltFrac;
    out[7] = statusByte;
    return 8;
}
// Page 0x01 calibration response. Success default (0xAC); calData is the offset (e.g. 903 → `87 03`),
// FAIL is 0xAF. The bike accepts the captured `01 AC FF FF FF FF 87 03` for offset 903.
inline size_t encodeCalibrationResponse(int16_t calData, uint8_t calId, uint8_t autoZero, uint8_t out[8]) {
    out[0] = PAGE_CALIBRATION;
    out[1] = calId;
    out[2] = autoZero;
    out[3] = ANT_RESERVED;
    out[4] = ANT_RESERVED;
    out[5] = ANT_RESERVED;
    putU16le(out + 6, (uint16_t)calData);
    return 8;
}
// Manual-zero REQUEST (display → meter): `01 AA FF FF FF FF FF FF` (no data, calData = -1 = 0xFFFF).
inline size_t encodeCalibrationRequest(uint8_t out[8]) {
    return encodeCalibrationResponse(-1, CAL_ID_MANUAL_ZERO_REQUEST, ANT_RESERVED, out);
}

// ---- Decoder (source read). Decodes the power-relevant pages an ANT+ meter broadcasts. ----
struct AntPowerReading {
    bool valid = false;         // a recognised power/crank-torque page was decoded
    uint8_t page = 0;           // raw page byte (toggle preserved)
    uint8_t eventCount = 0;
    int instantaneousPowerW = -1;  // page 0x10 only; -1 otherwise
    int cadenceRpm = -1;           // -1 = none (0xFF on the wire)
    int balancePct = -1;           // -1 = none / undifferentiated
    uint16_t accumulatedPower = 0; // page 0x10
    // crank-torque (0x12) raw accumulators — power is derived by the caller across events (stateful):
    // instantaneousPowerW stays -1 for 0x12.
    uint8_t crankTicks = 0;
    uint16_t accumulatedCrankPeriod = 0;
    uint16_t accumulatedTorque = 0;
};
inline AntPowerReading decodePage(const uint8_t* data, size_t len) {
    AntPowerReading r;
    if (len < 8) return r;
    r.page = data[0];
    const uint8_t pm = data[0] & 0x7F;
    if (pm == PAGE_POWER_ONLY) {
        r.valid = true;
        r.eventCount = data[1];
        if (data[2] != 0xFF) r.balancePct = data[2] & 0x7F;
        if (data[3] != 0xFF) r.cadenceRpm = data[3];
        r.accumulatedPower = rdU16le(data + 4);
        r.instantaneousPowerW = rdU16le(data + 6);
    } else if (pm == PAGE_CRANK_TORQUE) {
        r.valid = true;
        r.eventCount = data[1];
        r.crankTicks = data[2];
        if (data[3] != 0xFF) r.cadenceRpm = data[3];
        r.accumulatedCrankPeriod = rdU16le(data + 4);
        r.accumulatedTorque = rdU16le(data + 6);
    }
    return r;
}

}  // namespace nrfant
