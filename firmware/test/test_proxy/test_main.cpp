// Host-side unit tests for the platform-agnostic proxy core (no hardware).
//
//   pio test -e native
//
// The firmware analogue of the Python suite: the CPS codec, the correction, and the
// ProxyCore relay are proven on the host BEFORE any of it touches a real meter or the SB20.

#include <unity.h>

#include "CalibrationFit.h"
#include "CalibrationPage.h"
#include "CalibrationSession.h"
#include "ConfigPage.h"
#include "Correction.h"
#include "Cps.h"
#include "DiagReport.h"
#include "Ftms.h"
#include "HttpSecurity.h"
#include "MeterMatch.h"
#include "OtaManifest.h"
#include "OtaUpdater.h"
#include "OtaVerify.h"
#include "LogBuffer.h"
#include "MockCrank.h"
#include "MockMeter.h"
#include "Provisioning.h"
#include "PerfStats.h"
#include "ProxyCore.h"
#include "RuntimeConfig.h"
#include "SetupPin.h"
#include "Shifter.h"
#include "Status.h"
#include "StatusLed.h"
#include "OledScreen.h"
#include "WebApp.h"

using namespace sb20proxy;

void setUp() {}
void tearDown() {}

// --- correction ---------------------------------------------------------------

void test_correction_scale_offset() {
    Correction c{0.5f, 10.0f};
    PowerReading r;
    r.power_w = 200;
    TEST_ASSERT_EQUAL_INT(110, c.apply(r).power_w);  // 200*0.5 + 10
}

void test_correction_clamps_at_zero() {
    Correction c{1.0f, -50.0f};
    PowerReading r;
    r.power_w = 20;
    TEST_ASSERT_EQUAL_INT(0, c.apply(r).power_w);
}

// --- non-linear correction curve (GridTransform port) -------------------------

void test_curve_empty_is_unity() {
    CorrectionCurve curve;
    TEST_ASSERT_TRUE(curve.empty());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, curve.factorAt(200));
}

void test_curve_interpolates_and_holds_flat() {
    CorrectionCurve curve;
    curve.add(300, 0.91f);  // added out of order — add() keeps it sorted
    curve.add(100, 0.95f);
    TEST_ASSERT_EQUAL_FLOAT(0.95f, curve.factorAt(50));    // flat-held below first bp
    TEST_ASSERT_EQUAL_FLOAT(0.95f, curve.factorAt(100));   // at first bp
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.93f, curve.factorAt(200));  // midpoint interpolation
    TEST_ASSERT_EQUAL_FLOAT(0.91f, curve.factorAt(300));   // at last bp
    TEST_ASSERT_EQUAL_FLOAT(0.91f, curve.factorAt(400));   // flat-held above last bp
}

void test_curve_correction_takes_precedence() {
    // A populated curve wins; scale/offset are ignored. Golden integers match the Python
    // GridTransform: round(reported * factor), factor interpolated on the curve.
    Correction c;
    c.scale = 9.9f;   // absurd on purpose — proves it is NOT used when the curve is set
    c.offset = 9.9f;
    c.curve.add(100, 0.95f);
    c.curve.add(300, 0.91f);
    PowerReading r;
    r.power_w = 200;
    TEST_ASSERT_EQUAL_INT(186, c.apply(r).power_w);  // round(200 * 0.93)
    r.power_w = 100;
    TEST_ASSERT_EQUAL_INT(95, c.apply(r).power_w);   // round(100 * 0.95)
    r.power_w = 300;
    TEST_ASSERT_EQUAL_INT(273, c.apply(r).power_w);  // round(300 * 0.91)
}

// --- CPS measurement codec ----------------------------------------------------

void test_cps_measurement_roundtrip() {
    std::vector<uint8_t> frame = encodeCpsMeasurement(287);
    TEST_ASSERT_EQUAL_INT(4, frame.size());
    TEST_ASSERT_EQUAL_INT(287, decodeCpsPower(frame.data(), frame.size()));
}

void test_cps_decode_short_frame_is_safe() {
    uint8_t two[2] = {0, 0};
    TEST_ASSERT_EQUAL_INT(0, decodeCpsPower(two, 2));
}

void test_calibration_response_bytes() {
    // Real Stages crank BLE zero-reset reply (offset 0), captured byte-for-byte as 200c010000
    // (G-crank62144-ble-zero-20260615-070353.jsonl). This is the production SPOOF_CAL_OFFSET value.
    std::vector<uint8_t> real = encodeCalibrationResponse(0);
    const uint8_t want[] = {0x20, 0x0c, 0x01, 0x00, 0x00};
    TEST_ASSERT_EQUAL_INT(5, (int)real.size());
    for (size_t i = 0; i < 5; ++i) TEST_ASSERT_EQUAL_HEX8(want[i], real[i]);
    // sint16-LE encoding still works for a non-zero offset (903 = 0x0387, the ANT+ value):
    std::vector<uint8_t> nz = encodeCalibrationResponse(903);
    TEST_ASSERT_EQUAL_UINT8(0x87, nz[3]);
    TEST_ASSERT_EQUAL_UINT8(0x03, nz[4]);
}

// --- control-point handshake: the SB20's calibration/config (must be ANSWERED) ----------------
// The SB20 terminates the link if a CP procedure goes unanswered (bike-session 2: disconnect
// reason=531), so handleControlPoint replies to EVERY write. Formats grounded in captures: the
// Stages app sends the *Enhanced* offset-comp op 0x10 (not the basic 0x0C), and the Stages crank's
// Request-Crank-Length reply OMITS the success byte (`20 05 <len>`, unlike the Assioma's `20 05 01 <len>`).

void test_cp_offset_comp_enhanced_0x10() {
    // Enhanced Offset Compensation (0x10) — the op the Stages app sends. Spec response is RICHER than
    // the simple 0x0C: offset (sint16 LE) THEN Manufacturer Company ID (uint16 LE). The old firmware
    // wrongly sent the 5-byte 0x0C shape -> the calibrate UI spun (bike-session 3, 2026-06-19). This
    // asserts the spec-correct STRUCTURE; the exact company-id (+ any mfg data) are grounded next from
    // the real crank's 0x10 reply (sessions/session-04 capture), not derivable from the spec.
    const uint8_t req[] = {CP_OP_ENHANCED_OFFSET_COMP};
    CpResult r = handleControlPoint(req, sizeof(req), 345, /*offset*/0, /*mfgCompanyId*/0x0123);
    const uint8_t expected[] = {0x20, 0x10, 0x01, 0x00, 0x00, 0x23, 0x01};  // offset 0 LE, company 0x0123 LE
    TEST_ASSERT_EQUAL_INT(7, (int)r.response.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r.response.data(), 7);
    TEST_ASSERT_FALSE(r.crankLengthChanged);
}

void test_cp_offset_comp_enhanced_0x10_real_crank() {
    // The REAL Stages SPM2 crank's 0x10 reply, captured ON-BIKE 2026-06-25 (session 8 G1,
    // G-crank62144-ble-enhanced-0x10-20260625-0716.jsonl): 20 10 01 | 00 00 | ba 01 | 04 85 03 b7 03
    // = offset 0, Manufacturer Company ID 442 (0x01BA), then 5 opaque mfg-data bytes. The earlier
    // 0x0000 company-id placeholder left the Stages app's calibrate spinning (session 8 G2); this
    // golden vector locks in the real crank's EXACT bytes (now Config::SPOOF_MFG_COMPANY_ID + MFG_DATA).
    const uint8_t req[] = {CP_OP_ENHANCED_OFFSET_COMP};
    const std::vector<uint8_t> mfgData = {0x04, 0x85, 0x03, 0xB7, 0x03};
    CpResult r = handleControlPoint(req, sizeof(req), 345, /*offset*/0, /*mfgCompanyId*/0x01BA, mfgData);
    const uint8_t expected[] = {0x20, 0x10, 0x01, 0x00, 0x00, 0xBA, 0x01, 0x04, 0x85, 0x03, 0xB7, 0x03};
    TEST_ASSERT_EQUAL_INT(12, (int)r.response.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r.response.data(), 12);
    TEST_ASSERT_FALSE(r.crankLengthChanged);
}

void test_encode_enhanced_offset_comp_structure() {
    // 0x20 | 0x10 | 0x01 | offset(sint16 LE) | mfgCompanyId(uint16 LE) | mfgData[...]
    const std::vector<uint8_t> mfgData = {0xDE, 0xAD};
    auto r = encodeEnhancedOffsetCompResponse(/*offset*/-5, /*mfgCompanyId*/0xABCD, mfgData);
    const uint8_t expected[] = {0x20, 0x10, 0x01, 0xFB, 0xFF, 0xCD, 0xAB, 0xDE, 0xAD};  // -5 = 0xFFFB LE
    TEST_ASSERT_EQUAL_INT(9, (int)r.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r.data(), 9);
    // No manufacturer-specific data -> 7 bytes (offset + company id only); still richer than 0x0C's 5.
    auto bare = encodeEnhancedOffsetCompResponse(0, 0x0000);
    TEST_ASSERT_EQUAL_INT(7, (int)bare.size());
}

void test_cp_offset_comp_basic_0x0C() {
    const uint8_t req[] = {CP_OP_START_OFFSET_COMP};
    CpResult r = handleControlPoint(req, sizeof(req), 345, 903);
    const uint8_t expected[] = {0x20, 0x0C, 0x01, 0x87, 0x03};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r.response.data(), 5);
}

void test_cp_request_source_zero_flag() {
    // Offset-comp / zero-reset ops (0x0C, 0x10) set requestSourceZero so the hardware seam forwards a
    // REAL zero to the source meter (the Assioma's CP supports 0x0C — see decisions.md / forward-plan §10);
    // crank-length ops (0x04 set / 0x05 request) must NOT trigger a source zero.
    const uint8_t c0c[] = {CP_OP_START_OFFSET_COMP};
    TEST_ASSERT_TRUE(handleControlPoint(c0c, sizeof(c0c), 345, 0).requestSourceZero);
    const uint8_t c10[] = {CP_OP_ENHANCED_OFFSET_COMP};
    TEST_ASSERT_TRUE(handleControlPoint(c10, sizeof(c10), 345, 0).requestSourceZero);
    const uint8_t c04[] = {CP_OP_SET_CRANK_LENGTH, 0x59, 0x01};
    TEST_ASSERT_FALSE(handleControlPoint(c04, sizeof(c04), 345, 0).requestSourceZero);
    const uint8_t c05[] = {CP_OP_REQUEST_CRANK_LENGTH};
    TEST_ASSERT_FALSE(handleControlPoint(c05, sizeof(c05), 345, 0).requestSourceZero);
}

void test_cp_set_crank_length_0x04() {
    const uint8_t req[] = {CP_OP_SET_CRANK_LENGTH, 0x59, 0x01};   // 0x0159 = 345 = 172.5 mm
    CpResult r = handleControlPoint(req, sizeof(req), 330, 903);  // was 330 = 165 mm
    TEST_ASSERT_TRUE(r.crankLengthChanged);
    TEST_ASSERT_EQUAL_UINT16(345, r.crankLengthHalfMm);
    const uint8_t expected[] = {0x20, 0x04, 0x01};               // success
    TEST_ASSERT_EQUAL_INT(3, (int)r.response.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r.response.data(), 3);
}

void test_cp_request_crank_length_0x05_stages_format() {
    const uint8_t req[] = {CP_OP_REQUEST_CRANK_LENGTH};
    CpResult r = handleControlPoint(req, sizeof(req), 345, 903);
    const uint8_t expected[] = {0x20, 0x05, 0x59, 0x01};         // 0x0159 = 345, NO success byte
    TEST_ASSERT_EQUAL_INT(4, (int)r.response.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r.response.data(), 4);
}

void test_cp_set_then_request_crank_length_roundtrip() {
    const uint8_t setReq[] = {CP_OP_SET_CRANK_LENGTH, 0x54, 0x01};  // 0x0154 = 340 = 170 mm
    CpResult set = handleControlPoint(setReq, sizeof(setReq), 345, 903);
    const uint8_t getReq[] = {CP_OP_REQUEST_CRANK_LENGTH};
    CpResult got = handleControlPoint(getReq, sizeof(getReq), set.crankLengthHalfMm, 903);
    const uint8_t expected[] = {0x20, 0x05, 0x54, 0x01};         // reads back the value just set
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, got.response.data(), 4);
}

void test_cp_unknown_op_not_supported() {
    const uint8_t req[] = {0x03};  // Request Supported Sensor Locations — not implemented
    CpResult r = handleControlPoint(req, sizeof(req), 345, 903);
    const uint8_t expected[] = {0x20, 0x03, 0x02};              // not supported
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r.response.data(), 3);
}

void test_cp_malformed_is_safe() {
    CpResult r = handleControlPoint(nullptr, 0, 345, 903);     // empty/no data: never silent, no crash
    TEST_ASSERT_TRUE(r.response.size() >= 3);
    TEST_ASSERT_EQUAL_UINT8(0x20, r.response[0]);
    TEST_ASSERT_FALSE(r.crankLengthChanged);
    TEST_ASSERT_EQUAL_UINT16(345, r.crankLengthHalfMm);        // state preserved
}

void test_cp_set_crank_length_missing_param_invalid() {
    const uint8_t req[] = {CP_OP_SET_CRANK_LENGTH};            // opcode but no length param
    CpResult r = handleControlPoint(req, sizeof(req), 345, 903);
    const uint8_t expected[] = {0x20, 0x04, 0x03};            // invalid parameter
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r.response.data(), 3);
    TEST_ASSERT_FALSE(r.crankLengthChanged);
}

// --- meter selection: which device to READ (fixes the bike-session-2 source bouncing) ---------
// All of the Assioma AND the real Stages cranks advertise the CPS service, so matching on the
// service alone latched onto a real crank. isTargetMeter requires a NAMED device to match the
// meter-name filter; a PINNED address overrides everything (deterministic source).

void test_meter_match_assioma_by_name() {
    TEST_ASSERT_TRUE(isTargetMeter("ASSIOMA17039L", true, "e6:20:90:8c:f3:fe", "",
                                   "Stages 62144", "ASSIOMA"));
}

void test_meter_match_rejects_real_stages_crank() {
    // The bug: 'Stages 4963' advertises CPS but is not "ASSIOMA" — must be rejected.
    TEST_ASSERT_FALSE(isTargetMeter("Stages 4963", true, "e3:25:39:38:92:71", "",
                                    "Stages 62144", "ASSIOMA"));
}

void test_meter_match_skips_our_own_spoof() {
    TEST_ASSERT_FALSE(isTargetMeter("Stages 62144", true, "aa:bb:cc:dd:ee:ff", "",
                                    "Stages 62144", "ASSIOMA"));
}

void test_meter_match_nameless_winrt_rig_by_cps() {
    // A genuinely nameless CPS peripheral (e.g. a passive-scan view of the WinRT rig) matches by UUID.
    TEST_ASSERT_TRUE(isTargetMeter("", true, "12:34:56:78:9a:bc", "", "Stages 62144", "ASSIOMA"));
    TEST_ASSERT_FALSE(isTargetMeter("", false, "12:34:56:78:9a:bc", "", "Stages 62144", "ASSIOMA"));
}

