// Host-side unit tests for the pure Bridge wire-format core (firmware-nrf/lib/bridge).
//
//   pio test -e native
//
// Proto.h + ImuCapture.h are the byte-for-byte AUTHORITY that the Web Bluetooth app (JS) and the
// Connect IQ app (Monkey C) mirror. Until P4 these codecs were only ever verified over the USB
// serial console on the board (the desktop GATT cache made BLE verification unreliable); this suite
// pins the layouts documented in GATT.md so drift in the firmware OR either mirror is caught in CI.

#include <unity.h>
#include <cstring>

#include "Proto.h"
#include "ImuCapture.h"

using namespace nrfbridge;

void setUp(void) {}
void tearDown(void) {}

// ---- little-endian helpers (independent re-impl so the test doesn't trust Proto's own) ----------
static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t* p) { return (uint32_t)(le16(p) | ((uint32_t)le16(p + 2) << 16)); }

// ================= Status (20 bytes) ==========================================================
void test_status_layout(void) {
    StatusPacket s;
    s.srcConnected = true; s.recording = true;   // flags b0 | b2 = 0x05
    s.srcPowerW = 250; s.outPowerW = 275; s.cadenceRpm = 90;
    s.balancePct = 49; s.batteryPct = 88;
    s.scaleMilli = 1100; s.offsetDeciW = -50;      // -5.0 W
    s.recSamples = 0x00010203; s.uptimeS = 3661;
    uint8_t b[STATUS_LEN];
    TEST_ASSERT_EQUAL_UINT(STATUS_LEN, packStatus(s, b));
    TEST_ASSERT_EQUAL_UINT8(PROTO_VER, b[0]);
    TEST_ASSERT_EQUAL_UINT8(0x05, b[1]);
    TEST_ASSERT_EQUAL_INT16(250, (int16_t)le16(b + 2));
    TEST_ASSERT_EQUAL_INT16(275, (int16_t)le16(b + 4));
    TEST_ASSERT_EQUAL_INT16(90, (int16_t)le16(b + 6));
    TEST_ASSERT_EQUAL_INT8(49, (int8_t)b[8]);
    TEST_ASSERT_EQUAL_UINT8(88, b[9]);
    TEST_ASSERT_EQUAL_UINT16(1100, le16(b + 10));
    TEST_ASSERT_EQUAL_INT16(-50, (int16_t)le16(b + 12));
    TEST_ASSERT_EQUAL_UINT32(0x00010203u, le32(b + 14));
    TEST_ASSERT_EQUAL_UINT16(3661, le16(b + 18));
}

void test_status_none_sentinels(void) {
    StatusPacket s;  // defaults: all "none" = -1, battery 0xFF
    uint8_t b[STATUS_LEN];
    packStatus(s, b);
    TEST_ASSERT_EQUAL_UINT8(0x00, b[1]);
    TEST_ASSERT_EQUAL_INT16(-1, (int16_t)le16(b + 2));
    TEST_ASSERT_EQUAL_INT8(-1, (int8_t)b[8]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, b[9]);
}

// ================= Config (44 bytes, roundtrip + validation) ==================================
void test_config_roundtrip(void) {
    ConfigPacket c;
    c.singleSided = true; c.scaleMilli = 1234; c.offsetDeciW = -75;
    strncpy(c.srcFilter, "ASSIOMA", sizeof(c.srcFilter));
    strncpy(c.outName, "SB20 Bridge", sizeof(c.outName));
    uint8_t b[CONFIG_LEN];
    TEST_ASSERT_EQUAL_UINT(CONFIG_LEN, packConfig(c, b));
    TEST_ASSERT_EQUAL_UINT8(0x04, b[1]);            // b2 single-sided
    TEST_ASSERT_EQUAL_UINT16(1234, le16(b + 2));
    TEST_ASSERT_EQUAL_INT16(-75, (int16_t)le16(b + 4));
    ConfigPacket r;
    TEST_ASSERT_TRUE(unpackConfig(b, CONFIG_LEN, r));
    TEST_ASSERT_TRUE(r.singleSided);
    TEST_ASSERT_EQUAL_UINT16(1234, r.scaleMilli);
    TEST_ASSERT_EQUAL_INT16(-75, r.offsetDeciW);
    TEST_ASSERT_EQUAL_STRING("ASSIOMA", r.srcFilter);
    TEST_ASSERT_EQUAL_STRING("SB20 Bridge", r.outName);
}

