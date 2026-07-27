// Host tests for the two SHARED view projections — the logic that used to be hand-copied into each
// surface's frame builder in main.cpp.
//
// These exist because the copies had already drifted. projectRideView() replaces an OLED block that
// read balance from lastSource() and an LCD block that read it from lastOutput(); projectCalWizard()
// replaces two verbatim copies of the wizard's three-state machine, one of which ran against a view
// that is reused across frames and never cleared, so a finished fit leaked into the next Idle screen.
//
// Every assertion below is a property one of those copies could have broken silently.
#include <unity.h>

#include "CalibrationPage.h"
#include "UiModel.h"

using namespace sb20proxy;

void setUp() {}
void tearDown() {}

// ---------- projectRideView -----------------------------------------------------------------

static RideInputs connectedRide() {
    RideInputs in;
    in.out.power_w = 250;
    in.out.cadence_rpm = 88;
    in.out.balance_half_pct = 102;  // 51% left
    in.src.power_w = 238;
    in.src.cadence_rpm = 88;
    in.src.balance_half_pct = 102;
    in.meterConnected = true;
    in.meterName = "ASSIOMA17039L";
    in.identity = "Stages 62144";
    in.wifiUp = true;
    in.rssi = -57;
    return in;
}

// The hero number is what we BROADCAST; the details row is what we RECEIVED. Swapping them would
// make a corrected board look like it was doing nothing.
static void test_watts_is_the_broadcast_reading_and_srcWatts_the_received_one() {
    RideView v;
    projectRideView(connectedRide(), v);
    TEST_ASSERT_EQUAL_INT16(250, v.watts);
    TEST_ASSERT_EQUAL_INT16(238, v.srcWatts);
}

// balance_half_pct is left% x 2, so the view halves it.
static void test_balance_is_halved_into_a_left_percentage() {
    RideInputs in = connectedRide();
    in.src.balance_half_pct = 102;
    RideView v;
    projectRideView(in, v);
    TEST_ASSERT_EQUAL_INT16(51, v.balancePct);
}

// -1 means "the meter reported no split". Halving it would give 0 — a confident "0% left", which is
// a real reading a single-sided meter could never produce. The sentinel has to survive.
static void test_the_unknown_balance_sentinel_survives_the_halving() {
    RideInputs in = connectedRide();
    in.src.balance_half_pct = -1;
    RideView v;
    projectRideView(in, v);
    TEST_ASSERT_EQUAL_INT16(-1, v.balancePct);
}

// The drift this function was extracted to kill: balance comes from the RECEIVED reading, because
// balance is a property of the meter's measurement and not of our correction.
static void test_balance_is_projected_from_the_received_reading_not_the_broadcast_one() {
    RideInputs in = connectedRide();
    in.src.balance_half_pct = 100;  // 50% left, as measured
    in.out.balance_half_pct = 160;  // 80% left, as if a correction had synthesised a split
    RideView v;
    projectRideView(in, v);
    TEST_ASSERT_EQUAL_INT16(50, v.balancePct);
}

static void test_a_connected_meter_shows_its_name() {
    RideView v;
    projectRideView(connectedRide(), v);
    TEST_ASSERT_TRUE(v.srcOn);
    TEST_ASSERT_EQUAL_STRING("ASSIOMA17039L", v.srcName.c_str());
    TEST_ASSERT_EQUAL_STRING("Stages 62144", v.outName.c_str());
}

// A disconnected meter must not keep showing the name it had — the panel would read as a live link.
static void test_a_disconnected_meter_reads_as_searching_not_as_its_last_name() {
    RideInputs in = connectedRide();
    in.meterConnected = false;
    RideView v;
    v.srcName = "ASSIOMA17039L";  // as if a previous frame had left it there
    v.srcOn = true;
    projectRideView(in, v);
    TEST_ASSERT_FALSE(v.srcOn);
    TEST_ASSERT_EQUAL_STRING("searching...", v.srcName.c_str());
}

// A stale RSSI reads as a plausible signal strength rather than as "no link".
static void test_rssi_is_zeroed_when_wifi_is_down() {
    RideInputs in = connectedRide();
    in.wifiUp = false;
    in.rssi = -57;
    RideView v;
    projectRideView(in, v);
    TEST_ASSERT_EQUAL_INT32(0, v.wifiRssi);
}