void test_meter_match_winrt_rig_carries_host_name_blocked_in_prod() {
    // REALITY (decisions.md 2026-06-22): under an ACTIVE scan Windows stamps the PC's name into the
    // scan response, so the WinRT fake_meter rig is NOT nameless — and that name isn't "ASSIOMA".
    // In a production build (matchAnyCps default false) the rig is correctly NOT matched.
    TEST_ASSERT_FALSE(isTargetMeter("CAULDT9H", true, "48:cf:ea:87:24:f3", "",
                                    "Stages 62144", "ASSIOMA"));
}

void test_meter_match_any_cps_bench_flag() {
    // BENCH flag on: the host-named WinRT rig (CPS-advertising) IS matched...
    TEST_ASSERT_TRUE(isTargetMeter("CAULDT9H", true, "48:cf:ea:87:24:f3", "",
                                   "Stages 62144", "ASSIOMA", /*matchAnyCps=*/true));
    // ...and a real Assioma still matches...
    TEST_ASSERT_TRUE(isTargetMeter("ASSIOMA17039L", true, "e6:20:90:8c:f3:fe", "",
                                   "Stages 62144", "ASSIOMA", /*matchAnyCps=*/true));
    // ...but a real "Stages NNNN" crank is STILL rejected (never read the SB20's native cranks)...
    TEST_ASSERT_FALSE(isTargetMeter("Stages 4963", true, "e3:25:39:38:92:71", "",
                                    "Stages 62144", "ASSIOMA", /*matchAnyCps=*/true));
    // ...and our own spoof is STILL skipped (the loop guard wins even in bench mode)...
    TEST_ASSERT_FALSE(isTargetMeter("Stages 62144", true, "aa:bb:cc:dd:ee:ff", "",
                                    "Stages 62144", "ASSIOMA", /*matchAnyCps=*/true));
    // ...and a non-CPS device is never matched.
    TEST_ASSERT_FALSE(isTargetMeter("CAULDT9H", false, "48:cf:ea:87:24:f3", "",
                                    "Stages 62144", "ASSIOMA", /*matchAnyCps=*/true));
}

void test_meter_match_pinned_address_wins() {
    // Pinned to the right crank's address -> matches it even though its name isn't "ASSIOMA"
    // (the single-right-crank use case); a different address is rejected.
    TEST_ASSERT_TRUE(isTargetMeter("Stages 4963", true, "e3:25:39:38:92:71", "e3:25:39:38:92:71",
                                   "Stages 62144", "ASSIOMA"));
    TEST_ASSERT_FALSE(isTargetMeter("ASSIOMA17039L", true, "e6:20:90:8c:f3:fe", "e3:25:39:38:92:71",
                                    "Stages 62144", "ASSIOMA"));
}

void test_meter_match_pin_never_reads_own_spoof() {
    // Even pinned, the loop guard wins: never latch onto our own spoof name.
    TEST_ASSERT_FALSE(isTargetMeter("Stages 62144", true, "e3:25:39:38:92:71", "e3:25:39:38:92:71",
                                    "Stages 62144", "ASSIOMA"));
}

// --- CPS cadence (Crank Revolution Data) --------------------------------------

void test_cps_cadence_frame() {
    std::vector<uint8_t> f = encodeCpsMeasurement(250, 8, 5120);  // power, revs, eventTime
    TEST_ASSERT_EQUAL_INT(8, f.size());
    TEST_ASSERT_EQUAL_HEX16(CPM_CRANK_REV_DATA_PRESENT, decodeCpsFlags(f.data(), f.size()));
    TEST_ASSERT_EQUAL_INT(250, decodeCpsPower(f.data(), f.size()));
    TEST_ASSERT_EQUAL_INT(8, decodeCrankRevs(f.data(), f.size()));
    TEST_ASSERT_EQUAL_INT(5120, decodeCrankEventTime(f.data(), f.size()));
}

void test_crank_cadence_roundtrips_rpm() {
    // 96 rpm is chosen so the revolution period is an exact tick count (61440/96 = 640),
    // making the recovered cadence exact. Drive at 1 Hz for 5 s -> 1.6 rev/s * 5 = 8 revs.
    CrankCadence c;
    for (int i = 0; i < 5; ++i) c.advance(96.0f, 1000);
    TEST_ASSERT_EQUAL_INT(8, c.cumulativeRevs);
    TEST_ASSERT_EQUAL_INT(5120, c.lastEventTime);  // 8 * 640
    TEST_ASSERT_FLOAT_WITHIN(
        0.05f, 96.0f, cadenceRpmFromCrank(0, 0, c.cumulativeRevs, c.lastEventTime));
}

void test_crank_cadence_coasting_no_events() {
    CrankCadence c;
    c.advance(90.0f, 1000);  // pedalling
    uint16_t revs = c.cumulativeRevs, t = c.lastEventTime;
    TEST_ASSERT_TRUE(revs > 0);
    c.advance(0.0f, 5000);  // coasting: neither revs nor event time may advance
    TEST_ASSERT_EQUAL_INT(revs, c.cumulativeRevs);
    TEST_ASSERT_EQUAL_INT(t, c.lastEventTime);
}

// --- real Stages SPM2 frame (0x2F): golden vectors from the 2026-06-17 capture ------------
// findings/captures/G-crankL-ble-recon-20260617.jsonl. A minimal 0x20 frame paired with the
// SB20 but showed NO power; the spoof must emit THIS exact frame shape, so we pin it byte-wise.

void test_stages_frame_golden_encode() {
    // The exact bytes the real crank sent: flags 0x2F, power 174 W, balance 88 (44%), accum
    // torque 63292, crank revs 230, last event 27838 -> "2f00ae00583cf7e600be6c".
    std::vector<uint8_t> f = encodeStagesCpsMeasurement(174, 88, 63292, 230, 27838);
    const uint8_t expected[] = {0x2f, 0x00, 0xae, 0x00, 0x58, 0x3c, 0xf7, 0xe6, 0x00, 0xbe, 0x6c};
    TEST_ASSERT_EQUAL_INT(11, (int)f.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, f.data(), 11);
}

void test_stages_frame_flags_and_power() {
    std::vector<uint8_t> f = encodeStagesCpsMeasurement(174, 88, 63292, 230, 27838);
    TEST_ASSERT_EQUAL_HEX16(0x002F, decodeCpsFlags(f.data(), f.size()));
    TEST_ASSERT_EQUAL_INT(174, decodeCpsPower(f.data(), f.size()));  // power still at bytes 2-3
}

void test_decode_crank_data_behind_preceding_fields() {
    // The generic decoder must find crank-rev at offset 7 (after balance+torque), not a rigid 4.
    const uint8_t frame[] = {0x2f, 0x00, 0xae, 0x00, 0x58, 0x3c, 0xf7, 0xe6, 0x00, 0xbe, 0x6c};
    TEST_ASSERT_EQUAL_UINT(7, crankRevDataOffset(0x002F));
    CpsCrankData c = decodeCrankData(frame, sizeof(frame));
    TEST_ASSERT_TRUE(c.present);
    TEST_ASSERT_EQUAL_INT(230, c.cumulativeRevs);
    TEST_ASSERT_EQUAL_INT(27838, c.lastEventTime);
}

void test_cadence_from_two_real_stages_frames() {
    // Two consecutive captured frames: revs 230->231 over 1127 event ticks (~1.10 s) ~= 54.5 rpm.
    const uint8_t f1[] = {0x2f, 0x00, 0xae, 0x00, 0x58, 0x3c, 0xf7, 0xe6, 0x00, 0xbe, 0x6c};
    const uint8_t f2[] = {0x2f, 0x00, 0xb0, 0x00, 0x5a, 0x16, 0xfb, 0xe7, 0x00, 0x25, 0x71};
    CpsCrankData a = decodeCrankData(f1, sizeof(f1));
    CpsCrankData b = decodeCrankData(f2, sizeof(f2));
    float rpm = cadenceRpmFromCrank(a.cumulativeRevs, a.lastEventTime, b.cumulativeRevs,
                                    b.lastEventTime);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 54.5f, rpm);
}

// --- L/R pedal balance: decode the source split, forward it to the spoof ------------------
// Golden vectors from the REAL Assioma DUO BLE CPS, flags 0x0023 (balance + ref-left + crank-rev):
// findings/captures/ASSIOMA-ble-cps-20260622.jsonl. Balance byte sits at a fixed offset 4.

void test_decode_cps_balance_from_real_assioma() {
    // "23009e005816134e4d": flags 0x0023, power 158 W, balance 0x58=88 (44% L / 56% R).
    const uint8_t f[] = {0x23, 0x00, 0x9e, 0x00, 0x58, 0x16, 0x13, 0x4e, 0x4d};
    CpsBalance b = decodeCpsBalance(f, sizeof(f));
    TEST_ASSERT_TRUE(b.present);
    TEST_ASSERT_EQUAL_UINT8(88, b.halfPct);            // 88/2 = 44% left
    TEST_ASSERT_EQUAL_INT(158, decodeCpsPower(f, sizeof(f)));
    // crank-rev sits right after balance (offset 5) for this flag set — cadence still decodes.
    TEST_ASSERT_EQUAL_UINT(5, crankRevDataOffset(0x0023));
    TEST_ASSERT_TRUE(decodeCrankData(f, sizeof(f)).present);
}

void test_decode_cps_balance_absent_when_bit_clear() {
    // crank-rev-only frame (flags 0x20): no balance bit -> not present, don't misread byte 4.
    const uint8_t f[] = {0x20, 0x00, 0x9e, 0x00, 0x16, 0x13, 0x4e, 0x4d};
    CpsBalance b = decodeCpsBalance(f, sizeof(f));
    TEST_ASSERT_FALSE(b.present);
    TEST_ASSERT_EQUAL_UINT8(0, b.halfPct);
}

void test_decode_cps_balance_truncated_frame_is_safe() {
    // bit0 set but the frame is too short to hold the balance byte -> not present, no OOB read.
    const uint8_t f[] = {0x23, 0x00, 0x9e, 0x00};
    CpsBalance b = decodeCpsBalance(f, sizeof(f));
    TEST_ASSERT_FALSE(b.present);
}

void test_balance_survives_correction() {
    // The correction is power-only; the L/R split must pass through untouched (the SB20 needs the
    // real balance even when the power is being scaled by a meter-to-meter correction).
    PowerReading r;
    r.power_w = 200;
    r.cadence_rpm = 90;
    r.balance_half_pct = 88;  // 44% L
    PowerReading out = Correction{/*scale=*/1.1f, /*offset=*/0.0f}.apply(r);
    TEST_ASSERT_EQUAL_INT(220, out.power_w);          // power scaled
    TEST_ASSERT_EQUAL_INT(88, out.balance_half_pct);  // balance untouched
    TEST_ASSERT_EQUAL_INT(90, out.cadence_rpm);
}

void test_balance_hold_is_sticky_across_balanceless_frames() {
    // Once a split is seen, hold it through frames that drop the balance byte (no flap to 50/50);
    // reset() clears it so a new meter re-learns. Mirrors how cadence state isn't reset per-frame.
    BalanceHold h;
    TEST_ASSERT_EQUAL_INT(-1, h.halfPct);                          // nothing seen yet
    const uint8_t withBal[] = {0x23, 0x00, 0x9e, 0x00, 0x58};      // flags 0x0023, balance 88
    TEST_ASSERT_EQUAL_INT(88, h.update(decodeCpsBalance(withBal, sizeof(withBal))));
    const uint8_t noBal[] = {0x20, 0x00, 0x9e, 0x00, 0x16, 0x13};  // crank-rev only, no balance
    TEST_ASSERT_EQUAL_INT(88, h.update(decodeCpsBalance(noBal, sizeof(noBal))));  // held, not -1
    h.reset();
    TEST_ASSERT_EQUAL_INT(-1, h.update(decodeCpsBalance(noBal, sizeof(noBal))));  // re-learn
}

void test_balance_forwarded_to_spoof_frame() {
    // Read the Assioma's real split, re-emit it on the spoofed Stages 0x2F crank: the spoof's
    // balance byte (also offset 4) must equal the source's (88), not the old fixed 50% (100).
    const uint8_t src[] = {0x23, 0x00, 0x9e, 0x00, 0x58, 0x16, 0x13, 0x4e, 0x4d};
    CpsBalance b = decodeCpsBalance(src, sizeof(src));
    std::vector<uint8_t> out = encodeStagesCpsMeasurement(
        decodeCpsPower(src, sizeof(src)), b.halfPct, /*torque=*/0, /*revs=*/0, /*evt=*/0);
    TEST_ASSERT_EQUAL_UINT8(88, out[4]);               // forwarded split, not 100
    CpsBalance round = decodeCpsBalance(out.data(), out.size());
    TEST_ASSERT_TRUE(round.present);
    TEST_ASSERT_EQUAL_UINT8(88, round.halfPct);        // decode(encode(x)) == x
}

// --- ProxyCore relay (the loopback, in firmware) ------------------------------

void test_proxy_relays_power() {
    MockMeter meter;
    MockCrank crank;
    ProxyCore proxy(meter, crank);
    proxy.begin();
    TEST_ASSERT_TRUE(crank.started);

    meter.emit(250);
    TEST_ASSERT_EQUAL_INT(1, proxy.forwarded());
    TEST_ASSERT_EQUAL_INT(250, crank.last.power_w);
}

// --- RuntimeConfig (the NVS-backed user config) + the single-sided ×2 -----------------------

void test_runtime_config_defaults_from_compile_time() {
    RuntimeConfig c = RuntimeConfig::defaults();
    TEST_ASSERT_EQUAL_STRING(Config::METER_NAME_FILTER, c.meterNameFilter.c_str());
    TEST_ASSERT_EQUAL_STRING(Config::METER_ADDRESS, c.meterAddress.c_str());
    TEST_ASSERT_FALSE(c.singleSidedDouble);
}

void test_runtime_config_line_roundtrip() {
    RuntimeConfig c;
    c.meterAddress = "e3:25:39:38:92:71";  // a surviving R crank, pinned
    c.meterNameFilter = "Stages";
    c.singleSidedDouble = true;
    c.spoofName = "Stages 12345";          // a different owner's crank identity
    c.spoofSerial = "99887766";
    RuntimeConfig back = RuntimeConfig::fromLine(c.toLine());
    TEST_ASSERT_EQUAL_STRING("e3:25:39:38:92:71", back.meterAddress.c_str());
    TEST_ASSERT_EQUAL_STRING("Stages", back.meterNameFilter.c_str());
    TEST_ASSERT_TRUE(back.singleSidedDouble);
    TEST_ASSERT_EQUAL_STRING("Stages 12345", back.spoofName.c_str());   // identity round-trips
    TEST_ASSERT_EQUAL_STRING("99887766", back.spoofSerial.c_str());
    // an empty-address (match-by-name) config also round-trips
    RuntimeConfig n; n.meterNameFilter = "ASSIOMA";
    RuntimeConfig n2 = RuntimeConfig::fromLine(n.toLine());
    TEST_ASSERT_EQUAL_STRING("", n2.meterAddress.c_str());
    TEST_ASSERT_FALSE(n2.singleSidedDouble);
}

void test_runtime_config_defaults_spoof_identity() {
    RuntimeConfig c = RuntimeConfig::defaults();
    TEST_ASSERT_EQUAL_STRING(Config::SPOOF_NAME, c.spoofName.c_str());
    TEST_ASSERT_EQUAL_STRING(Config::SPOOF_SERIAL, c.spoofSerial.c_str());
}

