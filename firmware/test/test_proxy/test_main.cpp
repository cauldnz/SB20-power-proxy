// Host-side unit tests for the platform-agnostic proxy core (no hardware).
//
//   pio test -e native
//
// The firmware analogue of the Python suite: the CPS codec, the correction, and the
// ProxyCore relay are proven on the host BEFORE any of it touches a real meter or the SB20.

#include <unity.h>

#include "Correction.h"
#include "Cps.h"
#include "MeterMatch.h"
#include "LogBuffer.h"
#include "MockCrank.h"
#include "MockMeter.h"
#include "Provisioning.h"
#include "ProxyCore.h"
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
    std::vector<uint8_t> r = encodeCalibrationResponse(903);  // 903 = 0x0387 LE
    TEST_ASSERT_EQUAL_UINT8(0x20, r[0]);  // response op
    TEST_ASSERT_EQUAL_UINT8(0x0C, r[1]);  // start offset compensation
    TEST_ASSERT_EQUAL_UINT8(0x01, r[2]);  // success
    TEST_ASSERT_EQUAL_UINT8(0x87, r[3]);
    TEST_ASSERT_EQUAL_UINT8(0x03, r[4]);
}

// --- control-point handshake: the SB20's calibration/config (must be ANSWERED) ----------------
// The SB20 terminates the link if a CP procedure goes unanswered (bike-session 2: disconnect
// reason=531), so handleControlPoint replies to EVERY write. Formats grounded in captures: the
// Stages app sends the *Enhanced* offset-comp op 0x10 (not the basic 0x0C), and the Stages crank's
// Request-Crank-Length reply OMITS the success byte (`20 05 <len>`, unlike the Assioma's `20 05 01 <len>`).

void test_cp_offset_comp_enhanced_0x10() {
    const uint8_t req[] = {CP_OP_ENHANCED_OFFSET_COMP};
    CpResult r = handleControlPoint(req, sizeof(req), 345, 903);  // offset 903 = 0x0387 LE
    const uint8_t expected[] = {0x20, 0x10, 0x01, 0x87, 0x03};
    TEST_ASSERT_EQUAL_INT(5, (int)r.response.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r.response.data(), 5);
    TEST_ASSERT_FALSE(r.crankLengthChanged);
}

void test_cp_offset_comp_basic_0x0C() {
    const uint8_t req[] = {CP_OP_START_OFFSET_COMP};
    CpResult r = handleControlPoint(req, sizeof(req), 345, 903);
    const uint8_t expected[] = {0x20, 0x0C, 0x01, 0x87, 0x03};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, r.response.data(), 5);
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
    // The WinRT fake_meter rig advertises the CPS UUID with no name.
    TEST_ASSERT_TRUE(isTargetMeter("", true, "12:34:56:78:9a:bc", "", "Stages 62144", "ASSIOMA"));
    TEST_ASSERT_FALSE(isTargetMeter("", false, "12:34:56:78:9a:bc", "", "Stages 62144", "ASSIOMA"));
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

// --- status JSON (the HTTP observability model) -------------------------------

void test_status_json_mock() {
    ProxyStatus s;
    s.mock = true;
    s.forwarded = 5;
    s.srcPowerW = 220;
    s.srcCadenceRpm = 88;
    s.lastPowerW = 200;
    s.lastCadenceRpm = 90;
    s.uptimeMs = 12345;
    std::string j = renderStatusJson(s);
    TEST_ASSERT_TRUE(j.find("\"source\":\"mock\"") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"forwarded\":5") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"src_power_w\":220") != std::string::npos);   // received from meter
    TEST_ASSERT_TRUE(j.find("\"src_cadence_rpm\":88") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"power_w\":200") != std::string::npos);       // broadcast to crank
    TEST_ASSERT_TRUE(j.find("\"cadence_rpm\":90") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"ms\":12345") != std::string::npos);
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

void test_oled_connected_unknown_cadence_omitted() {
    auto l = formatOledLines(OledMode::Connected, "10.0.0.5", 120, -1);
    TEST_ASSERT_EQUAL_STRING("120W", l[2].c_str());  // cadence unknown -> power only, no rpm suffix
    TEST_ASSERT_EQUAL_STRING("", l[3].c_str());
}

// --- saved page ---------------------------------------------------------------

void test_saved_page_has_ssid_and_hints() {
    std::string p = renderSavedPage("Donnie Boon");
    TEST_ASSERT_TRUE(p.find("Donnie Boon") != std::string::npos);  // shows the chosen network
    TEST_ASSERT_TRUE(p.find("LED") != std::string::npos);          // LED hint present
    TEST_ASSERT_TRUE(p.find("OLED") != std::string::npos);         // OLED / IP hint present
    TEST_ASSERT_TRUE(p.find("restart") != std::string::npos);      // tells the user it reboots
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
    TEST_ASSERT_TRUE(p.find("fetch(") != std::string::npos);       // polls the device
    TEST_ASSERT_TRUE(p.find("<canvas") != std::string::npos);      // the live chart
    TEST_ASSERT_TRUE(p.find("power_w") != std::string::npos);      // reads the broadcast power field
    TEST_ASSERT_TRUE(p.find("src_power_w") != std::string::npos);  // reads the received power field
    TEST_ASSERT_TRUE(p.find("METER IN") != std::string::npos);     // shows the in->out flow
    TEST_ASSERT_TRUE(p.find("CRANK OUT") != std::string::npos);
}

// --- runner -------------------------------------------------------------------

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_correction_scale_offset);
    RUN_TEST(test_correction_clamps_at_zero);
    RUN_TEST(test_curve_empty_is_unity);
    RUN_TEST(test_curve_interpolates_and_holds_flat);
    RUN_TEST(test_curve_correction_takes_precedence);
    RUN_TEST(test_cps_measurement_roundtrip);
    RUN_TEST(test_cps_decode_short_frame_is_safe);
    RUN_TEST(test_calibration_response_bytes);
    RUN_TEST(test_cp_offset_comp_enhanced_0x10);
    RUN_TEST(test_cp_offset_comp_basic_0x0C);
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
    RUN_TEST(test_meter_match_pinned_address_wins);
    RUN_TEST(test_meter_match_pin_never_reads_own_spoof);
    RUN_TEST(test_cps_cadence_frame);
    RUN_TEST(test_crank_cadence_roundtrips_rpm);
    RUN_TEST(test_crank_cadence_coasting_no_events);
    RUN_TEST(test_stages_frame_golden_encode);
    RUN_TEST(test_stages_frame_flags_and_power);
    RUN_TEST(test_decode_crank_data_behind_preceding_fields);
    RUN_TEST(test_cadence_from_two_real_stages_frames);
    RUN_TEST(test_proxy_relays_power);
    RUN_TEST(test_proxy_applies_correction);
    RUN_TEST(test_proxy_preserves_cadence);
    RUN_TEST(test_status_json_mock);
    RUN_TEST(test_status_json_source_state);
    RUN_TEST(test_status_json_unknown_cadence);
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
    RUN_TEST(test_oled_connected_unknown_cadence_omitted);
    RUN_TEST(test_saved_page_has_ssid_and_hints);
    RUN_TEST(test_saved_page_escapes_ssid);
    RUN_TEST(test_app_page_essentials);
    return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() { delay(2000); runUnityTests(); }
void loop() {}
#else
int main() { return runUnityTests(); }
#endif
