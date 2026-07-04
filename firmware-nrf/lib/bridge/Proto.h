#pragma once
// Proto — the pure pack/unpack for the Bridge GATT contract (see ../../GATT.md, the authority).
// No Arduino, no SoftDevice: host-testable, and the byte-for-byte reference for the Web
// Bluetooth (JS) and Connect IQ (Monkey C) mirrors. All fields little-endian.
#include <cstdint>
#include <cstring>

namespace nrfbridge {

constexpr uint8_t PROTO_VER = 1;

// ---- Status (notify, 20 bytes) --------------------------------------------------------------
struct StatusPacket {
    bool srcConnected = false;
    bool outAdvertising = false;
    bool recording = false;
    bool srcIsAnt = false;
    bool outIsAnt = false;
    int16_t srcPowerW = -1;
    int16_t outPowerW = -1;
    int16_t cadenceRpm = -1;
    int8_t balancePct = -1;     // left %, -1 none
    uint8_t batteryPct = 0xFF;  // 0xFF unknown
    uint16_t scaleMilli = 1000; // correction scale x1000
    int16_t offsetDeciW = 0;    // correction offset x10
    uint32_t recSamples = 0;
    uint16_t uptimeS = 0;
};
constexpr size_t STATUS_LEN = 20;

inline void packU16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void packU32(uint8_t* p, uint32_t v) { packU16(p, (uint16_t)v); packU16(p + 2, (uint16_t)(v >> 16)); }
inline uint16_t unpackU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t unpackU32(const uint8_t* p) { return unpackU16(p) | ((uint32_t)unpackU16(p + 2) << 16); }

inline size_t packStatus(const StatusPacket& s, uint8_t out[STATUS_LEN]) {
    out[0] = PROTO_VER;
    out[1] = (uint8_t)((s.srcConnected ? 1 : 0) | (s.outAdvertising ? 2 : 0) |
                       (s.recording ? 4 : 0) | (s.srcIsAnt ? 8 : 0) | (s.outIsAnt ? 16 : 0));
    packU16(out + 2, (uint16_t)s.srcPowerW);
    packU16(out + 4, (uint16_t)s.outPowerW);
    packU16(out + 6, (uint16_t)s.cadenceRpm);
    out[8] = (uint8_t)s.balancePct;
    out[9] = s.batteryPct;
    packU16(out + 10, s.scaleMilli);
    packU16(out + 12, (uint16_t)s.offsetDeciW);
    packU32(out + 14, s.recSamples);
    packU16(out + 18, s.uptimeS);
    return STATUS_LEN;
}

// ---- Config (read/write, 44 bytes) ----------------------------------------------------------
constexpr size_t CFG_NAME_LEN = 19;
struct ConfigPacket {
    bool srcIsAnt = false;
    bool outIsAnt = false;
    uint16_t scaleMilli = 1000;
    int16_t offsetDeciW = 0;
    char srcFilter[CFG_NAME_LEN + 1] = {0};  // NUL-terminated in the struct, NUL-padded on the wire
    char outName[CFG_NAME_LEN + 1] = {0};
};
constexpr size_t CONFIG_LEN = 44;

inline size_t packConfig(const ConfigPacket& c, uint8_t out[CONFIG_LEN]) {
    out[0] = PROTO_VER;
    out[1] = (uint8_t)((c.srcIsAnt ? 1 : 0) | (c.outIsAnt ? 2 : 0));
    packU16(out + 2, c.scaleMilli);
    packU16(out + 4, (uint16_t)c.offsetDeciW);
    memset(out + 6, 0, CFG_NAME_LEN);
    memcpy(out + 6, c.srcFilter, strnlen(c.srcFilter, CFG_NAME_LEN));
    memset(out + 25, 0, CFG_NAME_LEN);
    memcpy(out + 25, c.outName, strnlen(c.outName, CFG_NAME_LEN));
    return CONFIG_LEN;
}

// Parse a written Config payload. Returns false (leaving `c` untouched) on bad version/length.
inline bool unpackConfig(const uint8_t* p, size_t len, ConfigPacket& c) {
    if (len < CONFIG_LEN || p[0] != PROTO_VER) return false;
    ConfigPacket n;
    n.srcIsAnt = (p[1] & 1) != 0;
    n.outIsAnt = (p[1] & 2) != 0;
    n.scaleMilli = unpackU16(p + 2);
    n.offsetDeciW = (int16_t)unpackU16(p + 4);
    memcpy(n.srcFilter, p + 6, CFG_NAME_LEN);
    n.srcFilter[CFG_NAME_LEN] = 0;
    memcpy(n.outName, p + 25, CFG_NAME_LEN);
    n.outName[CFG_NAME_LEN] = 0;
    // sanity: scale within 0.5..2.0, offset within +/-100 W (matches the ESP32 corrector limits)
    if (n.scaleMilli < 500 || n.scaleMilli > 2000) return false;
    if (n.offsetDeciW < -1000 || n.offsetDeciW > 1000) return false;
    c = n;
    return true;
}

// ---- RecCtl ----------------------------------------------------------------------------------
enum class RecCmd : uint8_t { Stop = 0, Start = 1, Erase = 2, Download = 3, SetRate = 4 };
enum class RecState : uint8_t { Idle = 0, Recording = 1, Downloading = 2 };

struct RecCtlWrite {
    RecCmd cmd = RecCmd::Stop;
    uint8_t rateHz = 52;  // only for SetRate
};
inline bool unpackRecCtl(const uint8_t* p, size_t len, RecCtlWrite& w) {
    if (len < 2 || p[0] != PROTO_VER) return false;
    if (p[1] > (uint8_t)RecCmd::SetRate) return false;
    w.cmd = (RecCmd)p[1];
    if (w.cmd == RecCmd::SetRate) {
        if (len < 3) return false;
        const uint8_t r = p[2];
        if (r != 13 && r != 26 && r != 52 && r != 104) return false;
        w.rateHz = r;
    }
    return true;
}

constexpr size_t RECSTATE_LEN = 12;
inline size_t packRecState(RecState st, uint8_t rateHz, uint32_t samples, uint32_t capacity,
                           uint8_t out[RECSTATE_LEN]) {
    out[0] = PROTO_VER;
    out[1] = (uint8_t)st;
    out[2] = rateHz;
    out[3] = 0;
    packU32(out + 4, samples);
    packU32(out + 8, capacity);
    return RECSTATE_LEN;
}

// ---- RecData framing --------------------------------------------------------------------------
// One IMU sample on the wire: 6 x i16 (ax ay az gx gy gz), 12 bytes.
// Every frame is [ver, TYPE, ...] with an EXPLICIT type byte — v1 put the data frames' seq
// low-byte where the type tag lives, so sequence 254 (0xFE) masqueraded as the trailer and
// truncated the download at exactly frame 254 (bench, 2026-07-05). Types: 0xFF header ·
// 0xFD data · 0xFE trailer.
constexpr size_t SAMPLE_LEN = 12;
constexpr uint8_t REC_FRAME_HEADER = 0xFF;
constexpr uint8_t REC_FRAME_DATA = 0xFD;
constexpr uint8_t REC_FRAME_TRAILER = 0xFE;
constexpr size_t DATA_FRAME_OVERHEAD = 6;      // ver, type, seq u16, count u8, reserved
constexpr size_t DATA_SAMPLES_PER_FRAME = 14;  // 6 + 14*12 = 174 <= 180-byte payload budget

inline size_t packRecHeader(uint8_t rateHz, uint32_t samples, uint32_t startMs, uint8_t out[12]) {
    out[0] = PROTO_VER;
    out[1] = REC_FRAME_HEADER;
    out[2] = rateHz;
    out[3] = 0;
    packU32(out + 4, samples);
    packU32(out + 8, startMs);
    return 12;
}
inline size_t packRecDataFrame(uint16_t seq, const int16_t* samples, size_t nSamples,
                               uint8_t* out /* >= DATA_FRAME_OVERHEAD + n*12 */) {
    out[0] = PROTO_VER;
    out[1] = REC_FRAME_DATA;
    packU16(out + 2, seq);
    out[4] = (uint8_t)nSamples;
    out[5] = 0;
    for (size_t i = 0; i < nSamples * 6; ++i)
        packU16(out + DATA_FRAME_OVERHEAD + i * 2, (uint16_t)samples[i]);
    return DATA_FRAME_OVERHEAD + nSamples * SAMPLE_LEN;
}
inline size_t packRecTrailer(uint32_t crc, uint8_t out[6]) {
    out[0] = PROTO_VER;
    out[1] = REC_FRAME_TRAILER;
    packU32(out + 2, crc);
    return 6;
}

}  // namespace nrfbridge