void test_runtime_config_old_line_keeps_default_identity() {
    // an old 3-field NVS line (pre-spoof-picker) must still parse, keeping the default identity
    RuntimeConfig c = RuntimeConfig::fromLine("aa:bb:cc:dd:ee:ff|ASSIOMA|1");
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", c.meterAddress.c_str());
    TEST_ASSERT_TRUE(c.singleSidedDouble);
    TEST_ASSERT_EQUAL_STRING(Config::SPOOF_NAME, c.spoofName.c_str());     // identity defaulted
    TEST_ASSERT_EQUAL_STRING(Config::SPOOF_SERIAL, c.spoofSerial.c_str());
}

void test_runtime_config_malformed_line_falls_back_to_defaults() {
    RuntimeConfig c = RuntimeConfig::fromLine("garbage-no-delimiters");
    TEST_ASSERT_EQUAL_STRING(Config::METER_NAME_FILTER, c.meterNameFilter.c_str());
    TEST_ASSERT_FALSE(RuntimeConfig::fromLine("").singleSidedDouble);
}

void test_runtime_config_defaults_to_spoof_mode() {
    RuntimeConfig c = RuntimeConfig::defaults();
    TEST_ASSERT_TRUE(c.mode == ProxyMode::Spoof);
    TEST_ASSERT_TRUE(c.curve.empty());
}

void test_runtime_config_old_line_keeps_spoof_mode_and_no_curve() {
    // a 5-field line (pre-corrector) must still parse: SPOOF, no reference, no curve.
    RuntimeConfig c = RuntimeConfig::fromLine("aa:bb:cc:dd:ee:ff|ASSIOMA|0|Stages 62144|11821518");
    TEST_ASSERT_TRUE(c.mode == ProxyMode::Spoof);
    TEST_ASSERT_TRUE(c.curve.empty());
    TEST_ASSERT_EQUAL_STRING("", c.refMeterAddress.c_str());
}

void test_runtime_config_corrector_roundtrip_with_curve() {
    RuntimeConfig c = RuntimeConfig::defaults();
    c.mode = ProxyMode::Corrector;
    c.meterAddress = "c1:c2:c3:c4:c5:c6";   // the XCadey (DUT)
    c.meterNameFilter = "XCADEY";
    c.spoofName = "SB20 Corrector";          // our own advertised identity
    c.refMeterAddress = "a1:a2:a3:a4:a5:a6"; // the Assioma (reference)
    c.refMeterNameFilter = "ASSIOMA";
    c.curve.add(100, 0.95f);
    c.curve.add(300, 0.91f);
    RuntimeConfig back = RuntimeConfig::fromLine(c.toLine());
    TEST_ASSERT_TRUE(back.mode == ProxyMode::Corrector);
    TEST_ASSERT_EQUAL_STRING("c1:c2:c3:c4:c5:c6", back.meterAddress.c_str());
    TEST_ASSERT_EQUAL_STRING("SB20 Corrector", back.spoofName.c_str());
    TEST_ASSERT_EQUAL_STRING("a1:a2:a3:a4:a5:a6", back.refMeterAddress.c_str());
    TEST_ASSERT_EQUAL_STRING("ASSIOMA", back.refMeterNameFilter.c_str());
    TEST_ASSERT_EQUAL_INT(2, (int)back.curve.points.size());
    // the curve survives to its stored precision, so Correction.apply matches before/after save.
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.95f, back.curve.factorAt(100));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.91f, back.curve.factorAt(300));
}

// --- meter-to-meter calibration fit (the C++ mirror of calibration.py) --------

// The shared golden dataset: XCadey-like DUT reads high; true ref = dut*(1.10 - 0.0003*dut). The
// SAME pairs + expected breakpoints are checked by code/tests/test_calibration_parity.py against the
// Python fit_grid oracle, so firmware and desk agree to the stored digits.
static std::vector<CalPair> goldenCalPairs() {
    const int duts[] = {60, 70, 80, 110, 120, 130, 160, 170, 180,
                        210, 220, 230, 260, 270, 280, 310, 320, 330};
    std::vector<CalPair> pairs;
    for (int d : duts) {
        const float ref = (float)d * (1.10f - 0.0003f * d);
        pairs.push_back({(float)d, ref});
    }
    return pairs;
}

void test_fit_curve_matches_python_grid() {
    CorrectionCurve curve = fitCurve(goldenCalPairs(), 6, 3);
    TEST_ASSERT_EQUAL_INT(6, (int)curve.points.size());
    const float bp[6][2] = {{70, 1.079f}, {120, 1.064f}, {170, 1.049f},
                            {220, 1.034f}, {270, 1.019f}, {320, 1.004f}};
    for (int i = 0; i < 6; ++i) {
        TEST_ASSERT_FLOAT_WITHIN(0.05f, bp[i][0], curve.points[i].power_w);
        TEST_ASSERT_FLOAT_WITHIN(1e-3f, bp[i][1], curve.points[i].factor);
    }
}

void test_fit_scale_offset_matches_python() {
    ScaleOffset so = fitScaleOffset(goldenCalPairs());
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.983f, so.scale);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 9.2f, so.offset);
}

void test_fit_correction_prefers_curve_and_corrects() {
    Correction c = fitCorrection(goldenCalPairs(), 6, 3);
    TEST_ASSERT_FALSE(c.curve.empty());  // the auto fit picks the curve when the data supports it
    // the corrected DUT should read close to the reference: residual is sub-watt on clean data.
    TEST_ASSERT_TRUE(residualMeanW(c, goldenCalPairs()) < 1.0f);
    // a raw 200 W DUT corrects upward (DUT reads ~3% high here -> factor > 1).
    PowerReading r;
    r.power_w = 200;
    TEST_ASSERT_TRUE(c.apply(r).power_w > 200);
}

void test_fit_correction_falls_back_to_linear_when_sparse() {
    // samples cluster in ONE power band (only that bin reaches minPerBin) -> fewer than 2
    // breakpoints -> fall back to a linear scale/offset instead of a curve. Ratio is a clean 1.10.
    std::vector<CalPair> pairs = {{100, 110}, {140, 154}, {150, 165}, {160, 176}, {300, 330}};
    Correction c = fitCorrection(pairs, 6, 3);
    TEST_ASSERT_TRUE(c.curve.empty());          // no curve (too few populated bins)
    TEST_ASSERT_TRUE(c.scale > 1.0f);           // but a real linear correction was fit (~1.10x)
}

void test_pair_accumulator_pairs_fresh_streams_only() {
    PairAccumulator acc(2000, 1200);
    acc.onRef(150.0f, 1000);
    acc.onDut(160.0f, 1500);   // ref 500 ms old -> paired
    TEST_ASSERT_EQUAL_INT(1, (int)acc.count());
    acc.onDut(170.0f, 4000);   // ref now 3000 ms old (> skew) -> dropped
    TEST_ASSERT_EQUAL_INT(1, (int)acc.count());
    acc.onRef(180.0f, 4200);
    acc.onDut(175.0f, 4300);   // fresh ref again -> paired
    TEST_ASSERT_EQUAL_INT(2, (int)acc.count());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 160.0f, acc.pairs()[0].dut);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 150.0f, acc.pairs()[0].ref);
}

void test_pair_accumulator_drops_implausible_and_caps() {
    PairAccumulator acc(2000, 2);
    acc.onRef(5.0f, 0);
    acc.onDut(150.0f, 100);    // ref 5 W < min -> dropped (implausible)
    TEST_ASSERT_EQUAL_INT(0, (int)acc.count());
    acc.onRef(150.0f, 200);
    acc.onDut(150.0f, 200);
    acc.onDut(151.0f, 300);
    acc.onDut(152.0f, 400);    // cap is 2 -> third pair dropped
    TEST_ASSERT_EQUAL_INT(2, (int)acc.count());
    TEST_ASSERT_TRUE(acc.full());
}

void test_pair_accumulator_coverage_bins() {
    PairAccumulator acc;
    acc.onRef(100.0f, 0);
    acc.onDut(60.0f, 0);
    acc.onDut(160.0f, 0);
    acc.onDut(260.0f, 0);
    std::vector<float> edges = {0, 100, 200, 300};  // 3 bands: [0,100),[100,200),[200,300)
    std::vector<int> cov = acc.coverage(edges);
    TEST_ASSERT_EQUAL_INT(3, (int)cov.size());
    TEST_ASSERT_EQUAL_INT(1, cov[0]);  // 60
    TEST_ASSERT_EQUAL_INT(1, cov[1]);  // 160
    TEST_ASSERT_EQUAL_INT(1, cov[2]);  // 260
}

void test_curve_string_roundtrip() {
    CorrectionCurve curve;
    curve.add(70, 1.079f);
    curve.add(320, 1.004f);
    CorrectionCurve back = curveFromString(curveToString(curve));
    TEST_ASSERT_EQUAL_INT(2, (int)back.points.size());
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 70.0f, back.points[0].power_w);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.079f, back.points[0].factor);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.004f, back.points[1].factor);
    TEST_ASSERT_TRUE(curveFromString("").empty());   // empty string -> no curve
}

// --- SB20 shifter decode + debounce (the C++ mirror of shifter_erg.py) --------

void test_shifter_decode_golden_buttons() {
    // the real session-3 `01`-frames, one per button (shifter-ble-protocol.md): `01 00 <bit LE>`
    const uint8_t l_up[4] = {0x01, 0x00, 0x01, 0x00};
    const uint8_t r_3rd[4] = {0x01, 0x00, 0x20, 0x00};
    TEST_ASSERT_EQUAL_INT(SHIFTER_LEFT_UP, decodeShifterButtonBit(l_up, 4));
    TEST_ASSERT_EQUAL_INT(SHIFTER_RIGHT_3RD, decodeShifterButtonBit(r_3rd, 4));
    TEST_ASSERT_TRUE(shifterButtonFromBit(SHIFTER_LEFT_UP) == ShifterButton::LeftUp);
    TEST_ASSERT_TRUE(shifterButtonFromBit(SHIFTER_RIGHT_3RD) == ShifterButton::Right3);
    TEST_ASSERT_TRUE(shifterButtonFromBit(0x0040) == ShifterButton::None);  // unknown bit
    const uint8_t too_short[2] = {0x01, 0x00};
    TEST_ASSERT_EQUAL_INT(0, decodeShifterButtonBit(too_short, 2));
    TEST_ASSERT_EQUAL_STRING("RIGHT 3rd", shifterButtonName(ShifterButton::Right3));
}

void test_shifter_debounce_one_event_per_press() {
    ShifterDebounce d;
    const uint8_t held_r3[4] = {0x01, 0x00, 0x20, 0x00};            // RIGHT-3rd, held (streams)
    const uint8_t commit_r3[6] = {0x03, 0x00, 0x20, 0x00, 0x20, 0x00};
    const uint8_t term04_r3[4] = {0x04, 0x00, 0x20, 0x00};
    const uint8_t held_lup[4] = {0x01, 0x00, 0x01, 0x00};          // LEFT-up, held
    // a held run collapses to ONE event on the rising edge; repeats are suppressed
    TEST_ASSERT_TRUE(d.feed(held_r3, 4) == ShifterButton::Right3);
    TEST_ASSERT_TRUE(d.feed(held_r3, 4) == ShifterButton::None);
    TEST_ASSERT_TRUE(d.feed(held_r3, 4) == ShifterButton::None);
    // commit + terminator end the press (boundary)
    TEST_ASSERT_TRUE(d.feed(commit_r3, 6) == ShifterButton::None);
    TEST_ASSERT_TRUE(d.feed(term04_r3, 4) == ShifterButton::None);
    // a fresh press of the SAME button fires again
    TEST_ASSERT_TRUE(d.feed(held_r3, 4) == ShifterButton::Right3);
    // a DIFFERENT button mid-stream is a new edge (no terminator needed)
    TEST_ASSERT_TRUE(d.feed(held_lup, 4) == ShifterButton::LeftUp);
}

// --- calibration session (the on-device wizard's orchestration) ---------------

void test_calibration_session_lifecycle_and_fit() {
    CalibrationSession s(5);  // small min for the test
    TEST_ASSERT_TRUE(s.state() == CalState::Idle);
    s.onDut(100, 1000);       // readings before start are ignored
    TEST_ASSERT_EQUAL_INT(0, (int)s.pairCount());

    s.start();
    TEST_ASSERT_TRUE(s.collecting());
    uint32_t t = 1000;
    for (const auto& p : goldenCalPairs()) {  // feed the shared golden DUT/ref dataset, paired in time
        s.onRef(p.ref, t);
        s.onDut(p.dut, t);
        t += 1000;
    }
    TEST_ASSERT_EQUAL_INT(18, (int)s.pairCount());
    TEST_ASSERT_TRUE(s.enoughToFit());
    TEST_ASSERT_TRUE(s.finish());
    TEST_ASSERT_TRUE(s.fitted());
    TEST_ASSERT_FALSE(s.fit().curve.empty());      // a real curve came out of the session
    TEST_ASSERT_TRUE(s.residualW() < 1.0f);        // and it corrects the DUT to ~the reference
}

void test_calibration_session_finish_needs_enough_pairs() {
    CalibrationSession s(10);
    s.start();
    s.onRef(150, 0);
    s.onDut(150, 0);  // 1 pair, need 10
    TEST_ASSERT_FALSE(s.enoughToFit());
    TEST_ASSERT_FALSE(s.finish());   // too few -> stays collecting, no fit
    TEST_ASSERT_TRUE(s.collecting());
}

void test_calibration_session_cancel_resets() {
    CalibrationSession s(2);
    s.start();
    s.onRef(150, 0);
    s.onDut(150, 0);
    s.cancel();
    TEST_ASSERT_TRUE(s.state() == CalState::Idle);
    TEST_ASSERT_EQUAL_INT(0, (int)s.pairCount());
}

void test_calibration_session_coverage_guides_the_rider() {
    CalibrationSession s(2);
    s.start();
    s.onRef(100, 0);
    s.onDut(60, 0);    // band 0 (<100)
    s.onDut(160, 0);   // band 2 (150-200)
    s.onDut(260, 0);   // band 4 (250-300)
    std::vector<int> cov = s.coverage();  // edges {0,100,150,200,250,300,2000}
    TEST_ASSERT_EQUAL_INT(6, (int)cov.size());
    TEST_ASSERT_EQUAL_INT(1, cov[0]);
    TEST_ASSERT_EQUAL_INT(1, cov[2]);
    TEST_ASSERT_EQUAL_INT(1, cov[4]);
}

// --- calibration wizard page (pure render/parse) ------------------------------

void test_calibration_form_parse() {
    CalForm f = parseCalibrationForm("action=start&dut=aa%3Abb%3Acc%3Add%3Aee%3Aff&ref=11%3A22%3A33%3A44%3A55%3A66&name=My+Meter");
    TEST_ASSERT_EQUAL_STRING("start", f.action.c_str());
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", f.dutAddr.c_str());  // urldecoded
    TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", f.refAddr.c_str());
    TEST_ASSERT_EQUAL_STRING("My Meter", f.deviceName.c_str());        // '+' -> space
}

void test_calibration_start_validation() {
    CalForm none;  // nothing picked
    TEST_ASSERT_NOT_NULL(calibrationStartError(none));
    CalForm same; same.dutAddr = "aa"; same.refAddr = "aa";  // same meter
    TEST_ASSERT_NOT_NULL(calibrationStartError(same));
    CalForm ok; ok.dutAddr = "aa"; ok.refAddr = "bb";
    TEST_ASSERT_NULL(calibrationStartError(ok));
}