void test_config_name_padding_and_truncation(void) {
    ConfigPacket c;
    // exactly 19 chars fills the field with no NUL on the wire
    strncpy(c.srcFilter, "0123456789ABCDEFGHI", sizeof(c.srcFilter));
    uint8_t b[CONFIG_LEN];
    packConfig(c, b);
    TEST_ASSERT_EQUAL_UINT8('I', b[6 + 18]);       // last name byte
    TEST_ASSERT_EQUAL_UINT8(0x00, b[25 + 0]);      // outName empty -> NUL-padded
    ConfigPacket r;
    TEST_ASSERT_TRUE(unpackConfig(b, CONFIG_LEN, r));
    TEST_ASSERT_EQUAL_STRING("0123456789ABCDEFGHI", r.srcFilter);  // 19 chars, NUL-terminated in struct
    TEST_ASSERT_EQUAL_STRING("", r.outName);
}

void test_config_rejects_bad_version_and_length(void) {
    ConfigPacket c, r = c;
    uint8_t b[CONFIG_LEN];
    packConfig(c, b);
    b[0] = 2;  // wrong version
    TEST_ASSERT_FALSE(unpackConfig(b, CONFIG_LEN, r));
    b[0] = PROTO_VER;
    TEST_ASSERT_FALSE(unpackConfig(b, CONFIG_LEN - 1, r));  // short
}

void test_config_rejects_out_of_range(void) {
    ConfigPacket c; c.scaleMilli = 3000;  // > 2.0x
    uint8_t b[CONFIG_LEN]; packConfig(c, b);
    ConfigPacket r;
    TEST_ASSERT_FALSE(unpackConfig(b, CONFIG_LEN, r));
    ConfigPacket c2; c2.offsetDeciW = 2000;  // +200 W > +/-100 W
    packConfig(c2, b);
    TEST_ASSERT_FALSE(unpackConfig(b, CONFIG_LEN, r));
}

// ================= Curve (roundtrip + factor gate + clear) ====================================
void test_curve_roundtrip(void) {
    CurvePoint pts[3] = {{100, 1250}, {200, 1111}, {300, 1000}};
    uint8_t b[2 + 3 * 4];
    TEST_ASSERT_EQUAL_UINT(2 + 3 * 4, packCurve(pts, 3, b));
    TEST_ASSERT_EQUAL_UINT8(3, b[1]);
    TEST_ASSERT_EQUAL_UINT16(100, le16(b + 2));
    TEST_ASSERT_EQUAL_UINT16(1250, le16(b + 4));
    CurvePoint out[CURVE_MAX_POINTS];
    TEST_ASSERT_EQUAL_INT(3, unpackCurve(b, sizeof(b), out));
    TEST_ASSERT_EQUAL_UINT16(200, out[1].powerW);
    TEST_ASSERT_EQUAL_UINT16(1111, out[1].factorMilli);
}

void test_curve_empty_clears(void) {
    uint8_t b[2]; packCurve(nullptr, 0, b);
    CurvePoint out[CURVE_MAX_POINTS];
    TEST_ASSERT_EQUAL_INT(0, unpackCurve(b, 2, out));
}

void test_curve_rejects_bad_factor_and_overflow(void) {
    CurvePoint out[CURVE_MAX_POINTS];
    // factor below 0.25x
    CurvePoint bad[1] = {{100, 100}};
    uint8_t b[6]; packCurve(bad, 1, b);
    TEST_ASSERT_EQUAL_INT(-1, unpackCurve(b, 6, out));
    // nPoints claims more than payload / MAX
    uint8_t hdr[2] = {PROTO_VER, CURVE_MAX_POINTS + 1};
    TEST_ASSERT_EQUAL_INT(-1, unpackCurve(hdr, 2, out));
}

