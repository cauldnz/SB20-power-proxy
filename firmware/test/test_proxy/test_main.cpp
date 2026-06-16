// Host-side unit tests for the platform-agnostic proxy core (no hardware).
//
//   pio test -e native
//
// The firmware analogue of the Python suite: the CPS codec, the correction, and the
// ProxyCore relay are proven on the host BEFORE any of it touches a real meter or the SB20.

#include <unity.h>

#include "Correction.h"
#include "Cps.h"
#include "LogBuffer.h"
#include "MockCrank.h"
#include "MockMeter.h"
#include "Provisioning.h"
#include "ProxyCore.h"
#include "Status.h"

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
    s.lastPowerW = 200;
    s.lastCadenceRpm = 90;
    s.uptimeMs = 12345;
    std::string j = renderStatusJson(s);
    TEST_ASSERT_TRUE(j.find("\"source\":\"mock\"") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"forwarded\":5") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"power_w\":200") != std::string::npos);
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
    RUN_TEST(test_cps_cadence_frame);
    RUN_TEST(test_crank_cadence_roundtrips_rpm);
    RUN_TEST(test_crank_cadence_coasting_no_events);
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
    RUN_TEST(test_logbuffer_keeps_recent_in_order);
    RUN_TEST(test_logbuffer_drops_oldest_past_capacity);
    RUN_TEST(test_logbuffer_caps_line_length);
    RUN_TEST(test_log_toggle_footer_states);
    RUN_TEST(test_portal_page_shows_log_toggle_when_requested);
    return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() { delay(2000); runUnityTests(); }
void loop() {}
#else
int main() { return runUnityTests(); }
#endif