void test_calibration_page_idle_lists_meters_and_pickers() {
    CalWizardView v;  // Idle
    SourceCandidate a; a.address = "aa:bb:cc:dd:ee:ff"; a.name = "XCADEY"; a.isCps = true;
    SourceCandidate b; b.address = "11:22:33:44:55:66"; b.name = "ASSIOMA17039L"; b.isCps = true;
    v.devices = {a, b};
    std::string h = renderCalibrationPage(v);
    TEST_ASSERT_TRUE(h.find("XCADEY") != std::string::npos);
    TEST_ASSERT_TRUE(h.find("ASSIOMA17039L") != std::string::npos);
    TEST_ASSERT_TRUE(h.find("/calibrate/start") != std::string::npos);
    TEST_ASSERT_TRUE(h.find(">DUT<") != std::string::npos);   // the per-row role buttons
    TEST_ASSERT_TRUE(h.find(">Ref<") != std::string::npos);
}

void test_calibration_page_collecting_shows_coverage_and_gates_finish() {
    CalWizardView v;
    v.state = CalState::Collecting;
    v.dutConnected = true;
    v.refConnected = true;
    v.pairCount = 12;
    v.minPairs = 30;
    v.enoughToFit = false;
    v.coverage = {3, 2, 4, 1, 0, 0};
    std::string h = renderCalibrationPage(v);
    TEST_ASSERT_TRUE(h.find("12") != std::string::npos);          // pair count shown
    TEST_ASSERT_TRUE(h.find("/calibrate/finish") != std::string::npos);
    TEST_ASSERT_TRUE(h.find("submit' disabled") != std::string::npos);  // Finish button gated
    v.enoughToFit = true;
    std::string h2 = renderCalibrationPage(v);
    TEST_ASSERT_TRUE(h2.find("Finish") != std::string::npos);
    TEST_ASSERT_TRUE(h2.find("submit' disabled") == std::string::npos);  // now enabled
}

void test_calibration_page_fitted_shows_curve_and_name() {
    CalWizardView v;
    v.state = CalState::Fitted;
    v.residualW = 0.7f;
    v.curve.add(70, 1.079f);
    v.curve.add(320, 1.004f);
    v.deviceName = "XCadey-corrected";
    std::string h = renderCalibrationPage(v);
    TEST_ASSERT_TRUE(h.find("0.7 W") != std::string::npos);            // residual
    TEST_ASSERT_TRUE(h.find("1.079") != std::string::npos);           // a curve factor
    TEST_ASSERT_TRUE(h.find("XCadey-corrected") != std::string::npos);  // editable name prefilled
    TEST_ASSERT_TRUE(h.find("/calibrate/save") != std::string::npos);
}

void test_runtime_config_calibrating_roundtrip() {
    RuntimeConfig c = RuntimeConfig::defaults();
    c.calibrating = true;
    c.meterAddress = "d1:d2:d3:d4:d5:d6";
    c.refMeterAddress = "r1:r2:r3:r4:r5:r6";
    RuntimeConfig back = RuntimeConfig::fromLine(c.toLine());
    TEST_ASSERT_TRUE(back.calibrating);
    TEST_ASSERT_EQUAL_STRING("r1:r2:r3:r4:r5:r6", back.refMeterAddress.c_str());
    // a 9-field line (pre-calibrating) parses with calibrating = false (backward compatible)
    RuntimeConfig old = RuntimeConfig::fromLine("a|ASSIOMA|0|Stages 62144|11821518|0|||");
    TEST_ASSERT_FALSE(old.calibrating);
}

void test_correction_to_curve_passthrough_and_linear() {
    Correction curveC;
    curveC.curve.add(100, 0.95f);
    curveC.curve.add(300, 0.91f);
    TEST_ASSERT_EQUAL_INT(2, (int)correctionToCurve(curveC).points.size());  // a curve stored as-is
    Correction lin;
    lin.scale = 1.10f;  // a pure scale -> factor 1.10 at every power
    CorrectionCurve b = correctionToCurve(lin);
    TEST_ASSERT_TRUE(b.points.size() >= 2);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.10f, b.factorAt(200));
}

void test_proxycore_tap_sees_raw_source_reading() {
    MockMeter m;
    MockCrank ck;
    ProxyCore pc(m, ck, Correction{2.0f, 0.0f});  // doubles power
    int tapped = -1;
    pc.setTap([&](const PowerReading& r) { tapped = r.power_w; });
    pc.begin();
    m.emit(150, 90, 1000);
    TEST_ASSERT_EQUAL_INT(150, tapped);            // the tap sees the RAW (pre-correction) DUT power
    TEST_ASSERT_EQUAL_INT(300, ck.last.power_w);   // the crank still gets the corrected value
}

void test_strip_config_delims_protects_the_nvs_line() {
    TEST_ASSERT_EQUAL_STRING("ABSB20", stripConfigDelims("A|B|SB20").c_str());  // all '|' removed
    // a device name with '|' must NOT corrupt the config line: the parsers strip it first
    CalForm f = parseCalibrationForm("action=save&name=Chris%7CSB20");  // %7C = '|'
    TEST_ASSERT_EQUAL_STRING("ChrisSB20", f.deviceName.c_str());
    RuntimeConfig c = parseConfigForm("name=AS%7CSIOMA&spoof_name=St%7Cages");
    TEST_ASSERT_EQUAL_STRING("ASSIOMA", c.meterNameFilter.c_str());
    TEST_ASSERT_EQUAL_STRING("Stages", c.spoofName.c_str());
}

void test_curve_string_keeps_zero_factor_rejects_garbage() {
    // a legitimately-clamped low-power point (factor 0.0) must survive the round-trip, not be dropped
    CorrectionCurve k = curveFromString("50.0:0.0000,300.0:1.0500");
    TEST_ASSERT_EQUAL_INT(2, (int)k.points.size());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, k.points[0].factor);
    // a malformed factor token is rejected (not silently read as 0)
    CorrectionCurve g = curveFromString("100.0:abc,300.0:1.05");
    TEST_ASSERT_EQUAL_INT(1, (int)g.points.size());  // only the well-formed point survives
}

void test_band_label_guards_short_edges() {
    TEST_ASSERT_EQUAL_STRING("", sb20proxy::detail::bandLabel({}, 0).c_str());        // empty
    TEST_ASSERT_EQUAL_STRING("", sb20proxy::detail::bandLabel({100.0f}, 0).c_str());  // single edge
}

void test_url_encode_roundtrips_with_decode() {
    // urlEncode rebuilds a form body from the WebServer's already-decoded named args; the values our
    // forms carry (spaces, ':', '&', '%') must survive urlDecode(urlEncode(x)) == x.
    const char* cases[] = {"SB20 Corrector", "aa:bb:cc:dd:ee:ff", "a&b=c", "100%done", "XCadey-corrected"};
    for (auto* s : cases) TEST_ASSERT_EQUAL_STRING(s, urlDecode(urlEncode(s)).c_str());
    TEST_ASSERT_EQUAL_STRING("SB20%20Corrector", urlEncode("SB20 Corrector").c_str());  // space -> %20
    TEST_ASSERT_EQUAL_STRING("a%3Ab", urlEncode("a:b").c_str());                         // reserved ':'
}

void test_config_form_parse() {
    RuntimeConfig c = parseConfigForm("addr=e3%3A25%3A39%3A38%3A92%3A71&name=Stages&single=1");
    TEST_ASSERT_EQUAL_STRING("e3:25:39:38:92:71", c.meterAddress.c_str());  // %3A urldecoded to ':'
    TEST_ASSERT_EQUAL_STRING("Stages", c.meterNameFilter.c_str());
    TEST_ASSERT_TRUE(c.singleSidedDouble);
    // no checkbox -> single false; pinned address empty -> match by name
    RuntimeConfig d = parseConfigForm("addr=&name=ASSIOMA");
    TEST_ASSERT_EQUAL_STRING("", d.meterAddress.c_str());
    TEST_ASSERT_EQUAL_STRING("ASSIOMA", d.meterNameFilter.c_str());
    TEST_ASSERT_FALSE(d.singleSidedDouble);
    // spoof identity round-trips through the form; blank identity falls back to the default
    RuntimeConfig e = parseConfigForm("addr=&name=ASSIOMA&spoof_name=Stages+12345&spoof_serial=42");
    TEST_ASSERT_EQUAL_STRING("Stages 12345", e.spoofName.c_str());  // '+' -> space
    TEST_ASSERT_EQUAL_STRING("42", e.spoofSerial.c_str());
    RuntimeConfig f = parseConfigForm("addr=&name=ASSIOMA&spoof_name=&spoof_serial=");
    TEST_ASSERT_EQUAL_STRING(Config::SPOOF_NAME, f.spoofName.c_str());      // blank -> default
    TEST_ASSERT_EQUAL_STRING(Config::SPOOF_SERIAL, f.spoofSerial.c_str());
}

void test_config_validation() {
    RuntimeConfig empty;  // no address, no name
    TEST_ASSERT_NOT_NULL(configValidationError(empty));
    RuntimeConfig byAddr; byAddr.meterAddress = "aa:bb:cc:dd:ee:ff";
    TEST_ASSERT_NULL(configValidationError(byAddr));
    RuntimeConfig byName; byName.meterNameFilter = "ASSIOMA";
    TEST_ASSERT_NULL(configValidationError(byName));
}

void test_add_candidate_dedup_and_cap() {
    std::vector<SourceCandidate> list;
    addCandidate(list, {"aa:bb:cc:dd:ee:01", "M1", -70, true, false}, 2);
    addCandidate(list, {"aa:bb:cc:dd:ee:01", "", -55, true, false}, 2);  // dup: stronger rssi wins
    TEST_ASSERT_EQUAL_INT(1, (int)list.size());
    TEST_ASSERT_EQUAL_INT(-55, list[0].rssi);
    TEST_ASSERT_EQUAL_STRING("M1", list[0].name.c_str());                // name kept across passes
    addCandidate(list, {"aa:bb:cc:dd:ee:02", "M2", -80, true, false}, 2);   // fills the cap
    addCandidate(list, {"aa:bb:cc:dd:ee:03", "weak", -90, true, false}, 2); // full + weaker -> dropped
    TEST_ASSERT_EQUAL_INT(2, (int)list.size());
    addCandidate(list, {"aa:bb:cc:dd:ee:04", "near", -40, true, false}, 2); // full + stronger -> evicts weakest
    bool has04 = false, has02 = false;
    for (auto& e : list) { if (e.address == "aa:bb:cc:dd:ee:04") has04 = true;
                           if (e.address == "aa:bb:cc:dd:ee:02") has02 = true; }
    TEST_ASSERT_TRUE(has04);   // the close newcomer is in
    TEST_ASSERT_FALSE(has02);  // the weakest (-80) was evicted
}

void test_dedupe_and_sort_sources() {
    std::vector<SourceCandidate> in = {
        {"aa:bb:cc:dd:ee:01", "Weak", -80, true, false},
        {"aa:bb:cc:dd:ee:02", "Strong", -45, true, false},
        {"aa:bb:cc:dd:ee:01", "Weak", -60, true, false},  // dup of #1, stronger
        {"", "no-addr", -50, true, false},                 // dropped (no address)
    };
    auto out = dedupeAndSortSources(in);
    TEST_ASSERT_EQUAL_INT(2, (int)out.size());             // deduped + the no-addr dropped
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:02", out[0].address.c_str());  // strongest first
    TEST_ASSERT_EQUAL_INT(-60, out[1].rssi);               // dup merged to the stronger reading
}

void test_render_config_page_marks_selected_and_badges() {
    RuntimeConfig cfg; cfg.meterAddress = "aa:bb:cc:dd:ee:02"; cfg.singleSidedDouble = true;
    std::vector<SourceCandidate> ds = {
        {"aa:bb:cc:dd:ee:02", "ASSIOMA17039L", -45, true, false},
        {"e3:25:39:38:92:71", "Stages 4963", -55, true, true},
    };
    std::string h = renderConfigPage(cfg, ds);
    TEST_ASSERT_TRUE(h.find("ASSIOMA17039L") != std::string::npos);
    TEST_ASSERT_TRUE(h.find("class='dev sel'") != std::string::npos);   // the pinned one is selected
    TEST_ASSERT_TRUE(h.find("crank") != std::string::npos);             // the Stages crank badge
    TEST_ASSERT_TRUE(h.find("checkbox' name='single' value='1' checked") != std::string::npos);
    TEST_ASSERT_TRUE(h.find("class='nav'") != std::string::npos);       // the shared bottom Ride/Setup/More nav
    TEST_ASSERT_TRUE(h.find("action='/setup/save'") != std::string::npos);  // the save contract is intact
}

void test_render_config_page_escapes_name() {
    std::vector<SourceCandidate> ds = {{"aa:bb:cc:dd:ee:02", "<script>x", -45, true, false}};
    std::string h = renderConfigPage(RuntimeConfig::defaults(), ds);
    TEST_ASSERT_TRUE(h.find("&lt;script&gt;x") != std::string::npos);
    TEST_ASSERT_TRUE(h.find("<script>x</") == std::string::npos);       // no raw injection
}

void test_render_config_page_shows_spoof_identity() {
    RuntimeConfig cfg = RuntimeConfig::defaults();
    cfg.spoofName = "Stages 62144";
    cfg.spoofSerial = "11821518";
    std::string h = renderConfigPage(cfg);
    TEST_ASSERT_TRUE(h.find("name='spoof_name'") != std::string::npos);   // the identity field
    TEST_ASSERT_TRUE(h.find("Stages 62144") != std::string::npos);        // pre-filled
    TEST_ASSERT_TRUE(h.find("name='spoof_serial'") != std::string::npos);
    TEST_ASSERT_TRUE(h.find("Crank identity") != std::string::npos);      // the section heading
    TEST_ASSERT_TRUE(h.find("action='/setup/reset'") != std::string::npos);  // reset-to-defaults form
}

void test_render_config_page_status_banner() {
    std::string h = renderConfigPage(RuntimeConfig::defaults(), {}, "", false, -1, "Reading ASSIOMA");
    TEST_ASSERT_TRUE(h.find("class='ok'") != std::string::npos);        // the live status banner
    TEST_ASSERT_TRUE(h.find("Reading ASSIOMA") != std::string::npos);
    // no banner when no status passed
    TEST_ASSERT_TRUE(renderConfigPage(RuntimeConfig::defaults()).find("class='ok'") == std::string::npos);
}

void test_proxy_set_correction_applies_single_sided_double() {
    // The runtime single-sided ×2 (a surviving R crank → doubled for total) folds into the
    // correction; setCorrection swaps it in after NVS load, before any reading is forwarded.
    MockMeter meter;
    MockCrank crank;
    ProxyCore proxy(meter, crank);
    proxy.begin();
    proxy.setCorrection(Correction{/*scale=*/2.0f, /*offset=*/0.0f});
    meter.emit(120);  // one leg
    TEST_ASSERT_EQUAL_INT(240, crank.last.power_w);  // doubled to total
}

void test_proxy_applies_correction() {
    MockMeter meter;
    MockCrank crank;
    ProxyCore proxy(meter, crank, Correction{1.0f / 1.1f, 0.0f});  // meter reads ~10% high
    proxy.begin();

    meter.emit(220);  // true ~200
    TEST_ASSERT_INT_WITHIN(1, 200, crank.last.power_w);
}