static void test_rssi_is_reported_when_wifi_is_up() {
    RideView v;
    projectRideView(connectedRide(), v);
    TEST_ASSERT_EQUAL_INT32(-57, v.wifiRssi);
}

// The output is always on: we advertise the spoofed crank whether or not a meter is feeding it.
static void test_the_output_link_is_always_reported_as_on() {
    RideInputs in = connectedRide();
    in.meterConnected = false;
    RideView v;
    projectRideView(in, v);
    TEST_ASSERT_TRUE(v.outOn);
}

// The caller still owns the device-specific fields; the projection must not stamp on them.
static void test_the_projection_leaves_the_callers_own_fields_alone() {
    static const int16_t hist[3] = {100, 200, 300};
    RideView v;
    v.hist = hist;
    v.nHist = 3;
    v.version = "1.2.3 abc1234";
    v.freeHeap = 90000;
    v.wkRunning = true;
    v.wkTarget = 210;
    projectRideView(connectedRide(), v);
    TEST_ASSERT_EQUAL_PTR(hist, v.hist);
    TEST_ASSERT_EQUAL_INT(3, v.nHist);
    TEST_ASSERT_EQUAL_STRING("1.2.3 abc1234", v.version.c_str());
    TEST_ASSERT_EQUAL_UINT32(90000, v.freeHeap);
    TEST_ASSERT_TRUE(v.wkRunning);
    TEST_ASSERT_EQUAL_INT16(210, v.wkTarget);
}

// ---------- projectCalWizard ----------------------------------------------------------------

static std::vector<SourceCandidate> twoCandidates() {
    std::vector<SourceCandidate> d;
    SourceCandidate a;
    a.name = "XCADEY";
    a.address = "aa:bb:cc:dd:ee:01";
    SourceCandidate b;
    b.name = "ASSIOMA17039L";
    b.address = "aa:bb:cc:dd:ee:02";
    d.push_back(a);
    d.push_back(b);
    return d;
}

static void collect(CalibrationSession& s, int pairs) {
    s.start();
    for (int i = 0; i < pairs; ++i) {
        const uint32_t t = (uint32_t)(i * 1000);
        const float dut = 100.0f + (float)(i % 5) * 40.0f;
        s.onDut(dut, t);
        s.onRef(dut * 1.1f, t);
    }
}

// Not calibrating -> the picker screen, with the scanned meters to choose from.
static void test_idle_offers_the_scanned_meters() {
    CalibrationSession s(4);
    const std::vector<SourceCandidate> devices = twoCandidates();
    CalWizardInputs in;
    in.calibrating = false;
    in.session = &s;
    in.devices = &devices;
    CalWizardView v;
    projectCalWizard(in, v);
    TEST_ASSERT_TRUE(v.state == CalState::Idle);
    TEST_ASSERT_EQUAL_size_t(2, v.devices.size());
    TEST_ASSERT_EQUAL_STRING("XCADEY", v.devices[0].name.c_str());
}

// A null session is a bench/mock build with no calibration at all — the idle prompt, not a crash.
static void test_a_missing_session_is_treated_as_idle() {
    CalWizardInputs in;
    in.calibrating = true;  // even with the flag set
    in.session = nullptr;
    CalWizardView v;
    v.state = CalState::Fitted;
    projectCalWizard(in, v);
    TEST_ASSERT_TRUE(v.state == CalState::Idle);
}

static void test_collecting_reports_progress_and_both_links() {
    CalibrationSession s(4);
    collect(s, 6);
    CalWizardInputs in;
    in.calibrating = true;
    in.session = &s;
    in.dutConnected = true;
    in.refConnected = false;
    CalWizardView v;
    projectCalWizard(in, v);
    TEST_ASSERT_TRUE(v.state == CalState::Collecting);
    TEST_ASSERT_TRUE(v.dutConnected);
    TEST_ASSERT_FALSE(v.refConnected);
    TEST_ASSERT_EQUAL_INT(4, v.minPairs);
    TEST_ASSERT_TRUE(v.pairCount > 0);
    TEST_ASSERT_TRUE(v.enoughToFit);
    TEST_ASSERT_TRUE(v.coverage.size() > 0);
}