// ================= RecCtl (SetRate validation) ================================================
void test_recctl_setrate_valid(void) {
    uint8_t b[3] = {PROTO_VER, (uint8_t)RecCmd::SetRate, 26};
    RecCtlWrite w;
    TEST_ASSERT_TRUE(unpackRecCtl(b, 3, w));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)RecCmd::SetRate, (uint8_t)w.cmd);
    TEST_ASSERT_EQUAL_UINT8(26, w.rateHz);
}

void test_recctl_rejects_bad_rate_and_cmd(void) {
    RecCtlWrite w;
    uint8_t badrate[3] = {PROTO_VER, (uint8_t)RecCmd::SetRate, 60};  // not 13/26/52/104
    TEST_ASSERT_FALSE(unpackRecCtl(badrate, 3, w));
    uint8_t badcmd[2] = {PROTO_VER, 9};  // > SetRate
    TEST_ASSERT_FALSE(unpackRecCtl(badcmd, 2, w));
}

void test_recstate_layout(void) {
    uint8_t b[RECSTATE_LEN];
    packRecState(RecState::Recording, 52, 1000, 10240, b);
    TEST_ASSERT_EQUAL_UINT8(1, b[1]);              // recording
    TEST_ASSERT_EQUAL_UINT8(52, b[2]);
    TEST_ASSERT_EQUAL_UINT32(1000, le32(b + 4));
    TEST_ASSERT_EQUAL_UINT32(10240, le32(b + 8));
}

// ================= Calibrate state (16 bytes, coverage bins) ==================================
void test_calstate_layout(void) {
    int cov[6] = {1, 2, 300, 0, 5, 5};  // 300 clamps to 255
    uint8_t b[CALSTATE_LEN];
    packCalState(CalWireState::Fitted, 40, 30, -3 /* -0.3 W */, cov, true, b);
    TEST_ASSERT_EQUAL_UINT8(2, b[1]);              // fitted
    TEST_ASSERT_EQUAL_UINT16(40, le16(b + 3));
    TEST_ASSERT_EQUAL_UINT16(30, le16(b + 5));
    TEST_ASSERT_EQUAL_INT16(-3, (int16_t)le16(b + 7));
    TEST_ASSERT_EQUAL_UINT8(1, b[9]);
    TEST_ASSERT_EQUAL_UINT8(255, b[11]);           // clamped
    TEST_ASSERT_EQUAL_UINT8(1, b[15]);             // enough
}

// ================= Workout state (18 bytes, P4 — biasW at offset 12) ==========================
void test_wkstate_layout_with_bias(void) {
    uint8_t flags = 1 | 2 | 8 | 16;  // loaded+running+ergConnected+ergControlled
    uint8_t b[WKSTATE_LEN];
    TEST_ASSERT_EQUAL_UINT(WKSTATE_LEN, packWkState(flags, 138, 0, 9, 480, 138, 5, -10, b));
    TEST_ASSERT_EQUAL_UINT8(flags, b[1]);
    TEST_ASSERT_EQUAL_INT16(138, (int16_t)le16(b + 2));  // target (already includes bias)
    TEST_ASSERT_EQUAL_UINT8(0, b[4]);              // segIndex
    TEST_ASSERT_EQUAL_UINT8(9, b[5]);              // nSeg
    TEST_ASSERT_EQUAL_UINT16(480, le16(b + 6));    // segRemainS
    TEST_ASSERT_EQUAL_INT16(138, (int16_t)le16(b + 8));  // ergSentW
    TEST_ASSERT_EQUAL_UINT16(5, le16(b + 10));     // elapsedS
    TEST_ASSERT_EQUAL_INT16(-10, (int16_t)le16(b + 12));  // biasW (negative -> signed)
    TEST_ASSERT_EQUAL_UINT8(0, b[14]);             // reserved
    TEST_ASSERT_EQUAL_UINT8(0, b[17]);
}