void test_proxy_preserves_cadence() {
    MockMeter meter;
    MockCrank crank;
    ProxyCore proxy(meter, crank, Correction{0.5f, 0.0f});  // correction touches power only
    proxy.begin();
    meter.emit(200, 90);                                 // power 200, cadence 90
    TEST_ASSERT_EQUAL_INT(100, crank.last.power_w);      // power corrected
    TEST_ASSERT_EQUAL_INT(90, crank.last.cadence_rpm);   // cadence passes through untouched
}

void test_proxy_reset_clears_stale_readings() {
    MockMeter meter;
    MockCrank crank;
    ProxyCore proxy(meter, crank, Correction{1.0f, 0.0f});
    proxy.begin();
    meter.emit(250, 90);
    TEST_ASSERT_EQUAL_INT(250, proxy.lastOutput().power_w);
    proxy.reset();  // meter disconnected -> drop stale values
    TEST_ASSERT_EQUAL_INT(0, proxy.lastOutput().power_w);
    TEST_ASSERT_EQUAL_INT(-1, proxy.lastOutput().cadence_rpm);
    TEST_ASSERT_EQUAL_INT(0, proxy.lastSource().power_w);
}

// --- status JSON (the HTTP observability model) -------------------------------

void test_status_json_mock() {
    ProxyStatus s;
    s.mock = true;
    s.forwarded = 5;
    s.srcPowerW = 220;
    s.srcCadenceRpm = 88;
    s.srcName = "ASSIOMA \"L\"";  // includes a quote -> must be JSON-escaped
    s.identity = "Stages 62144";  // the OUT side we advertise
    s.srcBalanceHalfPct = 88;   // 44 % left
    s.lastPowerW = 200;
    s.lastCadenceRpm = 90;
    s.lastBalanceHalfPct = 88;
    s.uptimeMs = 12345;
    std::string j = renderStatusJson(s);
    TEST_ASSERT_TRUE(j.find("\"source\":\"mock\"") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"identity\":\"Stages 62144\"") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"mode\":\"spoof\"") != std::string::npos);  // default mode
    TEST_ASSERT_TRUE(j.find("\"forwarded\":5") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"src_power_w\":220") != std::string::npos);   // received from meter
    TEST_ASSERT_TRUE(j.find("\"src_cadence_rpm\":88") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"src_balance_pct\":44") != std::string::npos);  // 88/2 = 44 % left
    TEST_ASSERT_TRUE(j.find("\"src_name\":\"ASSIOMA \\\"L\\\"\"") != std::string::npos);  // escaped
    TEST_ASSERT_TRUE(j.find("\"power_w\":200") != std::string::npos);       // broadcast to crank
    TEST_ASSERT_TRUE(j.find("\"cadence_rpm\":90") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"balance_pct\":44") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"ms\":12345") != std::string::npos);
}

void test_status_json_no_balance_is_minus_one() {
    ProxyStatus s;  // defaults: no source balance
    std::string j = renderStatusJson(s);
    TEST_ASSERT_TRUE(j.find("\"src_balance_pct\":-1") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"balance_pct\":-1") != std::string::npos);
}

void test_status_json_mode_corrector() {
    ProxyStatus s;
    s.corrector = true;
    s.identity = "SB20 Corrector";
    std::string j = renderStatusJson(s);
    TEST_ASSERT_TRUE(j.find("\"mode\":\"corrector\"") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"identity\":\"SB20 Corrector\"") != std::string::npos);
}

void test_status_json_source_state() {
    ProxyStatus s;  // real source (mock=false), not yet linked
    TEST_ASSERT_TRUE(renderStatusJson(s).find("\"source\":\"searching\"") != std::string::npos);
    s.sourceConnected = true;
    TEST_ASSERT_TRUE(renderStatusJson(s).find("\"source\":\"connected\"") != std::string::npos);
}

void test_status_json_unknown_cadence() {
    ProxyStatus s;  // default cadence -1
    TEST_ASSERT_TRUE(renderStatusJson(s).find("\"cadence_rpm\":-1") != std::string::npos);
}

void test_status_json_has_firmware_version() {
    ProxyStatus s;  // version defaults to Config::FIRMWARE_VERSION
    const std::string expect = std::string("\"version\":\"") + Config::FIRMWARE_VERSION + "\"";
    TEST_ASSERT_TRUE(renderStatusJson(s).find(expect) != std::string::npos);
}

void test_diag_report_has_firmware_version() {
    RuntimeConfig cfg = RuntimeConfig::defaults();
    ProxyStatus st;  // version defaults to Config::FIRMWARE_VERSION
    const std::string r = renderDiagReport(cfg, st, {});
    TEST_ASSERT_TRUE(r.find(std::string("version=") + Config::FIRMWARE_VERSION) != std::string::npos);
}

void test_firmware_version_feeds_ota_decision() {
    // The build's FIRMWARE_VERSION is what the OTA fleet channel compares against a release
    // manifest: a strictly-newer, well-formed manifest updates; same/older does not.
    OtaManifest newer;
    newer.valid = true; newer.version = "999.0.0"; newer.url = "https://x/fw.bin"; newer.size = 1;
    TEST_ASSERT_TRUE(shouldUpdate(Config::FIRMWARE_VERSION, newer));
    OtaManifest same = newer; same.version = Config::FIRMWARE_VERSION;
    TEST_ASSERT_FALSE(shouldUpdate(Config::FIRMWARE_VERSION, same));
}

// --- WiFi provisioning (the captive-portal pure logic) ------------------------