// Too few pairs: the wizard must not offer Finish, or the rider fits noise.
static void test_collecting_withholds_finish_until_there_are_enough_pairs() {
    CalibrationSession s(30);
    collect(s, 3);
    CalWizardInputs in;
    in.calibrating = true;
    in.session = &s;
    CalWizardView v;
    projectCalWizard(in, v);
    TEST_ASSERT_TRUE(v.state == CalState::Collecting);
    TEST_ASSERT_FALSE(v.enoughToFit);
    TEST_ASSERT_EQUAL_INT(30, v.minPairs);
}

// A fit either produced a curve or fell back to linear — the view says which, so the page never
// has to guess from an empty curve.
static void test_a_fitted_session_reports_a_curve_or_the_linear_fallback_but_not_both() {
    CalibrationSession s(4);
    collect(s, 40);
    TEST_ASSERT_TRUE(s.finish());
    CalWizardInputs in;
    in.calibrating = true;
    in.session = &s;
    CalWizardView v;
    projectCalWizard(in, v);
    TEST_ASSERT_TRUE(v.state == CalState::Fitted);
    if (v.linear) {
        TEST_ASSERT_TRUE(v.curve.empty());
    } else {
        TEST_ASSERT_FALSE(v.curve.empty());
    }
}

// THE staleness bug this extraction fixes. The LCD reuses one LcdViews across frames and never
// clears it, so a finished fit's curve/residual/pair counts survived into the next Idle screen —
// while GET /calibrate, which builds a fresh view per request, showed them empty. Same session,
// two answers.
static void test_leaving_a_fitted_state_clears_the_fit_from_a_reused_view() {
    CalibrationSession s(4);
    collect(s, 40);
    TEST_ASSERT_TRUE(s.finish());

    CalWizardView v;  // ONE view, reused — exactly how the LCD task holds it
    CalWizardInputs in;
    in.calibrating = true;
    in.session = &s;
    projectCalWizard(in, v);
    TEST_ASSERT_TRUE(v.state == CalState::Fitted);
    const bool hadSomething = !v.curve.empty() || v.linear;
    TEST_ASSERT_TRUE(hadSomething);

    s.cancel();
    in.calibrating = false;
    projectCalWizard(in, v);
    TEST_ASSERT_TRUE(v.state == CalState::Idle);
    TEST_ASSERT_TRUE(v.curve.empty());
    TEST_ASSERT_FALSE(v.linear);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, v.scale);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, v.offset);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, v.residualW);
    TEST_ASSERT_EQUAL_INT(0, v.pairCount);
    TEST_ASSERT_EQUAL_size_t(0, v.coverage.size());
}

// The mirror of the above: a stale Collecting link state must not survive into Fitted, or the
// finished screen claims a meter is still connected.
static void test_reaching_a_fitted_state_clears_the_collecting_link_state() {
    CalibrationSession s(4);
    collect(s, 40);
    CalWizardView v;
    CalWizardInputs in;
    in.calibrating = true;
    in.session = &s;
    in.dutConnected = true;
    in.refConnected = true;
    projectCalWizard(in, v);
    TEST_ASSERT_TRUE(v.dutConnected);

    TEST_ASSERT_TRUE(s.finish());
    projectCalWizard(in, v);
    TEST_ASSERT_TRUE(v.state == CalState::Fitted);
    TEST_ASSERT_FALSE(v.dutConnected);
    TEST_ASSERT_FALSE(v.refConnected);
}

// Idle must not carry the previous run's progress either.
static void test_idle_clears_a_previous_runs_progress() {
    CalibrationSession s(4);
    collect(s, 10);
    CalWizardView v;
    CalWizardInputs in;
    in.calibrating = true;
    in.session = &s;
    projectCalWizard(in, v);
    TEST_ASSERT_TRUE(v.pairCount > 0);

    in.calibrating = false;
    projectCalWizard(in, v);
    TEST_ASSERT_TRUE(v.state == CalState::Idle);
    TEST_ASSERT_EQUAL_INT(0, v.pairCount);
    TEST_ASSERT_FALSE(v.enoughToFit);
    TEST_ASSERT_EQUAL_size_t(0, v.coverage.size());
}