// ================= ScanList (21-byte slots) ===================================================
void test_scanlist_slots(void) {
    ScanEntry e[2];
    memset(e, 0, sizeof(e));
    strncpy(e[0].name, "SB20-FTMS-Server", SCAN_NAME);
    e[0].rssi = -55; e[0].flags = 0x02;            // isFtms
    strncpy(e[1].name, "ASSIOMA", SCAN_NAME);
    e[1].rssi = -70; e[1].flags = 0x01;            // isCps
    uint8_t b[2 + 2 * SCAN_SLOT];
    TEST_ASSERT_EQUAL_UINT(2 + 2 * SCAN_SLOT, packScanList(e, 2, b));
    TEST_ASSERT_EQUAL_UINT8(2, b[1]);
    TEST_ASSERT_EQUAL_STRING_LEN("SB20-FTMS-Server", (char*)(b + 2), 16);
    TEST_ASSERT_EQUAL_INT8(-55, (int8_t)b[2 + SCAN_NAME]);
    TEST_ASSERT_EQUAL_UINT8(0x02, b[2 + SCAN_NAME + 1]);
    // second slot at +21
    TEST_ASSERT_EQUAL_INT8(-70, (int8_t)b[2 + SCAN_SLOT + SCAN_NAME]);
    TEST_ASSERT_EQUAL_UINT8(0x01, b[2 + SCAN_SLOT + SCAN_NAME + 1]);
}

void test_scanlist_caps_at_max(void) {
    ScanEntry e[SCAN_MAX + 2];
    memset(e, 0, sizeof(e));
    for (size_t i = 0; i < SCAN_MAX + 2; ++i) { strncpy(e[i].name, "M", SCAN_NAME); }
    uint8_t b[2 + (SCAN_MAX + 2) * SCAN_SLOT];
    size_t n = packScanList(e, SCAN_MAX + 2, b);
    TEST_ASSERT_EQUAL_UINT8(SCAN_MAX, b[1]);       // clamped
    TEST_ASSERT_EQUAL_UINT(2 + SCAN_MAX * SCAN_SLOT, n);
}

// ================= RecData framing (explicit type byte — the 0xFE regression) ==================
void test_recdata_frame_types(void) {
    uint8_t h[12];
    packRecHeader(52, 766, 12345, h);
    TEST_ASSERT_EQUAL_UINT8(REC_FRAME_HEADER, h[1]);  // 0xFF
    TEST_ASSERT_EQUAL_UINT8(52, h[2]);
    TEST_ASSERT_EQUAL_UINT32(766, le32(h + 4));
    TEST_ASSERT_EQUAL_UINT32(12345, le32(h + 8));

    uint8_t t[6];
    packRecTrailer(0xDEADBEEF, t);
    TEST_ASSERT_EQUAL_UINT8(REC_FRAME_TRAILER, t[1]);  // 0xFE
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, le32(t + 2));
}

void test_recdata_seq254_is_still_a_data_frame(void) {
    // The regression: sequence 254 (0xFE) once put 0xFE where the type tag lives and masqueraded
    // as the trailer, truncating the download. With the explicit type byte, seq 254's TYPE is 0xFD.
    int16_t samples[2 * 6] = {1, -2, 3, -4, 5, -6, 100, -200, 300, -400, 500, -600};
    uint8_t b[DATA_FRAME_OVERHEAD + 2 * SAMPLE_LEN];
    size_t len = packRecDataFrame(254, samples, 2, b);
    TEST_ASSERT_EQUAL_UINT8(REC_FRAME_DATA, b[1]);   // 0xFD, NOT 0xFE
    TEST_ASSERT_EQUAL_UINT16(254, le16(b + 2));      // seq lives at offset 2, not the type slot
    TEST_ASSERT_EQUAL_UINT8(2, b[4]);                // count
    TEST_ASSERT_EQUAL_INT16(-2, (int16_t)le16(b + DATA_FRAME_OVERHEAD + 2));
    TEST_ASSERT_EQUAL_INT16(-600, (int16_t)le16(b + DATA_FRAME_OVERHEAD + 11 * 2));
    TEST_ASSERT_EQUAL_UINT(DATA_FRAME_OVERHEAD + 2 * SAMPLE_LEN, len);
}

