#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// BLE Fitness Machine Service (0x1826) — UUIDs + the pure, host-testable "indoor bike"
// codec. The C++ mirror of the Python sb20proxy.ble.ftms; Indoor Bike Data + the Control
// Point (erg = Set Target Power) + Feature/Power-Range/Status.
//
// SPEC-BUILT, pending real-capture validation (Session 4 Part C, capture_ftms.py --erg):
// the wire layout is the public Bluetooth FTMS spec, NOT yet pinned by captured SB20
// frames. When those land they become the golden source and real data wins.
namespace sb20proxy {

constexpr const char* UUID_FTMS               = "1826";
constexpr const char* UUID_INDOOR_BIKE_DATA   = "2AD2";  // notify
constexpr const char* UUID_FTMS_FEATURE       = "2ACC";  // read
constexpr const char* UUID_SUPPORTED_POWER_RANGE = "2AD8";  // read
constexpr const char* UUID_FTMS_CONTROL_POINT = "2AD9";  // write + indicate
constexpr const char* UUID_FTMS_STATUS        = "2ADA";  // notify

// --- Indoor Bike Data flags (uint16). NOTE bit0 inversion: speed present when bit0 == 0. ---
constexpr uint16_t IBD_MORE_DATA      = 1 << 0;  // speed present when CLEAR
constexpr uint16_t IBD_AVG_SPEED      = 1 << 1;
constexpr uint16_t IBD_INST_CADENCE   = 1 << 2;
constexpr uint16_t IBD_AVG_CADENCE    = 1 << 3;
constexpr uint16_t IBD_TOTAL_DISTANCE = 1 << 4;
constexpr uint16_t IBD_RESISTANCE     = 1 << 5;
constexpr uint16_t IBD_INST_POWER     = 1 << 6;
constexpr uint16_t IBD_AVG_POWER      = 1 << 7;

// --- Control Point (0x2AD9) op codes ---
constexpr uint8_t FTMS_CP_REQUEST_CONTROL  = 0x00;
constexpr uint8_t FTMS_CP_RESET            = 0x01;
constexpr uint8_t FTMS_CP_SET_TARGET_POWER = 0x05;  // + sint16 LE watts  (erg)
constexpr uint8_t FTMS_CP_START_RESUME     = 0x07;
constexpr uint8_t FTMS_CP_STOP_PAUSE       = 0x08;  // + uint8 (0x01 stop / 0x02 pause)
constexpr uint8_t FTMS_CP_RESPONSE         = 0x80;
constexpr uint8_t FTMS_CP_SUCCESS              = 0x01;
constexpr uint8_t FTMS_CP_OP_NOT_SUPPORTED     = 0x02;
constexpr uint8_t FTMS_CP_INVALID_PARAMETER    = 0x03;
constexpr uint8_t FTMS_CP_CONTROL_NOT_PERMITTED = 0x05;

// --- Feature bits we use ---
constexpr uint32_t FTMS_FEAT_CADENCE       = 1u << 1;
constexpr uint32_t FTMS_FEAT_POWER_MEAS    = 1u << 14;
constexpr uint32_t FTMS_TGT_POWER          = 1u << 3;   // Power Target Setting Supported (erg)
constexpr uint32_t FTMS_TGT_INDOOR_BIKE_SIM = 1u << 13;

// --- Status (0x2ADA) op codes we parse ---
constexpr uint8_t FTMS_ST_STOPPED_PAUSED        = 0x02;
constexpr uint8_t FTMS_ST_STARTED_RESUMED       = 0x04;
constexpr uint8_t FTMS_ST_TARGET_POWER_CHANGED  = 0x08;  // + sint16 watts
constexpr uint8_t FTMS_ST_CONTROL_PERMISSION_LOST = 0xFF;

// ============================ Indoor Bike Data ============================

struct IndoorBikeData {
    uint16_t flags = 0;
    bool hasSpeed = false;   uint16_t instSpeed = 0;    // 1/100 km/h
    bool hasCadence = false; uint16_t instCadence = 0;  // 1/2 rpm
    bool hasPower = false;   int16_t instPower = 0;     // W
    bool hasAvgPower = false; int16_t avgPower = 0;     // W
    float speedKmh() const { return instSpeed / 100.0f; }
    float cadenceRpm() const { return instCadence / 2.0f; }
};

// Decode an Indoor Bike Data notification. Walks every field in flag order so that power /
// cadence land at the right offset even behind fields we don't keep. Lenient on short
// frames (returns what it has) — mirrors the firmware's defensive CPS decode.
inline IndoorBikeData decodeIndoorBikeData(const uint8_t* d, size_t len) {
    IndoorBikeData o;
    if (d == nullptr || len < 2) return o;
    o.flags = (uint16_t)(d[0] | (d[1] << 8));
    size_t i = 2;
    auto u16 = [&]() -> uint16_t {
        uint16_t v = (i + 2 <= len) ? (uint16_t)(d[i] | (d[i + 1] << 8)) : 0;
        i += 2;
        return v;
    };
    if (!(o.flags & IBD_MORE_DATA)) { o.hasSpeed = true; o.instSpeed = u16(); }  // speed if bit0 clear
    if (o.flags & IBD_AVG_SPEED) { (void)u16(); }
    if (o.flags & IBD_INST_CADENCE) { o.hasCadence = true; o.instCadence = u16(); }
    if (o.flags & IBD_AVG_CADENCE) { (void)u16(); }
    if (o.flags & IBD_TOTAL_DISTANCE) { i += 3; }
    if (o.flags & IBD_RESISTANCE) { (void)u16(); }
    if (o.flags & IBD_INST_POWER) { o.hasPower = true; o.instPower = (int16_t)u16(); }
    if (o.flags & IBD_AVG_POWER) { o.hasAvgPower = true; o.avgPower = (int16_t)u16(); }
    return o;
}

// Encode an Indoor Bike Data notification for the trainer-server: instantaneous power +
// cadence, with optional instantaneous speed. Field order is by ascending flag bit, and the
// More-Data bit0 is set only when speed is omitted (the spec's inverted speed-present rule).
inline std::vector<uint8_t> encodeIndoorBikeData(int16_t power_w, float cadenceRpm,
                                                 bool haveSpeed = false, float speedKmh = 0.0f) {
    uint16_t flags = 0;
    std::vector<uint8_t> body;
    if (!haveSpeed) {
        flags |= IBD_MORE_DATA;
    } else {
        uint16_t s = (uint16_t)(speedKmh * 100.0f + 0.5f);
        body.push_back((uint8_t)(s & 0xFF));
        body.push_back((uint8_t)(s >> 8));
    }
    flags |= IBD_INST_CADENCE;
    uint16_t c = (uint16_t)(cadenceRpm * 2.0f + 0.5f);
    body.push_back((uint8_t)(c & 0xFF));
    body.push_back((uint8_t)(c >> 8));
    flags |= IBD_INST_POWER;
    body.push_back((uint8_t)(power_w & 0xFF));
    body.push_back((uint8_t)((power_w >> 8) & 0xFF));

    std::vector<uint8_t> out = {(uint8_t)(flags & 0xFF), (uint8_t)(flags >> 8)};
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

// ============================ Control Point ============================

inline std::vector<uint8_t> encodeRequestControl() { return {FTMS_CP_REQUEST_CONTROL}; }
inline std::vector<uint8_t> encodeStart()          { return {FTMS_CP_START_RESUME}; }
inline std::vector<uint8_t> encodeReset()          { return {FTMS_CP_RESET}; }
inline std::vector<uint8_t> encodeStop(bool pause = false) {
    return {FTMS_CP_STOP_PAUSE, (uint8_t)(pause ? 0x02 : 0x01)};
}
// The erg op: Set Target Power 0x05 + sint16 LE watts.
inline std::vector<uint8_t> encodeSetTargetPower(int16_t watts) {
    return {FTMS_CP_SET_TARGET_POWER, (uint8_t)(watts & 0xFF), (uint8_t)((watts >> 8) & 0xFF)};
}
// The indication a machine returns: 0x80 <req-op> <result>.
inline std::vector<uint8_t> encodeControlPointResponse(uint8_t reqOp, uint8_t result = FTMS_CP_SUCCESS) {
    return {FTMS_CP_RESPONSE, reqOp, result};
}

struct FtmsCpMessage {
    bool valid = false;
    bool isResponse = false;
    uint8_t opcode = 0;          // request op (a write), or the response-code (0x80)
    uint8_t requestOpcode = 0;   // responses only
    uint8_t result = 0;          // responses only
    bool hasTargetPower = false; // a Set Target Power write
    int16_t targetPower = 0;     // watts
    bool success() const { return isResponse && result == FTMS_CP_SUCCESS; }
};

// Decode a control-point value: 0x80-prefixed -> response; otherwise a write request.
inline FtmsCpMessage decodeControlPoint(const uint8_t* d, size_t len) {
    FtmsCpMessage m;
    if (d == nullptr || len == 0) return m;
    m.valid = true;
    if (d[0] == FTMS_CP_RESPONSE && len >= 3) {
        m.isResponse = true;
        m.requestOpcode = d[1];
        m.result = d[2];
        return m;
    }
    m.opcode = d[0];
    if (m.opcode == FTMS_CP_SET_TARGET_POWER && len >= 3) {
        m.hasTargetPower = true;
        m.targetPower = (int16_t)(d[1] | (d[2] << 8));
    }
    return m;
}

// ============================ Feature / range / status ============================

inline std::vector<uint8_t> encodeFitnessMachineFeature(uint32_t machine, uint32_t target) {
    return {
        (uint8_t)(machine & 0xFF), (uint8_t)((machine >> 8) & 0xFF),
        (uint8_t)((machine >> 16) & 0xFF), (uint8_t)((machine >> 24) & 0xFF),
        (uint8_t)(target & 0xFF), (uint8_t)((target >> 8) & 0xFF),
        (uint8_t)((target >> 16) & 0xFF), (uint8_t)((target >> 24) & 0xFF),
    };
}

struct FtmsFeature {
    uint32_t machine = 0;
    uint32_t target = 0;
    bool powerMeasurement() const { return (machine & FTMS_FEAT_POWER_MEAS) != 0; }
    bool cadence() const { return (machine & FTMS_FEAT_CADENCE) != 0; }
    bool powerTargetSetting() const { return (target & FTMS_TGT_POWER) != 0; }  // erg capability
};
inline FtmsFeature decodeFitnessMachineFeature(const uint8_t* d, size_t len) {
    FtmsFeature f;
    if (d == nullptr || len < 8) return f;
    f.machine = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    f.target = (uint32_t)d[4] | ((uint32_t)d[5] << 8) | ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24);
    return f;
}

struct FtmsPowerRange {
    int16_t minimum = 0;
    int16_t maximum = 0;
    uint16_t increment = 0;
    int16_t clamp(int16_t w) const {
        if (w < minimum) return minimum;
        if (w > maximum) return maximum;
        return w;
    }
};
inline std::vector<uint8_t> encodeSupportedPowerRange(int16_t mn, int16_t mx, uint16_t inc) {
    return {
        (uint8_t)(mn & 0xFF), (uint8_t)((mn >> 8) & 0xFF),
        (uint8_t)(mx & 0xFF), (uint8_t)((mx >> 8) & 0xFF),
        (uint8_t)(inc & 0xFF), (uint8_t)((inc >> 8) & 0xFF),
    };
}
inline FtmsPowerRange decodeSupportedPowerRange(const uint8_t* d, size_t len) {
    FtmsPowerRange r;
    if (d == nullptr || len < 6) return r;
    r.minimum = (int16_t)(d[0] | (d[1] << 8));
    r.maximum = (int16_t)(d[2] | (d[3] << 8));
    r.increment = (uint16_t)(d[4] | (d[5] << 8));
    return r;
}

struct FtmsStatus {
    uint8_t opcode = 0;
    bool hasTargetPower = false;
    int16_t targetPower = 0;
};
inline FtmsStatus decodeFitnessMachineStatus(const uint8_t* d, size_t len) {
    FtmsStatus s;
    if (d == nullptr || len < 1) return s;
    s.opcode = d[0];
    if (s.opcode == FTMS_ST_TARGET_POWER_CHANGED && len >= 3) {
        s.hasTargetPower = true;
        s.targetPower = (int16_t)(d[1] | (d[2] << 8));
    }
    return s;
}

}  // namespace sb20proxy