void test_form_parse_basic() {
    WifiCredentials c = parseFormUrlEncoded("ssid=HomeNet&pass=secret123");
    TEST_ASSERT_EQUAL_STRING("HomeNet", c.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("secret123", c.pass.c_str());
}

void test_form_parse_url_encoding() {
    // '+' -> space, %XX -> byte; field order independent; 'password' alias accepted.
    WifiCredentials c = parseFormUrlEncoded("password=p%40ss+word&ssid=My%20Wi-Fi");
    TEST_ASSERT_EQUAL_STRING("My Wi-Fi", c.ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("p@ss word", c.pass.c_str());
}

void test_form_parse_empty_password() {
    WifiCredentials c = parseFormUrlEncoded("ssid=OpenNet&pass=");
    TEST_ASSERT_EQUAL_STRING("OpenNet", c.ssid.c_str());
    TEST_ASSERT_TRUE(c.pass.empty());
}

void test_validate_accepts_wpa_and_open() {
    TEST_ASSERT_NULL(credValidationError({"HomeNet", "secret123"}));  // WPA
    TEST_ASSERT_NULL(credValidationError({"OpenNet", ""}));           // open network
}

void test_validate_rejects_bad_creds() {
    TEST_ASSERT_NOT_NULL(credValidationError({"", "secret123"}));        // no SSID
    TEST_ASSERT_NOT_NULL(credValidationError({"HomeNet", "short"}));     // pass < 8
    TEST_ASSERT_NOT_NULL(credValidationError({std::string(33, 'x'), ""}));  // SSID > 32
}

void test_portal_page_has_form_fields() {
    std::string html = renderProvisioningPage({{"AlphaNet", -45, true}, {"BetaNet", -70, true}},
                                              "Wrong password");
    TEST_ASSERT_TRUE(html.find("action='/save'") != std::string::npos);
    TEST_ASSERT_TRUE(html.find("name='ssid'") != std::string::npos);
    TEST_ASSERT_TRUE(html.find("name='pass'") != std::string::npos);
    TEST_ASSERT_TRUE(html.find("AlphaNet") != std::string::npos);   // scanned network listed
    TEST_ASSERT_TRUE(html.find("Wrong password") != std::string::npos);  // error surfaced
}

void test_portal_page_escapes_ssid() {
    std::string html = renderProvisioningPage({{"A&B<net>", -50, true}}, "");
    TEST_ASSERT_TRUE(html.find("A&amp;B&lt;net&gt;") != std::string::npos);
    TEST_ASSERT_TRUE(html.find("A&B<net>") == std::string::npos);  // raw form not present
}

// --- the scanned-network picker (RSSI sort, mesh dedup, secured flag, rescan) --------------

void test_rssi_bars_buckets() {
    TEST_ASSERT_EQUAL_INT(4, rssiBars(-40));
    TEST_ASSERT_EQUAL_INT(4, rssiBars(-55));   // boundary (>= -55)
    TEST_ASSERT_EQUAL_INT(3, rssiBars(-60));
    TEST_ASSERT_EQUAL_INT(2, rssiBars(-70));
    TEST_ASSERT_EQUAL_INT(1, rssiBars(-80));
    TEST_ASSERT_EQUAL_INT(0, rssiBars(-90));
}

void test_dedupe_and_sort_networks() {
    // A blank SSID (hidden), a mesh name from two radios, and out-of-order signal strengths.
    std::vector<ScannedNet> raw = {
        {"Weak", -80, true}, {"", -30, false}, {"Mesh", -70, true},
        {"Strong", -42, false}, {"Mesh", -55, true},  // 2nd Mesh AP is stronger -> wins
    };
    std::vector<ScannedNet> out = dedupeAndSortNetworks(raw);
    TEST_ASSERT_EQUAL_INT(3, (int)out.size());     // hidden dropped, Mesh merged
    TEST_ASSERT_EQUAL_STRING("Strong", out[0].ssid.c_str());  // strongest first
    TEST_ASSERT_EQUAL_STRING("Mesh", out[1].ssid.c_str());
    TEST_ASSERT_EQUAL_INT(-55, out[1].rssi);       // kept the stronger of the two Mesh APs
    TEST_ASSERT_EQUAL_STRING("Weak", out[2].ssid.c_str());
}

void test_portal_page_lists_networks_strongest_first() {
    // Supplied weakest-first; the page must render the stronger SSID earlier in the document.
    std::string html = renderProvisioningPage({{"FarNet", -82, true}, {"NearNet", -38, true}});
    TEST_ASSERT_TRUE(html.find("NearNet") < html.find("FarNet"));
    // Tap-list, not a bare datalist: each row carries the SSID for the pick() handler.
    TEST_ASSERT_TRUE(html.find("data-ssid='NearNet'") != std::string::npos);
    TEST_ASSERT_TRUE(html.find("onclick='pick(this)'") != std::string::npos);
    TEST_ASSERT_TRUE(html.find("<datalist") == std::string::npos);  // old approach is gone
}

void test_portal_page_marks_secured_and_open() {
    std::string html = renderProvisioningPage({{"LockedNet", -50, true}, {"OpenNet", -52, false}});
    TEST_ASSERT_TRUE(html.find("&#128274;") != std::string::npos);  // closed padlock (secured)
    TEST_ASSERT_TRUE(html.find("&#128275;") != std::string::npos);  // open padlock (open AP)
}

void test_portal_page_has_rescan_and_manual_entry() {
    std::string html = renderProvisioningPage({{"AlphaNet", -45, true}});
    TEST_ASSERT_TRUE(html.find("href='/rescan'") != std::string::npos);   // rescan button
    TEST_ASSERT_TRUE(html.find("id='ssid' name='ssid'") != std::string::npos);  // manual fallback
    TEST_ASSERT_TRUE(html.find("http-equiv='refresh'") == std::string::npos);   // not scanning
}

void test_portal_page_scanning_state() {
    std::string html = renderProvisioningPage({}, "", -1, /*scanning=*/true);
    TEST_ASSERT_TRUE(html.find("http-equiv='refresh'") != std::string::npos);  // auto-polls
    TEST_ASSERT_TRUE(html.find("Scanning") != std::string::npos);
}

// The WiFi key is an EXISTING credential the rider already knows. A native type=password field
// makes iOS Safari (and the Captive Network Assistant webview) pop the "Use Strong Password"
// generator + a save prompt, which is useless here and gets in the way. The page must therefore
// render a MASKED TEXT field (never classified as a credential) instead of type=password, with a
// Show/Hide reveal toggle. See forward-plan §8.
void test_portal_page_password_not_a_credential_field() {
    std::string html = renderProvisioningPage({{"AlphaNet", -45, true}});
    // No native password field, and nothing that signals a *new* password (the generator trigger).
    TEST_ASSERT_TRUE(html.find("type='password'") == std::string::npos);
    TEST_ASSERT_TRUE(html.find("new-password") == std::string::npos);
    // The dot-mask is CSS (-webkit-text-security), honoured by WebKit + Blink (all captive browsers).
    TEST_ASSERT_TRUE(html.find("-webkit-text-security:disc") != std::string::npos);
    // The pass input carries the credential-suppressing attributes (text + autocomplete off, etc.).
    size_t pass = html.find("id='pass' name='pass'");
    TEST_ASSERT_TRUE(pass != std::string::npos);
    std::string tag = html.substr(pass, html.find('>', pass) - pass);
    TEST_ASSERT_TRUE(tag.find("type='text'") != std::string::npos);
    TEST_ASSERT_TRUE(tag.find("autocomplete='off'") != std::string::npos);
    TEST_ASSERT_TRUE(tag.find("autocapitalize='off'") != std::string::npos);
    TEST_ASSERT_TRUE(tag.find("autocorrect='off'") != std::string::npos);
    TEST_ASSERT_TRUE(tag.find("spellcheck='false'") != std::string::npos);
    // A Show/Hide toggle replaces the native password reveal (field is no longer type=password).
    TEST_ASSERT_TRUE(html.find("revealPass(this)") != std::string::npos);
    TEST_ASSERT_TRUE(html.find("class='reveal'") != std::string::npos);
}

// --- diagnostic log endpoint (ring buffer + /log toggle footer) ---------------

void test_logbuffer_keeps_recent_in_order() {
    LogBuffer log(3);
    log.add("one");
    log.add("two");
    log.add("three");
    TEST_ASSERT_EQUAL_UINT(3, log.count());
    TEST_ASSERT_EQUAL_STRING("one\ntwo\nthree\n", log.text().c_str());  // oldest-first
}

void test_logbuffer_drops_oldest_past_capacity() {
    LogBuffer log(2);
    log.add("a");
    log.add("b");
    log.add("c");  // evicts "a"
    TEST_ASSERT_EQUAL_UINT(2, log.count());
    TEST_ASSERT_EQUAL_STRING("b\nc\n", log.text().c_str());
}

void test_logbuffer_caps_line_length() {
    LogBuffer log(4);
    log.add(std::string(LogBuffer::kMaxLine + 50, 'x'));
    // stored line is truncated to kMaxLine (+1 for the trailing newline)
    TEST_ASSERT_EQUAL_UINT(LogBuffer::kMaxLine + 1, log.text().size());
}

void test_tohex_encodes_bytes() {
    const uint8_t b[] = {0x2f, 0x00, 0xae, 0x0c};
    TEST_ASSERT_EQUAL_STRING("2f00ae0c", toHex(b, sizeof(b)).c_str());
    TEST_ASSERT_EQUAL_STRING("", toHex(b, 0).c_str());  // empty is safe
}

void test_log_toggle_footer_states() {
    TEST_ASSERT_EQUAL_STRING("", renderLogToggleFooter(-1).c_str());  // hidden
    TEST_ASSERT_TRUE(renderLogToggleFooter(1).find("/log/off") != std::string::npos);  // on
    TEST_ASSERT_TRUE(renderLogToggleFooter(0).find("/log/on") != std::string::npos);   // off
}

void test_portal_page_shows_log_toggle_when_requested() {
    TEST_ASSERT_TRUE(renderProvisioningPage({}, "", 1).find("/log/off") != std::string::npos);
    // default (no logState arg) hides the footer entirely
    TEST_ASSERT_TRUE(renderProvisioningPage({}, "").find("/log") == std::string::npos);
}

// --- status LED ---------------------------------------------------------------

void test_status_led_searching_fast_blink() {
    // Searching toggles every SEARCHING_HALF_MS (120 ms): on [0,120), off [120,240), on [240,…).
    TEST_ASSERT_TRUE(StatusLed::lit(LinkState::Searching, 0));
    TEST_ASSERT_TRUE(StatusLed::lit(LinkState::Searching, 119));
    TEST_ASSERT_FALSE(StatusLed::lit(LinkState::Searching, 120));
    TEST_ASSERT_FALSE(StatusLed::lit(LinkState::Searching, 239));
    TEST_ASSERT_TRUE(StatusLed::lit(LinkState::Searching, 240));
}

void test_status_led_connected_slow_pulse() {
    // Connected toggles every CONNECTED_HALF_MS (1000 ms).
    TEST_ASSERT_TRUE(StatusLed::lit(LinkState::Connected, 0));
    TEST_ASSERT_TRUE(StatusLed::lit(LinkState::Connected, 999));
    TEST_ASSERT_FALSE(StatusLed::lit(LinkState::Connected, 1000));
    TEST_ASSERT_TRUE(StatusLed::lit(LinkState::Connected, 2000));
}

void test_status_led_searching_blinks_faster_than_connected() {
    // At 120 ms the fast (searching) LED has already toggled off while the slow (connected) one is
    // still in its first ON — the periods differ in the expected direction.
    TEST_ASSERT_FALSE(StatusLed::lit(LinkState::Searching, 120));
    TEST_ASSERT_TRUE(StatusLed::lit(LinkState::Connected, 120));
    TEST_ASSERT_TRUE(StatusLed::CONNECTED_HALF_MS > StatusLed::SEARCHING_HALF_MS);
}

// --- OLED screen --------------------------------------------------------------

void test_oled_portal_lines() {
    auto l = formatOledLines(OledMode::Portal, std::string(), 0, 0);
    TEST_ASSERT_EQUAL_STRING("SB20 SETUP", l[0].c_str());
    TEST_ASSERT_EQUAL_STRING("SB20-Setup", l[2].c_str());
    TEST_ASSERT_EQUAL_STRING("192.168.4.1", l[3].c_str());
}

void test_oled_connected_lines() {
    auto l = formatOledLines(OledMode::Connected, "192.168.1.82", 230, 85);
    TEST_ASSERT_EQUAL_STRING("SB20 PROXY", l[0].c_str());
    TEST_ASSERT_EQUAL_STRING("192.168.1.82", l[1].c_str());  // the IP — the thing you came for
    TEST_ASSERT_EQUAL_STRING("230W 85rpm", l[2].c_str());    // power + cadence share row 3 (3-row panel)
    TEST_ASSERT_EQUAL_STRING("", l[3].c_str());
}

void test_oled_connected_shows_balance_compact() {
    // With an L/R split, row 3 appends a compact "L44" and drops the "rpm" unit so it still fits
    // the ~12-char panel; the IP stays put.
    auto l = formatOledLines(OledMode::Connected, "192.168.1.82", 230, 85, 0, /*balancePct=*/44);
    TEST_ASSERT_EQUAL_STRING("230W 85 L44", l[2].c_str());
    TEST_ASSERT_TRUE(l[2].size() <= 12);
}

void test_oled_connected_unknown_cadence_omitted() {
    auto l = formatOledLines(OledMode::Connected, "10.0.0.5", 120, -1);
    TEST_ASSERT_EQUAL_STRING("120W", l[2].c_str());  // cadence unknown -> power only, no rpm suffix
    TEST_ASSERT_EQUAL_STRING("", l[3].c_str());      // cadence < 0 (unknown) -> blank row
}

void test_oled_connected_shows_rssi() {
    // RSSI (negative dBm) rides the title row; the IP keeps its own row. rssi default 0 -> brand.
    auto l = formatOledLines(OledMode::Connected, "192.168.1.82", 230, 85, -68);
    TEST_ASSERT_EQUAL_STRING("WiFi -68", l[0].c_str());
    TEST_ASSERT_EQUAL_STRING("192.168.1.82", l[1].c_str());  // IP unchanged
    TEST_ASSERT_EQUAL_STRING("230W 85rpm", l[2].c_str());    // power+cadence still share the row
    auto plain = formatOledLines(OledMode::Connected, "192.168.1.82", 230, 85);  // no rssi
    TEST_ASSERT_EQUAL_STRING("SB20 PROXY", plain[0].c_str());
}

// --- saved page ---------------------------------------------------------------

void test_saved_page_has_ssid_and_hints() {
    std::string p = renderSavedPage("Donnie Boon");
    TEST_ASSERT_TRUE(p.find("Donnie Boon") != std::string::npos);  // shows the chosen network
    TEST_ASSERT_TRUE(p.find("LED") != std::string::npos);          // LED hint present
    TEST_ASSERT_TRUE(p.find("OLED") != std::string::npos);         // OLED / IP hint present
    TEST_ASSERT_TRUE(p.find("restart") != std::string::npos);      // tells the user it reboots
    TEST_ASSERT_TRUE(p.find("Source") != std::string::npos);       // first-run handoff to /setup
}

void test_saved_page_escapes_ssid() {
    std::string p = renderSavedPage("<script>");
    TEST_ASSERT_TRUE(p.find("<script>") == std::string::npos);     // never injected raw
    TEST_ASSERT_TRUE(p.find("&lt;script&gt;") != std::string::npos);
}

// --- web dashboard (the /ui page) ---------------------------------------------

void test_app_page_essentials() {
    std::string p = appPageHtml();
    TEST_ASSERT_TRUE(p.find("SB20 Proxy") != std::string::npos);   // titled
    TEST_ASSERT_TRUE(p.find("fetch('/status'") != std::string::npos);  // polls /status (not /)
    TEST_ASSERT_TRUE(p.find("<canvas") != std::string::npos);      // the live sparkline
    TEST_ASSERT_TRUE(p.find("power_w") != std::string::npos);      // reads the broadcast power field
    TEST_ASSERT_TRUE(p.find("src_power_w") != std::string::npos);  // reads the received power field
    TEST_ASSERT_TRUE(p.find("d.identity") != std::string::npos);   // shows the OUT identity (in->out title)
    TEST_ASSERT_TRUE(p.find(">IN<") != std::string::npos);         // the expandable IN card badge
    TEST_ASSERT_TRUE(p.find(">OUT<") != std::string::npos);        // the OUT card badge
    TEST_ASSERT_TRUE(p.find(">POWER<") != std::string::npos);      // the big power hero
    TEST_ASSERT_TRUE(p.find("balance_pct") != std::string::npos);  // shows the L/R balance
    TEST_ASSERT_TRUE(p.find("class='nav'") != std::string::npos);  // the bottom Ride/Setup/More nav
    TEST_ASSERT_TRUE(p.find("href='/setup'") != std::string::npos);    // link to pick the source
    TEST_ASSERT_TRUE(p.find("href='/wifi/off'") != std::string::npos); // ride-mode (WiFi off) link
    TEST_ASSERT_TRUE(p.find("href='/report'") != std::string::npos);   // the review-&-send report page
}

void test_report_page_review_and_send() {
    std::string p = diagReportPageHtml();
    TEST_ASSERT_TRUE(p.find("fetch('/diag'") != std::string::npos);   // shows the raw report for review
    TEST_ASSERT_TRUE(p.find("Download") != std::string::npos);        // one-tap download
    TEST_ASSERT_TRUE(p.find("mailto:") != std::string::npos);         // email-us affordance
    TEST_ASSERT_TRUE(p.find("No location or personal data") != std::string::npos);  // consent copy
    TEST_ASSERT_TRUE(p.find("until you") != std::string::npos);       // "nothing leaves until you act"
    TEST_ASSERT_TRUE(p.find("href='/'") != std::string::npos);        // back to the dashboard
}

void test_diag_report() {
    RuntimeConfig cfg = RuntimeConfig::defaults();
    cfg.meterAddress = "e6:20:90:8c:f3:fe";
    cfg.spoofName = "Stages 62144";
    ProxyStatus st;
    st.sourceConnected = true;
    st.srcName = "ASSIOMA17039L";
    st.srcPowerW = 158;
    st.srcBalanceHalfPct = 88;  // 44 %
    std::vector<std::string> frames = {"23009e005816134e4d", "23009f005a1a13915a"};
    std::string r = renderDiagReport(cfg, st, frames);
    TEST_ASSERT_TRUE(r.find("SB20 Proxy diagnostic") != std::string::npos);
    TEST_ASSERT_TRUE(r.find("source_addr=e6:20:90:8c:f3:fe") != std::string::npos);  // config
    TEST_ASSERT_TRUE(r.find("spoof_name=Stages 62144") != std::string::npos);
    TEST_ASSERT_TRUE(r.find("ASSIOMA17039L") != std::string::npos);                  // status
    TEST_ASSERT_TRUE(r.find("src_balance_pct=44") != std::string::npos);
    TEST_ASSERT_TRUE(r.find("23009e005816134e4d") != std::string::npos);            // raw frames
    TEST_ASSERT_TRUE(r.find("23009f005a1a13915a") != std::string::npos);
    // empty frame set -> a helpful note, not a crash
    TEST_ASSERT_TRUE(renderDiagReport(cfg, st, {}).find("none yet") != std::string::npos);
}

void test_ride_mode_pages() {
    std::string c = rideModeConfirmHtml();
    TEST_ASSERT_TRUE(c.find("WiFi off") != std::string::npos);
    TEST_ASSERT_TRUE(c.find("action='/wifi/off'") != std::string::npos);  // POSTs to do it
    TEST_ASSERT_TRUE(c.find("power-cycle") != std::string::npos);         // how to undo it
    std::string d = rideModeDoneHtml();
    TEST_ASSERT_TRUE(d.find("Bluetooth-only") != std::string::npos);
    TEST_ASSERT_TRUE(d.find("Power-cycle") != std::string::npos);  // capitalised in the done page
}

// --- perf monitor / stats (the load-observability core) -----------------------

void test_perf_monitor_basic() {
    PerfMonitor m;
    for (int i = 0; i <= 100; ++i) m.sample((uint64_t)i * 1000);  // 100 deltas of 1 ms
    LoopStats s = m.summary();
    TEST_ASSERT_EQUAL_UINT32(100, s.count);
    TEST_ASSERT_EQUAL_UINT32(1000, s.meanUs);
    TEST_ASSERT_EQUAL_UINT32(1000, s.maxUs);
    TEST_ASSERT_EQUAL_UINT32(0, s.stalls50);
    TEST_ASSERT_EQUAL_UINT32(2000, s.p95Us);  // 1 ms lands in the [1000,2000) bucket (upper edge)
}

void test_perf_monitor_stalls() {
    PerfMonitor m;
    m.sample(0);
    m.sample(1000);     // dt 1 ms
    m.sample(61000);    // dt 60 ms  -> stall50
    m.sample(300000);   // dt 239 ms -> stall50 + stall200
    LoopStats s = m.summary();
    TEST_ASSERT_EQUAL_UINT32(3, s.count);
    TEST_ASSERT_EQUAL_UINT32(239000, s.maxUs);
    TEST_ASSERT_EQUAL_UINT32(2, s.stalls50);
    TEST_ASSERT_EQUAL_UINT32(1, s.stalls200);
}

void test_perf_monitor_reset() {
    PerfMonitor m;
    m.sample(0);
    m.sample(5000);
    m.reset();
    LoopStats s = m.summary();
    TEST_ASSERT_EQUAL_UINT32(0, s.count);
    TEST_ASSERT_EQUAL_UINT32(0, s.maxUs);
}

void test_perf_frag_and_reset_reason() {
    TEST_ASSERT_EQUAL_INT(40, fragPct(1000, 600));  // 1 - 600/1000
    TEST_ASSERT_EQUAL_INT(0, fragPct(1000, 1000));  // no fragmentation
    TEST_ASSERT_EQUAL_STRING("task_wdt", resetReasonName(6));
    TEST_ASSERT_EQUAL_STRING("poweron", resetReasonName(1));
    TEST_ASSERT_EQUAL_STRING("brownout", resetReasonName(9));
    TEST_ASSERT_EQUAL_STRING("unknown", resetReasonName(99));
}

void test_perf_json_fields() {
    PerfStats p;
    p.loop.count = 1200;
    p.loop.p95Us = 5000;
    p.loop.maxUs = 60000;
    p.loop.stalls50 = 3;
    p.freeHeap = 130000;
    p.largestBlock = 90000;
    p.rebootCount = 2;
    p.resetReasonCode = 6;  // task_wdt
    std::string j = renderPerfJson(p);
    TEST_ASSERT_TRUE(j.find("\"loop_p95_us\":5000") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"loop_max_us\":60000") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"stalls_50ms\":3") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"frag_pct\":31") != std::string::npos);  // 100 - 90000*100/130000 = 31 (int trunc)
    TEST_ASSERT_TRUE(j.find("\"reboot_count\":2") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"reset_reason\":\"task_wdt\"") != std::string::npos);
}

// --- FTMS codec (Ftms.h) — the C++ twin of sb20proxy.ble.ftms ------------------
// SPEC-BUILT (Session 4 Part C captures will pin these). Vectors match the Python
// SPEC_VECTORS byte-for-byte so the two codecs agree on the wire.

void test_ftms_ibd_encode_matches_spec_vector() {
    // (30.00 km/h, 90 rpm, 200 W) -> the documented frame 4400 b80b b400 c800
    std::vector<uint8_t> f = encodeIndoorBikeData(200, 90.0f, /*haveSpeed=*/true, 30.0f);
    const uint8_t want[] = {0x44, 0x00, 0xb8, 0x0b, 0xb4, 0x00, 0xc8, 0x00};
    TEST_ASSERT_EQUAL_INT(8, (int)f.size());
    for (size_t i = 0; i < 8; ++i) TEST_ASSERT_EQUAL_HEX8(want[i], f[i]);
}

void test_ftms_ibd_decode_speed_cadence_power() {
    const uint8_t frame[] = {0x44, 0x00, 0xb8, 0x0b, 0xb4, 0x00, 0xc8, 0x00};
    IndoorBikeData d = decodeIndoorBikeData(frame, sizeof(frame));
    TEST_ASSERT_TRUE(d.hasPower && d.hasCadence && d.hasSpeed);
    TEST_ASSERT_EQUAL_INT(200, d.instPower);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, d.cadenceRpm());
    TEST_ASSERT_EQUAL_FLOAT(30.0f, d.speedKmh());
}

void test_ftms_ibd_more_data_inversion() {
    // power-only (no speed) -> More-Data bit set, no speed field
    std::vector<uint8_t> f = encodeIndoorBikeData(150, 80.0f);
    IndoorBikeData d = decodeIndoorBikeData(f.data(), f.size());
    TEST_ASSERT_TRUE((d.flags & IBD_MORE_DATA) != 0);
    TEST_ASSERT_FALSE(d.hasSpeed);
    TEST_ASSERT_EQUAL_INT(150, d.instPower);
}

void test_ftms_ibd_short_frame_is_safe() {
    uint8_t two[2] = {0x40, 0x00};  // claims power but truncated -> no crash, hasPower false
    IndoorBikeData d = decodeIndoorBikeData(two, 2);
    TEST_ASSERT_EQUAL_INT(0x40, (int)(d.flags & 0xFF));
}