// ================= ImuCapture (linear fill, auto-stop, crc) ====================================
void test_imu_capture_fills_and_autostops(void) {
    ImuCapture<4> cap;
    cap.start(1000, 52);
    TEST_ASSERT_TRUE(cap.active());
    TEST_ASSERT_EQUAL_UINT32(4, cap.capacity());
    int16_t s[6] = {1, 2, 3, 4, 5, 6};
    for (int i = 0; i < 4; ++i) TEST_ASSERT_TRUE(cap.add(s));
    TEST_ASSERT_EQUAL_UINT32(4, cap.count());
    TEST_ASSERT_FALSE(cap.active());               // full -> auto-stopped
    TEST_ASSERT_FALSE(cap.add(s));                 // rejects further
    TEST_ASSERT_EQUAL_UINT32(4, cap.count());      // data preserved
    TEST_ASSERT_EQUAL_UINT8(52, cap.rateHz());
    TEST_ASSERT_EQUAL_UINT32(1000, cap.startMs());
}

void test_imu_capture_stores_samples_and_erase(void) {
    ImuCapture<8> cap;
    cap.start(0, 26);
    int16_t a[6] = {-1, -2, -3, -4, -5, -6};
    int16_t b[6] = {10, 20, 30, 40, 50, 60};
    cap.add(a); cap.add(b);
    TEST_ASSERT_EQUAL_INT16(-3, cap.sample(0)[2]);
    TEST_ASSERT_EQUAL_INT16(40, cap.sample(1)[3]);
    cap.erase();
    TEST_ASSERT_EQUAL_UINT32(0, cap.count());
    TEST_ASSERT_FALSE(cap.active());
}

void test_imu_crc32_deterministic_and_data_sensitive(void) {
    ImuCapture<4> a, b;
    a.start(0, 52); b.start(0, 52);
    int16_t s1[6] = {1, 2, 3, 4, 5, 6};
    int16_t s2[6] = {1, 2, 3, 4, 5, 7};  // one LSB different
    a.add(s1); b.add(s1);
    TEST_ASSERT_EQUAL_UINT32(a.crc32(), b.crc32());  // same data -> same crc
    ImuCapture<4> c; c.start(0, 52); c.add(s2);
    TEST_ASSERT_NOT_EQUAL(a.crc32(), c.crc32());     // different data -> different crc
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_status_layout);
    RUN_TEST(test_status_none_sentinels);
    RUN_TEST(test_config_roundtrip);
    RUN_TEST(test_config_name_padding_and_truncation);
    RUN_TEST(test_config_rejects_bad_version_and_length);
    RUN_TEST(test_config_rejects_out_of_range);
    RUN_TEST(test_curve_roundtrip);
    RUN_TEST(test_curve_empty_clears);
    RUN_TEST(test_curve_rejects_bad_factor_and_overflow);
    RUN_TEST(test_recctl_setrate_valid);
    RUN_TEST(test_recctl_rejects_bad_rate_and_cmd);
    RUN_TEST(test_recstate_layout);
    RUN_TEST(test_calstate_layout);
    RUN_TEST(test_wkstate_layout_with_bias);
    RUN_TEST(test_scanlist_slots);
    RUN_TEST(test_scanlist_caps_at_max);
    RUN_TEST(test_recdata_frame_types);
    RUN_TEST(test_recdata_seq254_is_still_a_data_frame);
    RUN_TEST(test_imu_capture_fills_and_autostops);
    RUN_TEST(test_imu_capture_stores_samples_and_erase);
    RUN_TEST(test_imu_crc32_deterministic_and_data_sensitive);
    return UNITY_END();
}