// The surface owns the fields the session cannot know — the pending picks, the editable device
// name and the banner. Clearing those would wipe a rider's half-finished form on the next frame.
static void test_the_projection_leaves_the_surfaces_own_fields_alone() {
    CalibrationSession s(4);
    CalWizardView v;
    v.dutAddr = "aa:bb:cc:dd:ee:01";
    v.refAddr = "aa:bb:cc:dd:ee:02";
    v.deviceName = "My Corrector";
    v.message = "Pick a reference meter";
    v.scanning = true;
    CalWizardInputs in;
    in.calibrating = false;
    in.session = &s;
    projectCalWizard(in, v);
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:01", v.dutAddr.c_str());
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:02", v.refAddr.c_str());
    TEST_ASSERT_EQUAL_STRING("My Corrector", v.deviceName.c_str());
    TEST_ASSERT_EQUAL_STRING("Pick a reference meter", v.message.c_str());
    TEST_ASSERT_TRUE(v.scanning);
    TEST_ASSERT_TRUE(v.edges.size() > 0);
}

// The whole point: both surfaces get the same answer from the same session.
static void test_both_surfaces_project_the_same_session_identically() {
    CalibrationSession s(4);
    collect(s, 12);
    CalWizardInputs in;
    in.calibrating = true;
    in.session = &s;
    in.dutConnected = true;
    in.refConnected = true;

    CalWizardView lcd;   // reused across frames, as the LCD task holds it
    projectCalWizard(in, lcd);
    projectCalWizard(in, lcd);
    CalWizardView web;   // fresh per request, as the route builds it
    projectCalWizard(in, web);

    TEST_ASSERT_TRUE(lcd.state == web.state);
    TEST_ASSERT_EQUAL_INT(web.pairCount, lcd.pairCount);
    TEST_ASSERT_EQUAL_INT(web.minPairs, lcd.minPairs);
    TEST_ASSERT_EQUAL_size_t(web.coverage.size(), lcd.coverage.size());
    TEST_ASSERT_EQUAL_INT(web.dutConnected ? 1 : 0, lcd.dutConnected ? 1 : 0);
    TEST_ASSERT_EQUAL_INT(web.refConnected ? 1 : 0, lcd.refConnected ? 1 : 0);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_watts_is_the_broadcast_reading_and_srcWatts_the_received_one);
    RUN_TEST(test_balance_is_halved_into_a_left_percentage);
    RUN_TEST(test_the_unknown_balance_sentinel_survives_the_halving);
    RUN_TEST(test_balance_is_projected_from_the_received_reading_not_the_broadcast_one);
    RUN_TEST(test_a_connected_meter_shows_its_name);
    RUN_TEST(test_a_disconnected_meter_reads_as_searching_not_as_its_last_name);
    RUN_TEST(test_rssi_is_zeroed_when_wifi_is_down);
    RUN_TEST(test_rssi_is_reported_when_wifi_is_up);
    RUN_TEST(test_the_output_link_is_always_reported_as_on);
    RUN_TEST(test_the_projection_leaves_the_callers_own_fields_alone);

    RUN_TEST(test_idle_offers_the_scanned_meters);
    RUN_TEST(test_a_missing_session_is_treated_as_idle);
    RUN_TEST(test_collecting_reports_progress_and_both_links);
    RUN_TEST(test_collecting_withholds_finish_until_there_are_enough_pairs);
    RUN_TEST(test_a_fitted_session_reports_a_curve_or_the_linear_fallback_but_not_both);
    RUN_TEST(test_leaving_a_fitted_state_clears_the_fit_from_a_reused_view);
    RUN_TEST(test_reaching_a_fitted_state_clears_the_collecting_link_state);
    RUN_TEST(test_idle_clears_a_previous_runs_progress);
    RUN_TEST(test_the_projection_leaves_the_surfaces_own_fields_alone);
    RUN_TEST(test_both_surfaces_project_the_same_session_identically);
    return UNITY_END();
}