void test_ftms_set_target_power_bytes() {
    std::vector<uint8_t> f = encodeSetTargetPower(250);
    const uint8_t want[] = {0x05, 0xfa, 0x00};
    TEST_ASSERT_EQUAL_INT(3, (int)f.size());
    for (size_t i = 0; i < 3; ++i) TEST_ASSERT_EQUAL_HEX8(want[i], f[i]);
}

void test_ftms_cp_decode_set_target_power_request() {
    const uint8_t req[] = {0x05, 0xfa, 0x00};
    FtmsCpMessage m = decodeControlPoint(req, sizeof(req));
    TEST_ASSERT_TRUE(m.valid && !m.isResponse && m.hasTargetPower);
    TEST_ASSERT_EQUAL_INT(250, m.targetPower);
}

void test_ftms_cp_decode_response() {
    const uint8_t ok[] = {0x80, 0x05, 0x01};
    FtmsCpMessage m = decodeControlPoint(ok, sizeof(ok));
    TEST_ASSERT_TRUE(m.isResponse && m.success());
    TEST_ASSERT_EQUAL_INT(0x05, m.requestOpcode);
    const uint8_t no[] = {0x80, 0x05, 0x05};
    TEST_ASSERT_FALSE(decodeControlPoint(no, sizeof(no)).success());
}

void test_ftms_feature_and_power_range_and_status() {
    // feature: cadence|power-measurement machine + power target setting
    const uint8_t feat[] = {0x02, 0x40, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00};
    FtmsFeature f = decodeFitnessMachineFeature(feat, sizeof(feat));
    TEST_ASSERT_TRUE(f.cadence() && f.powerMeasurement() && f.powerTargetSetting());
    // power range 0..1000 step 1, clamped
    const uint8_t pr[] = {0x00, 0x00, 0xe8, 0x03, 0x01, 0x00};
    FtmsPowerRange r = decodeSupportedPowerRange(pr, sizeof(pr));
    TEST_ASSERT_EQUAL_INT(0, r.minimum);
    TEST_ASSERT_EQUAL_INT(1000, r.maximum);
    TEST_ASSERT_EQUAL_INT(1000, r.clamp(5000));
    TEST_ASSERT_EQUAL_INT(0, r.clamp(-10));
    // status: target power changed -> 200 W
    const uint8_t st[] = {0x08, 0xc8, 0x00};
    FtmsStatus s = decodeFitnessMachineStatus(st, sizeof(st));
    TEST_ASSERT_TRUE(s.hasTargetPower);
    TEST_ASSERT_EQUAL_INT(200, s.targetPower);
}

// --- CSRF / same-origin guard (Vuln 2) ---------------------------------------

void test_request_authority_extracts_host() {
    TEST_ASSERT_EQUAL_STRING("192.168.1.165", requestAuthority("http://192.168.1.165/forget").c_str());
    TEST_ASSERT_EQUAL_STRING("sb20proxy.local:8080",
                             requestAuthority("https://sb20proxy.local:8080/setup/save").c_str());
    TEST_ASSERT_EQUAL_STRING("192.168.4.1", requestAuthority("192.168.4.1").c_str());  // bare Host header
    TEST_ASSERT_EQUAL_STRING("evil.com", requestAuthority("http://evil.com/?x=1").c_str());
    TEST_ASSERT_EQUAL_STRING("", requestAuthority("").c_str());
}

void test_same_origin_allows_tools_and_self() {
    // No Origin/Referer (curl, our scripts, same-origin GET) -> allowed.
    TEST_ASSERT_TRUE(isSameOriginRequest("sb20proxy.local", "", ""));
    // Origin matches our Host -> our own page POSTing back -> allowed.
    TEST_ASSERT_TRUE(isSameOriginRequest("192.168.1.165", "http://192.168.1.165", ""));
    // Referer (no Origin) from our own page -> allowed.
    TEST_ASSERT_TRUE(isSameOriginRequest("sb20proxy.local", "", "http://sb20proxy.local/setup"));
}

void test_same_origin_blocks_cross_site() {
    // A malicious page POSTing to us -> Origin authority differs -> blocked (the /forget CSRF).
    TEST_ASSERT_FALSE(isSameOriginRequest("sb20proxy.local", "http://evil.com", ""));
    // Origin absent but a cross-site Referer -> blocked.
    TEST_ASSERT_FALSE(isSameOriginRequest("192.168.1.165", "", "http://evil.com/attack.html"));
    // Origin wins over Referer: a forged Origin is checked even if Referer looks local.
    TEST_ASSERT_FALSE(isSameOriginRequest("sb20proxy.local", "http://evil.com",
                                          "http://sb20proxy.local/"));
    // No Host to validate against -> refuse rather than guess.
    TEST_ASSERT_FALSE(isSameOriginRequest("", "http://evil.com", ""));
}

// --- signed-OTA manifest (pure) ----------------------------------------------

void test_manifest_parse_and_validate() {
    const std::string body =
        "{\"version\":\"0.2.0\",\"url\":\"https://ota.example/fw-0.2.0.bin\",\"size\":1094042,"
        "\"blake2b\":\"" + std::string(128, 'a') + "\",\"sig\":\"" + std::string(128, 'b') + "\"}";
    const OtaManifest m = parseManifest(body);
    TEST_ASSERT_TRUE(m.valid);
    TEST_ASSERT_EQUAL_STRING("0.2.0", m.version.c_str());
    TEST_ASSERT_EQUAL_STRING("https://ota.example/fw-0.2.0.bin", m.url.c_str());
    TEST_ASSERT_EQUAL_INT(1094042, (int)m.size);
}

void test_manifest_rejects_malformed() {
    // Short digest -> invalid (so a truncated manifest can never trigger an update).
    const std::string shortDigest =
        "{\"version\":\"1.0.0\",\"url\":\"u\",\"size\":10,\"blake2b\":\"abcd\",\"sig\":\"" +
        std::string(128, 'b') + "\"}";
    TEST_ASSERT_FALSE(parseManifest(shortDigest).valid);
    // Missing fields / empty body -> invalid.
    TEST_ASSERT_FALSE(parseManifest("{}").valid);
    TEST_ASSERT_FALSE(parseManifest("not json").valid);
    // size 0 -> invalid.
    const std::string zeroSize =
        "{\"version\":\"1.0.0\",\"url\":\"u\",\"size\":0,\"blake2b\":\"" + std::string(128, 'a') +
        "\",\"sig\":\"" + std::string(128, 'b') + "\"}";
    TEST_ASSERT_FALSE(parseManifest(zeroSize).valid);
}

void test_version_compare_and_should_update() {
    TEST_ASSERT_TRUE(isNewerVersion("0.1.0", "0.2.0"));
    TEST_ASSERT_TRUE(isNewerVersion("0.1.9", "0.2.0"));
    TEST_ASSERT_TRUE(isNewerVersion("1.0.0", "1.0.1"));
    TEST_ASSERT_FALSE(isNewerVersion("0.2.0", "0.2.0"));  // equal
    TEST_ASSERT_FALSE(isNewerVersion("0.2.0", "0.1.9"));  // older
    TEST_ASSERT_FALSE(isNewerVersion("2.0.0", "1.9.9"));
    OtaManifest m;
    m.valid = true;
    m.version = "0.3.0";
    TEST_ASSERT_TRUE(shouldUpdate("0.2.0", m));
    TEST_ASSERT_FALSE(shouldUpdate("0.3.0", m));
    m.valid = false;  // a malformed manifest never updates, even if "newer"
    TEST_ASSERT_FALSE(shouldUpdate("0.1.0", m));
}

void test_hex_decode_roundtrip_and_rejects_bad() {
    std::vector<uint8_t> out;
    TEST_ASSERT_TRUE(hexDecode("00ff10ab", out));
    TEST_ASSERT_EQUAL_INT(4, (int)out.size());
    TEST_ASSERT_EQUAL_HEX8(0xff, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xab, out[3]);
    TEST_ASSERT_FALSE(hexDecode("abc", out));   // odd length
    TEST_ASSERT_FALSE(hexDecode("zz", out));     // non-hex
}

// --- signed-OTA crypto: golden vectors from the Python signer (sb20proxy.ota.sign) ----------------
// Deterministic test key (seed = bytes 0..31) signing a deterministic "image" (bytes 0..255). The
// digest/sig/pubkey below were produced by cryptography (ed25519) + hashlib.blake2b on the signing
// side; this asserts the vendored monocypher device verifier accepts them — i.e. Python-sign <-> C-verify
// interop, and BLAKE2b agreement. Regenerate via code/tests/test_ota_sign.py if the scheme changes.
namespace {
std::vector<uint8_t> testImage() {  // bytes 0x00..0xff
    std::vector<uint8_t> v(256);
    for (int i = 0; i < 256; ++i) v[i] = (uint8_t)i;
    return v;
}
const char* kGoldenPubKey =
    "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8";
const char* kGoldenDigest =
    "1ecc896f34d3f9cac484c73f75f6a5fb58ee6784be41b35f46067b9c65c63a67"
    "94d3d744112c653f73dd7deb6666204c5a9bfa5b46081fc10fdbe7884fa5cbf8";
const char* kGoldenSig =
    "b2e355e1bbc88f6f324fe8b444197d0f301162d91fb9484523afb0e77f64e769"
    "eae7217d166ebe394e9addd3589406f01a7b614c06787bb6bff227689a63cb01";
}  // namespace

void test_blake2b_matches_python_digest() {
    const std::vector<uint8_t> img = testImage();
    uint8_t digest[kImageDigestSize];
    imageDigest(img.data(), img.size(), digest);
    std::vector<uint8_t> expected;
    TEST_ASSERT_TRUE(hexDecode(kGoldenDigest, expected));
    TEST_ASSERT_EQUAL_INT(kImageDigestSize, (int)expected.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), digest, kImageDigestSize);
    // Streamed (chunked) hashing must agree with one-shot.
    ImageHasher h;
    h.update(img.data(), 100);
    h.update(img.data() + 100, img.size() - 100);
    uint8_t streamed[kImageDigestSize];
    h.finish(streamed);
    TEST_ASSERT_TRUE(digestsEqual(digest, streamed));
}

void test_ed25519_verifies_python_signature() {
    std::vector<uint8_t> pub, digest, sig;
    TEST_ASSERT_TRUE(hexDecode(kGoldenPubKey, pub));
    TEST_ASSERT_TRUE(hexDecode(kGoldenDigest, digest));
    TEST_ASSERT_TRUE(hexDecode(kGoldenSig, sig));
    // The real device path: recompute the digest from the image, confirm it matches the manifest's,
    // then verify the signature over it.
    const std::vector<uint8_t> img = testImage();
    uint8_t computed[kImageDigestSize];
    imageDigest(img.data(), img.size(), computed);
    TEST_ASSERT_TRUE(digestsEqual(computed, digest.data()));
    TEST_ASSERT_TRUE(verifyImageSignature(digest.data(), sig.data(), pub.data()));
}

void test_ed25519_rejects_tampering() {
    std::vector<uint8_t> pub, digest, sig;
    hexDecode(kGoldenPubKey, pub);
    hexDecode(kGoldenDigest, digest);
    hexDecode(kGoldenSig, sig);
    // Flip one signature byte -> reject.
    std::vector<uint8_t> badSig = sig;
    badSig[10] ^= 0x01;
    TEST_ASSERT_FALSE(verifyImageSignature(digest.data(), badSig.data(), pub.data()));
    // Flip one digest byte (a tampered image) -> reject.
    std::vector<uint8_t> badDigest = digest;
    badDigest[0] ^= 0x80;
    TEST_ASSERT_FALSE(verifyImageSignature(badDigest.data(), sig.data(), pub.data()));
    // Wrong public key (flip a byte) -> reject.
    std::vector<uint8_t> badPub = pub;
    badPub[5] ^= 0x01;
    TEST_ASSERT_FALSE(verifyImageSignature(digest.data(), sig.data(), badPub.data()));
}

// --- signed-OTA updater orchestration (pure, with fakes) -------------------------------------------
namespace {
struct FakeFetcher : IFirmwareFetcher {
    std::string manifestUrl, manifestBody, imageUrl;
    std::vector<uint8_t> image;
    bool manifestOk = true, imageOk = true;
    bool fetchText(const std::string& url, std::string& out) override {
        if (!manifestOk || url != manifestUrl) return false;
        out = manifestBody;
        return true;
    }
    bool fetchStream(const std::string& url,
                     const std::function<bool(const uint8_t*, size_t)>& sink) override {
        if (!imageOk || url != imageUrl) return false;
        const size_t mid = image.size() / 2;  // two chunks, to exercise streaming
        if (!sink(image.data(), mid)) return false;
        if (!sink(image.data() + mid, image.size() - mid)) return false;
        return true;
    }
};
struct FakeWriter : IFirmwareWriter {
    std::vector<uint8_t> written;
    bool began = false, ended = false, aborted = false;
    bool failBegin = false, failEnd = false;
    bool begin(size_t) override { began = true; return !failBegin; }
    bool write(const uint8_t* d, size_t n) override {
        written.insert(written.end(), d, d + n);
        return true;
    }
    bool end() override { ended = true; return !failEnd; }
    void abort() override { aborted = true; }
};
std::string otaManifest(const std::string& version, const std::string& url, long size,
                        const char* digestHex, const char* sigHex) {
    return "{\"version\":\"" + version + "\",\"url\":\"" + url + "\",\"size\":" +
           std::to_string(size) + ",\"blake2b\":\"" + digestHex + "\",\"sig\":\"" + sigHex + "\"}";
}
// Wire a fetcher to serve the golden image at a fixed pair of URLs.
void wireGolden(FakeFetcher& f, const std::string& manifestBody) {
    f.manifestUrl = "https://ota.local/manifest.json";
    f.imageUrl = "https://ota.local/fw.bin";
    f.manifestBody = manifestBody;
    f.image = testImage();
}
}  // namespace

void test_ota_applies_a_valid_signed_update() {
    std::vector<uint8_t> pub;
    hexDecode(kGoldenPubKey, pub);
    FakeFetcher f;
    FakeWriter w;
    wireGolden(f, otaManifest("0.2.0", "https://ota.local/fw.bin", 256, kGoldenDigest, kGoldenSig));
    OtaUpdater up(pub.data(), f, w);
    const OtaResult r = up.checkAndApply(f.manifestUrl, "0.1.0");
    TEST_ASSERT_EQUAL_INT((int)OtaResult::Applied, (int)r);
    TEST_ASSERT_TRUE(w.ended);
    TEST_ASSERT_FALSE(w.aborted);
    TEST_ASSERT_EQUAL_INT(256, (int)w.written.size());
}

void test_ota_skips_when_not_newer() {
    std::vector<uint8_t> pub;
    hexDecode(kGoldenPubKey, pub);
    FakeFetcher f;
    FakeWriter w;
    wireGolden(f, otaManifest("0.2.0", "https://ota.local/fw.bin", 256, kGoldenDigest, kGoldenSig));
    OtaUpdater up(pub.data(), f, w);
    const OtaResult r = up.checkAndApply(f.manifestUrl, "0.2.0");  // already on 0.2.0
    TEST_ASSERT_EQUAL_INT((int)OtaResult::UpToDate, (int)r);
    TEST_ASSERT_FALSE(w.began);  // never touched the flash
}

void test_ota_rejects_a_tampered_image() {
    std::vector<uint8_t> pub;
    hexDecode(kGoldenPubKey, pub);
    FakeFetcher f;
    FakeWriter w;
    wireGolden(f, otaManifest("0.2.0", "https://ota.local/fw.bin", 256, kGoldenDigest, kGoldenSig));
    f.image[42] ^= 0x01;  // flip a byte -> digest won't match the manifest
    OtaUpdater up(pub.data(), f, w);
    const OtaResult r = up.checkAndApply(f.manifestUrl, "0.1.0");
    TEST_ASSERT_EQUAL_INT((int)OtaResult::HashMismatch, (int)r);
    TEST_ASSERT_TRUE(w.aborted);
    TEST_ASSERT_FALSE(w.ended);
}

void test_ota_rejects_a_bad_signature() {
    std::vector<uint8_t> pub;
    hexDecode(kGoldenPubKey, pub);
    // Valid digest for the (untampered) image, but a corrupted signature.
    std::string badSig(kGoldenSig);
    badSig[0] = (badSig[0] == 'a') ? 'b' : 'a';
    FakeFetcher f;
    FakeWriter w;
    wireGolden(f, otaManifest("0.2.0", "https://ota.local/fw.bin", 256, kGoldenDigest, badSig.c_str()));
    OtaUpdater up(pub.data(), f, w);
    const OtaResult r = up.checkAndApply(f.manifestUrl, "0.1.0");
    TEST_ASSERT_EQUAL_INT((int)OtaResult::BadSignature, (int)r);
    TEST_ASSERT_TRUE(w.aborted);
    TEST_ASSERT_FALSE(w.ended);
}

void test_ota_rejects_size_mismatch_and_bad_manifest() {
    std::vector<uint8_t> pub;
    hexDecode(kGoldenPubKey, pub);
    {  // manifest claims a different size than the stream delivers
        FakeFetcher f;
        FakeWriter w;
        wireGolden(f, otaManifest("0.2.0", "https://ota.local/fw.bin", 255, kGoldenDigest, kGoldenSig));
        OtaUpdater up(pub.data(), f, w);
        const OtaResult r = up.checkAndApply(f.manifestUrl, "0.1.0");
        TEST_ASSERT_EQUAL_INT((int)OtaResult::SizeMismatch, (int)r);
        TEST_ASSERT_TRUE(w.aborted);
    }
    {  // garbage manifest -> never flashes
        FakeFetcher f;
        FakeWriter w;
        wireGolden(f, "not a manifest");
        OtaUpdater up(pub.data(), f, w);
        TEST_ASSERT_EQUAL_INT((int)OtaResult::BadManifest, (int)up.checkAndApply(f.manifestUrl, "0.1.0"));
        TEST_ASSERT_FALSE(w.began);
    }
    {  // manifest fetch fails outright
        FakeFetcher f;
        FakeWriter w;
        wireGolden(f, otaManifest("0.2.0", "https://ota.local/fw.bin", 256, kGoldenDigest, kGoldenSig));
        f.manifestOk = false;
        OtaUpdater up(pub.data(), f, w);
        TEST_ASSERT_EQUAL_INT((int)OtaResult::NoManifest, (int)up.checkAndApply(f.manifestUrl, "0.1.0"));
    }
}

// --- setup-AP PIN derivation + OLED portal row ----------------------------------------------------

void test_setup_pin_is_eight_digits() {
    const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33};
    const std::string pin = deriveSetupPin(mac, 6, "secret");
    TEST_ASSERT_EQUAL_INT(8, (int)pin.size());
    for (char c : pin) TEST_ASSERT_TRUE(c >= '0' && c <= '9');  // WPA2-typable, digits only
}

void test_setup_pin_deterministic_and_sensitive() {
    const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33};
    const uint8_t mac2[6] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x34};  // one byte different
    TEST_ASSERT_EQUAL_STRING(deriveSetupPin(mac, 6, "secret").c_str(),
                             deriveSetupPin(mac, 6, "secret").c_str());  // deterministic
    TEST_ASSERT_TRUE(deriveSetupPin(mac, 6, "secret") != deriveSetupPin(mac2, 6, "secret"));  // per-MAC
    TEST_ASSERT_TRUE(deriveSetupPin(mac, 6, "secret") != deriveSetupPin(mac, 6, "other"));   // per-secret
}

void test_oled_portal_shows_pin_when_present() {
    auto withPin = formatOledLines(OledMode::Portal, "", 0, -1, 0, -1, "12345678");
    TEST_ASSERT_EQUAL_STRING("SB20-Setup", withPin[1].c_str());
    TEST_ASSERT_EQUAL_STRING("PIN 12345678", withPin[2].c_str());
    auto noPin = formatOledLines(OledMode::Portal, "", 0, -1, 0, -1, "");  // open-AP fallback layout
    TEST_ASSERT_EQUAL_STRING("join wifi:", noPin[1].c_str());
}

// --- runner -------------------------------------------------------------------

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_setup_pin_is_eight_digits);
    RUN_TEST(test_setup_pin_deterministic_and_sensitive);
    RUN_TEST(test_oled_portal_shows_pin_when_present);
    RUN_TEST(test_request_authority_extracts_host);
    RUN_TEST(test_same_origin_allows_tools_and_self);
    RUN_TEST(test_same_origin_blocks_cross_site);
    RUN_TEST(test_manifest_parse_and_validate);
    RUN_TEST(test_manifest_rejects_malformed);
    RUN_TEST(test_version_compare_and_should_update);
    RUN_TEST(test_hex_decode_roundtrip_and_rejects_bad);
    RUN_TEST(test_blake2b_matches_python_digest);
    RUN_TEST(test_ed25519_verifies_python_signature);
    RUN_TEST(test_ed25519_rejects_tampering);
    RUN_TEST(test_ota_applies_a_valid_signed_update);
    RUN_TEST(test_ota_skips_when_not_newer);
    RUN_TEST(test_ota_rejects_a_tampered_image);
    RUN_TEST(test_ota_rejects_a_bad_signature);
    RUN_TEST(test_ota_rejects_size_mismatch_and_bad_manifest);
    RUN_TEST(test_correction_scale_offset);
    RUN_TEST(test_correction_clamps_at_zero);
    RUN_TEST(test_curve_empty_is_unity);
    RUN_TEST(test_curve_interpolates_and_holds_flat);
    RUN_TEST(test_curve_correction_takes_precedence);
    RUN_TEST(test_cps_measurement_roundtrip);
    RUN_TEST(test_cps_decode_short_frame_is_safe);
    RUN_TEST(test_calibration_response_bytes);
    RUN_TEST(test_cp_offset_comp_enhanced_0x10);
    RUN_TEST(test_cp_offset_comp_enhanced_0x10_real_crank);
    RUN_TEST(test_encode_enhanced_offset_comp_structure);
    RUN_TEST(test_cp_offset_comp_basic_0x0C);
    RUN_TEST(test_cp_request_source_zero_flag);
    RUN_TEST(test_cp_set_crank_length_0x04);
    RUN_TEST(test_cp_request_crank_length_0x05_stages_format);
    RUN_TEST(test_cp_set_then_request_crank_length_roundtrip);
    RUN_TEST(test_cp_unknown_op_not_supported);
    RUN_TEST(test_cp_malformed_is_safe);
    RUN_TEST(test_cp_set_crank_length_missing_param_invalid);
    RUN_TEST(test_meter_match_assioma_by_name);
    RUN_TEST(test_meter_match_rejects_real_stages_crank);
    RUN_TEST(test_meter_match_skips_our_own_spoof);
    RUN_TEST(test_meter_match_nameless_winrt_rig_by_cps);
    RUN_TEST(test_meter_match_winrt_rig_carries_host_name_blocked_in_prod);
    RUN_TEST(test_meter_match_any_cps_bench_flag);
    RUN_TEST(test_meter_match_pinned_address_wins);
    RUN_TEST(test_meter_match_pin_never_reads_own_spoof);
    RUN_TEST(test_cps_cadence_frame);
    RUN_TEST(test_crank_cadence_roundtrips_rpm);
    RUN_TEST(test_crank_cadence_coasting_no_events);
    RUN_TEST(test_stages_frame_golden_encode);
    RUN_TEST(test_stages_frame_flags_and_power);
    RUN_TEST(test_decode_crank_data_behind_preceding_fields);
    RUN_TEST(test_cadence_from_two_real_stages_frames);
    RUN_TEST(test_decode_cps_balance_from_real_assioma);
    RUN_TEST(test_decode_cps_balance_absent_when_bit_clear);
    RUN_TEST(test_decode_cps_balance_truncated_frame_is_safe);
    RUN_TEST(test_balance_survives_correction);
    RUN_TEST(test_balance_hold_is_sticky_across_balanceless_frames);
    RUN_TEST(test_balance_forwarded_to_spoof_frame);
    RUN_TEST(test_proxy_relays_power);
    RUN_TEST(test_runtime_config_defaults_from_compile_time);
    RUN_TEST(test_runtime_config_line_roundtrip);
    RUN_TEST(test_runtime_config_defaults_spoof_identity);
    RUN_TEST(test_runtime_config_old_line_keeps_default_identity);
    RUN_TEST(test_runtime_config_malformed_line_falls_back_to_defaults);
    RUN_TEST(test_strip_config_delims_protects_the_nvs_line);
    RUN_TEST(test_curve_string_keeps_zero_factor_rejects_garbage);
    RUN_TEST(test_band_label_guards_short_edges);
    RUN_TEST(test_url_encode_roundtrips_with_decode);
    RUN_TEST(test_config_form_parse);
    RUN_TEST(test_config_validation);
    RUN_TEST(test_add_candidate_dedup_and_cap);
    RUN_TEST(test_dedupe_and_sort_sources);
    RUN_TEST(test_render_config_page_marks_selected_and_badges);
    RUN_TEST(test_render_config_page_escapes_name);
    RUN_TEST(test_render_config_page_shows_spoof_identity);
    RUN_TEST(test_render_config_page_status_banner);
    RUN_TEST(test_proxy_set_correction_applies_single_sided_double);
    RUN_TEST(test_proxy_applies_correction);
    RUN_TEST(test_proxy_preserves_cadence);
    RUN_TEST(test_proxy_reset_clears_stale_readings);
    RUN_TEST(test_status_json_mock);
    RUN_TEST(test_status_json_no_balance_is_minus_one);
    RUN_TEST(test_status_json_mode_corrector);
    RUN_TEST(test_status_json_source_state);
    RUN_TEST(test_status_json_unknown_cadence);
    RUN_TEST(test_status_json_has_firmware_version);
    RUN_TEST(test_diag_report_has_firmware_version);
    RUN_TEST(test_firmware_version_feeds_ota_decision);
    RUN_TEST(test_form_parse_basic);
    RUN_TEST(test_form_parse_url_encoding);
    RUN_TEST(test_form_parse_empty_password);
    RUN_TEST(test_validate_accepts_wpa_and_open);
    RUN_TEST(test_validate_rejects_bad_creds);
    RUN_TEST(test_portal_page_has_form_fields);
    RUN_TEST(test_portal_page_escapes_ssid);
    RUN_TEST(test_rssi_bars_buckets);
    RUN_TEST(test_dedupe_and_sort_networks);
    RUN_TEST(test_portal_page_lists_networks_strongest_first);
    RUN_TEST(test_portal_page_marks_secured_and_open);
    RUN_TEST(test_portal_page_has_rescan_and_manual_entry);
    RUN_TEST(test_portal_page_scanning_state);
    RUN_TEST(test_portal_page_password_not_a_credential_field);
    RUN_TEST(test_logbuffer_keeps_recent_in_order);
    RUN_TEST(test_logbuffer_drops_oldest_past_capacity);
    RUN_TEST(test_logbuffer_caps_line_length);
    RUN_TEST(test_tohex_encodes_bytes);
    RUN_TEST(test_log_toggle_footer_states);
    RUN_TEST(test_portal_page_shows_log_toggle_when_requested);
    RUN_TEST(test_status_led_searching_fast_blink);
    RUN_TEST(test_status_led_connected_slow_pulse);
    RUN_TEST(test_status_led_searching_blinks_faster_than_connected);
    RUN_TEST(test_oled_portal_lines);
    RUN_TEST(test_oled_connected_lines);
    RUN_TEST(test_oled_connected_shows_balance_compact);
    RUN_TEST(test_oled_connected_unknown_cadence_omitted);
    RUN_TEST(test_oled_connected_shows_rssi);
    RUN_TEST(test_saved_page_has_ssid_and_hints);
    RUN_TEST(test_saved_page_escapes_ssid);
    RUN_TEST(test_app_page_essentials);
    RUN_TEST(test_report_page_review_and_send);
    RUN_TEST(test_diag_report);
    RUN_TEST(test_ride_mode_pages);
    RUN_TEST(test_perf_monitor_basic);
    RUN_TEST(test_perf_monitor_stalls);
    RUN_TEST(test_perf_monitor_reset);
    RUN_TEST(test_perf_frag_and_reset_reason);
    RUN_TEST(test_perf_json_fields);
    RUN_TEST(test_ftms_ibd_encode_matches_spec_vector);
    RUN_TEST(test_ftms_ibd_decode_speed_cadence_power);
    RUN_TEST(test_ftms_ibd_more_data_inversion);
    RUN_TEST(test_ftms_ibd_short_frame_is_safe);
    RUN_TEST(test_ftms_set_target_power_bytes);
    RUN_TEST(test_ftms_cp_decode_set_target_power_request);
    RUN_TEST(test_ftms_cp_decode_response);
    RUN_TEST(test_ftms_feature_and_power_range_and_status);
    RUN_TEST(test_runtime_config_defaults_to_spoof_mode);
    RUN_TEST(test_runtime_config_old_line_keeps_spoof_mode_and_no_curve);
    RUN_TEST(test_runtime_config_corrector_roundtrip_with_curve);
    RUN_TEST(test_fit_curve_matches_python_grid);
    RUN_TEST(test_fit_scale_offset_matches_python);
    RUN_TEST(test_fit_correction_prefers_curve_and_corrects);
    RUN_TEST(test_fit_correction_falls_back_to_linear_when_sparse);
    RUN_TEST(test_pair_accumulator_pairs_fresh_streams_only);
    RUN_TEST(test_pair_accumulator_drops_implausible_and_caps);
    RUN_TEST(test_pair_accumulator_coverage_bins);
    RUN_TEST(test_curve_string_roundtrip);
    RUN_TEST(test_shifter_decode_golden_buttons);
    RUN_TEST(test_shifter_debounce_one_event_per_press);
    RUN_TEST(test_calibration_session_lifecycle_and_fit);
    RUN_TEST(test_calibration_session_finish_needs_enough_pairs);
    RUN_TEST(test_calibration_session_cancel_resets);
    RUN_TEST(test_calibration_session_coverage_guides_the_rider);
    RUN_TEST(test_calibration_form_parse);
    RUN_TEST(test_calibration_start_validation);
    RUN_TEST(test_calibration_page_idle_lists_meters_and_pickers);
    RUN_TEST(test_calibration_page_collecting_shows_coverage_and_gates_finish);
    RUN_TEST(test_calibration_page_fitted_shows_curve_and_name);
    RUN_TEST(test_runtime_config_calibrating_roundtrip);
    RUN_TEST(test_correction_to_curve_passthrough_and_linear);
    RUN_TEST(test_proxycore_tap_sees_raw_source_reading);
    return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() { delay(2000); runUnityTests(); }
void loop() {}
#else
int main() { return runUnityTests(); }
#endif
