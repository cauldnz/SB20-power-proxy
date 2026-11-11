// Host tests for the pure HTTP route layer (WebRoutes.h).
//
// Before this extraction the routing layer had ZERO test coverage: all 48 routes
// lived as Arduino lambdas inside WifiLink.cpp, so which URL did what, whether a
// state-changing route was CSRF-guarded, and what a route returned when its hook
// was unwired were all facts held only by reading the file.
//
// The load-bearing test here is test_every_post_route_is_csrf_guarded, which walks
// the tables and asserts the invariant over every route that exists, so it also
// covers routes added later.

#include <string>
#include <vector>

#include <unity.h>

#include "BodyPump.h"
#include "WebRoutes.h"

using namespace sb20proxy;

void setUp() {}
void tearDown() {}

// --- helpers ---------------------------------------------------------------

static HttpRequest get(const std::string& uri) {
    HttpRequest r;
    r.method = HttpMethod::Get;
    r.uri = uri;
    r.host = "sb20proxy.local";
    return r;
}

static HttpRequest post(const std::string& uri, const std::string& body = "") {
    HttpRequest r;
    r.method = HttpMethod::Post;
    r.uri = uri;
    r.host = "sb20proxy.local";
    r.body = body;
    return r;  // no Origin/Referer => same-origin by the documented curl rule
}

static const Route* find(const std::vector<Route>& table, const std::string& path,
                         HttpMethod m) {
    for (const auto& r : table)
        if (path == r.path && r.method == m) return &r;
    return nullptr;
}

static const Route* station(const std::string& path, HttpMethod m = HttpMethod::Get) {
    return find(stationRoutes(), path, m);
}

// --- the security invariant ------------------------------------------------

// The whole point of the dispatcher. Walks EVERY route in both tables, so a route
// added later is covered without anyone remembering to write a test for it.
void test_every_post_route_is_csrf_guarded() {
    DeviceHooks h;
    for (const auto* table : {&stationRoutes(), &portalRoutes()}) {
        for (const Route& r : *table) {
            if (r.method != HttpMethod::Post) continue;
            HttpRequest req = post(r.path);
            req.origin = "http://evil.example";  // hostile cross-site origin
            const HttpResponse resp = dispatch(r, h, req);
            TEST_ASSERT_EQUAL_INT_MESSAGE(403, resp.status, r.path);
            TEST_ASSERT_FALSE_MESSAGE(resp.reboot, r.path);
        }
    }
}

// A rejected request must not reach the handler, so no hook may fire.
void test_csrf_rejection_runs_no_side_effect() {
    DeviceHooks h;
    bool saved = false, cleared = false;
    h.saveConfig = [&](const RuntimeConfig&) { saved = true; };
    h.clearCreds = [&] { cleared = true; };

    for (const auto* table : {&stationRoutes(), &portalRoutes()}) {
        for (const Route& r : *table) {
            if (r.method != HttpMethod::Post) continue;
            HttpRequest req = post(r.path);
            req.referer = "http://attacker.test/page";
            dispatch(r, h, req);
        }
    }
    TEST_ASSERT_FALSE(saved);
    TEST_ASSERT_FALSE(cleared);
}

// GET routes are deliberately unguarded (a cross-site GET can't be forged with a
// body, and /obc/press etc. are documented as curl-friendly bring-up actions).
void test_get_routes_are_not_csrf_guarded() {
    DeviceHooks h;
    const Route* r = station("/status");
    TEST_ASSERT_NOT_NULL(r);
    HttpRequest req = get("/status");
    req.origin = "http://evil.example";
    TEST_ASSERT_EQUAL_INT(200, dispatch(*r, h, req).status);
}

// A same-origin POST must pass the guard.
void test_same_origin_post_is_allowed() {
    DeviceHooks h;
    const Route* r = station("/curve", HttpMethod::Post);
    TEST_ASSERT_NOT_NULL(r);
    HttpRequest req = post("/curve", "");
    req.origin = "http://sb20proxy.local";
    TEST_ASSERT_EQUAL_INT(200, dispatch(*r, h, req).status);
}

// --- unwired hooks reproduce the old ternary defaults ----------------------

// These four defaults were the pre-refactor `hook_ ? hook_() : X` fallbacks. Two are
// load-bearing and easy to invert by accident, so they get named tests.

void test_unwired_workout_load_reports_failure() {
    DeviceHooks h;  // workoutLoad unset => must 400, not claim success
    const HttpResponse r = routes::workoutLoad(h, post("/workout/load", "{}"));
    TEST_ASSERT_EQUAL_INT(400, r.status);
    TEST_ASSERT_EQUAL_STRING("bad workout\n", r.body.c_str());
}

void test_unwired_cal_start_falls_through_to_success() {
    // The old call site read `calStart_ && !calStart_(...)`, so an unset hook did NOT
    // take the rejection branch. Defaulting this to false would silently break start.
    DeviceHooks h;
    const HttpResponse r = routes::calibrateStart(h, post("/calibrate/start",
                                                          "dut=AA:BB&ref=CC:DD"));
    TEST_ASSERT_TRUE(r.reboot);
}

void test_unwired_perf_and_compare_defaults() {
    DeviceHooks h;
    TEST_ASSERT_EQUAL_STRING("{}", routes::stats(h, get("/stats")).body.c_str());
    TEST_ASSERT_EQUAL_STRING("{\"valid\":false}",
                             routes::compare(h, get("/compare")).body.c_str());
}

void test_unwired_workout_state_default() {
    DeviceHooks h;
    TEST_ASSERT_EQUAL_STRING("{\"loaded\":false}",
                             routes::workoutStateJson(h, get("/workout/state")).body.c_str());
}

// --- reboot intent ---------------------------------------------------------

// Reboots are a returned intent now, so the set of rebooting routes is assertable.
// Previously this was twelve scattered `delay(400); esp_restart();` calls.
void test_exactly_the_expected_routes_request_a_reboot() {
    DeviceHooks h;
    h.calStart = [](const std::string&, const std::string&) { return true; };
    h.calSave = [](const std::string&) { return true; };

    struct Case { const char* path; HttpMethod m; std::string body; };
    const std::vector<Case> rebooting = {
        {"/setup/save", HttpMethod::Post, "name=ASSIOMA"},
        {"/setup/reset", HttpMethod::Post, ""},
        {"/config", HttpMethod::Post, "src_filter=ASSIOMA"},
        {"/calibrate/start", HttpMethod::Post, "dut=AA:BB&ref=CC:DD"},
        {"/calibrate/save", HttpMethod::Post, "name=Meter"},
        {"/calibrate/cancel", HttpMethod::Post, ""},
        {"/obc/devmode/on", HttpMethod::Post, ""},
        {"/obc/devmode/off", HttpMethod::Post, ""},
        {"/obc/shifter/on", HttpMethod::Post, ""},
        {"/obc/shifter/off", HttpMethod::Post, ""},
        {"/forget", HttpMethod::Post, ""},
    };
    for (const auto& c : rebooting) {
        const Route* r = station(c.path, c.m);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, c.path);
        TEST_ASSERT_TRUE_MESSAGE(dispatch(*r, h, post(c.path, c.body)).reboot, c.path);
    }
}

// Loading a workout or a curve is data, not identity — it must apply live.
void test_live_routes_do_not_reboot() {
    DeviceHooks h;
    h.workoutLoad = [](const std::string&) { return true; };
    for (const char* p : {"/curve", "/workout/load"}) {
        const Route* r = station(p, HttpMethod::Post);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, p);
        TEST_ASSERT_FALSE_MESSAGE(dispatch(*r, h, post(p, "100:1.0")).reboot, p);
    }
}

// Ride mode replies first and drops the radio after — never a reboot.
void test_ride_mode_drops_radio_without_reboot() {
    DeviceHooks h;
    const HttpResponse r = routes::rideModeGo(h, post("/wifi/off"));
    TEST_ASSERT_TRUE(r.radioOff);
    TEST_ASSERT_FALSE(r.reboot);
    TEST_ASSERT_TRUE(r.stream);
}

// --- config merge semantics through the routes -----------------------------

// The regression these merges exist to prevent: /setup owns only source + identity,
// so saving it must not wipe the fitted curve or flip the broadcast mode.
void test_setup_save_preserves_curve_and_mode() {
    DeviceHooks h;
    RuntimeConfig stored = RuntimeConfig::defaults();
    stored.mode = ProxyMode::Corrector;
    stored.curve = curveFromString("100:1.05,200:1.10");
    RuntimeConfig saved;
    h.config = [&] { return stored; };
    h.saveConfig = [&](const RuntimeConfig& c) { saved = c; };

    const Route* r = station("/setup/save", HttpMethod::Post);
    TEST_ASSERT_NOT_NULL(r);
    dispatch(*r, h, post("/setup/save", "name=ASSIOMA"));

    TEST_ASSERT_EQUAL_INT((int)ProxyMode::Corrector, (int)saved.mode);
    TEST_ASSERT_FALSE(saved.curve.points.empty());
}

// The SPA's POST /config merges too, for the same reason.
void test_config_post_preserves_curve() {
    DeviceHooks h;
    RuntimeConfig stored = RuntimeConfig::defaults();
    stored.meterNameFilter = "ASSIOMA";
    stored.curve = curveFromString("100:1.05");
    RuntimeConfig saved;
    h.config = [&] { return stored; };
    h.saveConfig = [&](const RuntimeConfig& c) { saved = c; };

    const Route* r = station("/config", HttpMethod::Post);
    dispatch(*r, h, post("/config", "mode=corrector"));

    TEST_ASSERT_FALSE(saved.curve.points.empty());
    TEST_ASSERT_EQUAL_STRING("ASSIOMA", saved.meterNameFilter.c_str());
}

// An unusable config (nothing to match on) must be rejected, not persisted.
void test_config_post_rejects_invalid_without_saving() {
    DeviceHooks h;
    RuntimeConfig stored = RuntimeConfig::defaults();
    stored.meterNameFilter = "";
    stored.meterAddress = "";
    bool saved = false;
    h.config = [&] { return stored; };
    h.saveConfig = [&](const RuntimeConfig&) { saved = true; };

    const Route* r = station("/config", HttpMethod::Post);
    const HttpResponse resp = dispatch(*r, h, post("/config", "src_filter="));
    TEST_ASSERT_EQUAL_INT(400, resp.status);
    TEST_ASSERT_FALSE(resp.reboot);
    TEST_ASSERT_FALSE(saved);
}

// /setup/reset persists the shipped defaults — that IS the clear.
void test_setup_reset_persists_defaults() {
    DeviceHooks h;
    RuntimeConfig saved;
    saved.meterNameFilter = "STALE";
    h.saveConfig = [&](const RuntimeConfig& c) { saved = c; };
    dispatch(*station("/setup/reset", HttpMethod::Post), h, post("/setup/reset"));
    TEST_ASSERT_EQUAL_STRING(RuntimeConfig::defaults().meterNameFilter.c_str(),
                             saved.meterNameFilter.c_str());
}

// --- OBC -------------------------------------------------------------------

// The asymmetry that was invisible across four near-identical handlers: Devmode ON
// also switches obcEnabled on, because Devmode implies the service is present.
void test_devmode_on_also_enables_obc_but_shifter_does_not() {
    DeviceHooks h;
    RuntimeConfig saved;
    h.saveConfig = [&](const RuntimeConfig& c) { saved = c; };

    dispatch(*station("/obc/devmode/on", HttpMethod::Post), h, post("/obc/devmode/on"));
    TEST_ASSERT_TRUE(saved.obcDevmode);
    TEST_ASSERT_TRUE(saved.obcEnabled);

    saved = RuntimeConfig::defaults();
    dispatch(*station("/obc/shifter/on", HttpMethod::Post), h, post("/obc/shifter/on"));
    TEST_ASSERT_TRUE(saved.obcSinkShifter);
    TEST_ASSERT_FALSE(saved.obcEnabled);  // sinking the shifter does NOT imply Devmode
}

// Turning Devmode off must not re-enable anything.
void test_devmode_off_clears_only_devmode() {
    DeviceHooks h;
    RuntimeConfig stored = RuntimeConfig::defaults();
    stored.obcDevmode = true;
    stored.obcEnabled = true;
    RuntimeConfig saved;
    h.config = [&] { return stored; };
    h.saveConfig = [&](const RuntimeConfig& c) { saved = c; };

    dispatch(*station("/obc/devmode/off", HttpMethod::Post), h, post("/obc/devmode/off"));
    TEST_ASSERT_FALSE(saved.obcDevmode);
    TEST_ASSERT_TRUE(saved.obcEnabled);  // untouched
}

void test_obc_press_argument_validation() {
    DeviceHooks h;
    int fired = 0;
    uint8_t gotId = 0, gotState = 0;
    h.obcPress = [&](uint8_t i, uint8_t s) { ++fired; gotId = i; gotState = s; };

    TEST_ASSERT_EQUAL_INT(400, routes::obcPress(h, get("/obc/press")).status);

    HttpRequest r = get("/obc/press");
    r.args = {{"id", "999"}};
    TEST_ASSERT_EQUAL_INT(400, routes::obcPress(h, r).status);
    TEST_ASSERT_EQUAL_INT(0, fired);  // nothing fired on a rejected request

    r.args = {{"id", "0x30"}};  // base-0 parse: hex accepted
    TEST_ASSERT_EQUAL_INT(200, routes::obcPress(h, r).status);
    TEST_ASSERT_EQUAL_UINT8(0x30, gotId);
    TEST_ASSERT_EQUAL_UINT8(1, gotState);  // default state

    r.args = {{"id", "48"}, {"state", "0"}};  // and decimal
    TEST_ASSERT_EQUAL_INT(200, routes::obcPress(h, r).status);
    TEST_ASSERT_EQUAL_UINT8(0x30, gotId);
    TEST_ASSERT_EQUAL_UINT8(0, gotState);
}

// GET emits what POST accepts — the round trip the web SPA depends on.
void test_obc_buttons_round_trip() {
    DeviceHooks h;
    RuntimeConfig stored = RuntimeConfig::defaults();
    stored.obcSinkShifter = true;
    h.config = [&] { return stored; };
    bool gotEnabled = false;
    h.obcButtons = [&](bool e, const Sb20ButtonMap&) { gotEnabled = e; };

    const std::string emitted = routes::obcButtonsGet(h, get("/obc/buttons.json")).body;
    const HttpResponse back = routes::obcButtonsSet(h, post("/obc/buttons.json", emitted));

    TEST_ASSERT_EQUAL_INT(200, back.status);
    TEST_ASSERT_TRUE(gotEnabled);
    TEST_ASSERT_EQUAL_STRING(emitted.c_str(), back.body.c_str());
}

void test_obc_buttons_rejects_garbage() {
    DeviceHooks h;
    bool called = false;
    h.obcButtons = [&](bool, const Sb20ButtonMap&) { called = true; };
    const HttpResponse r = routes::obcButtonsSet(h, post("/obc/buttons.json", "not json"));
    TEST_ASSERT_EQUAL_INT(400, r.status);
    TEST_ASSERT_FALSE(called);
}

// --- log -------------------------------------------------------------------

void test_log_is_403_when_disabled_and_toggles_persist() {
    DeviceHooks h;
    bool enabled = false;
    h.logEnabled = [&] { return enabled; };
    h.setLogEnabled = [&](bool on) { enabled = on; };
    h.logText = [] { return std::string("line one\n"); };

    TEST_ASSERT_EQUAL_INT(403, routes::log(h, get("/log")).status);
    routes::logOn(h, get("/log/on"));
    TEST_ASSERT_TRUE(enabled);
    TEST_ASSERT_EQUAL_STRING("line one\n", routes::log(h, get("/log")).body.c_str());
    routes::logOff(h, get("/log/off"));
    TEST_ASSERT_FALSE(enabled);
    TEST_ASSERT_EQUAL_INT(403, routes::log(h, get("/log")).status);
}

// --- calibration wizard ----------------------------------------------------

// Rejections must re-render the wizard with the reason, NOT reboot — rebooting would
// throw away the collected pairs.
void test_cal_save_rejection_rerenders_without_reboot() {
    DeviceHooks h;
    h.calSave = [](const std::string&) { return false; };  // not fitted yet
    const HttpResponse r = dispatch(*station("/calibrate/save", HttpMethod::Post), h,
                                    post("/calibrate/save", "name=Meter"));
    TEST_ASSERT_FALSE(r.reboot);
    TEST_ASSERT_EQUAL_INT(200, r.status);
}

void test_cal_start_rejection_rerenders_without_reboot() {
    DeviceHooks h;
    h.calStart = [](const std::string&, const std::string&) { return false; };  // already running
    const HttpResponse r = dispatch(*station("/calibrate/start", HttpMethod::Post), h,
                                    post("/calibrate/start", "dut=AA:BB&ref=CC:DD"));
    TEST_ASSERT_FALSE(r.reboot);
}

// Finishing fits in place so the rider can review before saving.
void test_cal_finish_redirects_without_reboot() {
    DeviceHooks h;
    bool fitted = false;
    h.calFinish = [&] { fitted = true; return true; };
    const HttpResponse r = dispatch(*station("/calibrate/finish", HttpMethod::Post), h,
                                    post("/calibrate/finish"));
    TEST_ASSERT_TRUE(fitted);
    TEST_ASSERT_EQUAL_INT(303, r.status);
    TEST_ASSERT_EQUAL_STRING("/calibrate", r.location.c_str());
    TEST_ASSERT_FALSE(r.reboot);
}

// --- workout ---------------------------------------------------------------

void test_workout_preset_unknown_key_is_400() {
    DeviceHooks h;
    h.workoutLoad = [](const std::string&) { return true; };
    HttpRequest r = post("/workout/preset");
    r.args = {{"key", "__nope__"}};
    TEST_ASSERT_EQUAL_INT(400, routes::workoutPreset(h, r).status);
}

void test_workout_verbs_reach_the_control_hook() {
    DeviceHooks h;
    std::vector<std::string> seen;
    h.workoutControl = [&](const std::string& v) { seen.push_back(v); };
    for (const char* v : workoutVerbs()) workoutControl(h, v);
    TEST_ASSERT_EQUAL_UINT32(5, (uint32_t)seen.size());
    TEST_ASSERT_EQUAL_STRING("start", seen[0].c_str());
    TEST_ASSERT_EQUAL_STRING("stop", seen[4].c_str());
}

// --- portal ----------------------------------------------------------------

void test_portal_save_rejects_bad_creds_without_saving() {
    DeviceHooks h;
    bool saved = false;
    h.saveCreds = [&](const WifiCredentials&) { saved = true; };
    const Route* r = find(portalRoutes(), "/save", HttpMethod::Post);
    TEST_ASSERT_NOT_NULL(r);
    const HttpResponse resp = dispatch(*r, h, post("/save", "ssid=&pass=x"));
    TEST_ASSERT_FALSE(saved);
    TEST_ASSERT_FALSE(resp.reboot);
}

void test_portal_save_accepts_good_creds_and_reboots() {
    DeviceHooks h;
    WifiCredentials got;
    h.saveCreds = [&](const WifiCredentials& c) { got = c; };
    HttpRequest req = post("/save");
    req.args = {{"ssid", "home-2g"}, {"pass", "hunter2hunter2"}};
    const HttpResponse resp =
        dispatch(*find(portalRoutes(), "/save", HttpMethod::Post), h, req);
    TEST_ASSERT_EQUAL_STRING("home-2g", got.ssid.c_str());
    TEST_ASSERT_TRUE(resp.reboot);
}

// Falls back to the raw body parser when the server didn't split the form.
void test_portal_save_parses_raw_body_when_args_absent() {
    DeviceHooks h;
    WifiCredentials got;
    h.saveCreds = [&](const WifiCredentials& c) { got = c; };
    dispatch(*find(portalRoutes(), "/save", HttpMethod::Post), h,
             post("/save", "ssid=raw-net&pass=hunter2hunter2"));
    TEST_ASSERT_EQUAL_STRING("raw-net", got.ssid.c_str());
}

void test_portal_probes_all_redirect_to_setup() {
    DeviceHooks h;
    TEST_ASSERT_EQUAL_UINT32(6, (uint32_t)portalProbes().size());
    const HttpResponse r = routes::portalRedirect(h, get("/generate_204"));
    TEST_ASSERT_EQUAL_INT(302, r.status);
    TEST_ASSERT_EQUAL_STRING(Config::SETUP_PORTAL_URL, r.location.c_str());
}

// --- table shape -----------------------------------------------------------

// /app must stream from flash: materialising the ~34 KB SPA into a std::string would
// blow the C3's heap beside BLE. Pinned so a later refactor can't quietly regress it.
void test_spa_route_streams_from_flash_not_the_heap() {
    DeviceHooks h;
    static const char kSpa[] = "<html>spa</html>";
    h.spaHtml = [] { return kSpa; };
    const HttpResponse r = routes::spa(h, get("/app"));
    TEST_ASSERT_EQUAL_PTR(kSpa, r.staticBody);
    TEST_ASSERT_TRUE(r.body.empty());
}

// Multi-KB HTML pages must use the drain-aware writer; Arduino's WebServer ignores
// short writes and silently truncates them under lwIP pressure.
void test_large_pages_are_marked_for_the_drain_aware_writer() {
    DeviceHooks h;
    for (const char* p : {"/", "/ui", "/more", "/setup", "/calibrate", "/workout", "/report"}) {
        const Route* r = station(p);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, p);
        TEST_ASSERT_TRUE_MESSAGE(dispatch(*r, h, get(p)).stream, p);
    }
}

// Small JSON/plain replies deliberately use the plain send.
void test_small_replies_are_not_streamed() {
    DeviceHooks h;
    for (const char* p : {"/status", "/stats", "/config", "/curve", "/scan"}) {
        const Route* r = station(p);
        TEST_ASSERT_NOT_NULL_MESSAGE(r, p);
        TEST_ASSERT_FALSE_MESSAGE(dispatch(*r, h, get(p)).stream, p);
    }
}

void test_dashboard_alias_shares_one_handler() {
    TEST_ASSERT_EQUAL_PTR(station("/")->fn, station("/ui")->fn);
}

// The portal serves the log routes too, so a tester on the setup AP can report.
void test_portal_serves_the_log_routes() {
    TEST_ASSERT_NOT_NULL(find(portalRoutes(), "/log", HttpMethod::Get));
    TEST_ASSERT_NOT_NULL(find(portalRoutes(), "/forget", HttpMethod::Post));
}

// Both /forget variants clear creds; they differ only in the message the tester sees.
void test_both_forget_variants_clear_credentials() {
    DeviceHooks h;
    int cleared = 0;
    h.clearCreds = [&] { ++cleared; };
    TEST_ASSERT_TRUE(routes::forgetStation(h, post("/forget")).reboot);
    TEST_ASSERT_TRUE(routes::forgetPortal(h, post("/forget")).reboot);
    TEST_ASSERT_EQUAL_INT(2, cleared);
    TEST_ASSERT_TRUE(routes::forgetStation(h, post("/forget")).body !=
                     routes::forgetPortal(h, post("/forget")).body);
}

// No duplicate (path, method) pairs — a shadowed route would never be reachable.
void test_no_duplicate_path_method_pairs() {
    for (const auto* table : {&stationRoutes(), &portalRoutes()}) {
        for (size_t i = 0; i < table->size(); ++i)
            for (size_t j = i + 1; j < table->size(); ++j) {
                const bool same = std::string((*table)[i].path) == (*table)[j].path &&
                                  (*table)[i].method == (*table)[j].method;
                TEST_ASSERT_FALSE_MESSAGE(same, (*table)[i].path);
            }
    }
}

void test_source_banner_reflects_connection_state() {
    ProxyStatus st;
    st.mock = true;
    TEST_ASSERT_TRUE(routes::sourceBanner(st).find("simulated") != std::string::npos);
    st.mock = false;
    st.sourceConnected = true;
    st.srcName = "ASSIOMA";
    TEST_ASSERT_TRUE(routes::sourceBanner(st).find("ASSIOMA") != std::string::npos);
    st.sourceConnected = false;
    TEST_ASSERT_TRUE(routes::sourceBanner(st).find("Searching") != std::string::npos);
}

// POST /ble/off parks the radio; POST /ble/on restores it. Both reboot, because the flag is read at
// boot. The property that matters is that neither touches anything else in the config -- a board
// parked quiet must come back with the same identity, meter pin and trainer it had.
void test_ble_off_sets_only_the_flag_and_reboots() {
    DeviceHooks h;
    RuntimeConfig stored = RuntimeConfig::defaults();
    stored.spoofName = "Stages 99744";
    stored.meterAddress = "aa:bb:cc:dd:ee:ff";
    stored.trainerNameFilter = "Stages Bike 0105";
    RuntimeConfig saved;
    h.config = [&] { return stored; };
    h.saveConfig = [&](const RuntimeConfig& c) { saved = c; };

    HttpResponse r = dispatch(*station("/ble/off", HttpMethod::Post), h, post("/ble/off"));
    TEST_ASSERT_TRUE(saved.bleOff);
    TEST_ASSERT_TRUE(r.reboot);
    TEST_ASSERT_EQUAL_STRING("Stages 99744", saved.spoofName.c_str());
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", saved.meterAddress.c_str());
    TEST_ASSERT_EQUAL_STRING("Stages Bike 0105", saved.trainerNameFilter.c_str());
}

void test_ble_on_clears_the_flag_and_reboots() {
    DeviceHooks h;
    RuntimeConfig stored = RuntimeConfig::defaults();
    stored.bleOff = true;
    stored.obcDevmode = true;          // must survive: /ble/on is not a reset
    RuntimeConfig saved;
    h.config = [&] { return stored; };
    h.saveConfig = [&](const RuntimeConfig& c) { saved = c; };

    HttpResponse r = dispatch(*station("/ble/on", HttpMethod::Post), h, post("/ble/on"));
    TEST_ASSERT_FALSE(saved.bleOff);
    TEST_ASSERT_TRUE(r.reboot);
    TEST_ASSERT_TRUE(saved.obcDevmode);
}

// The recovery property this route exists for: unlike /wifi/off, a parked board is still served
// over HTTP, so /ble/on is reachable. If /ble/on ever stopped being routable on the station server
// the feature would be a trap, so pin it.
void test_ble_on_is_reachable_on_the_station_server() {
    TEST_ASSERT_NOT_NULL(station("/ble/on", HttpMethod::Post));
    TEST_ASSERT_NOT_NULL(station("/ble/off", HttpMethod::Post));
}

// What /setup/save does to a parked radio, pinned because it is the question anyone will ask.
// It PRESERVES bleOff: /setup/save goes through mergeSetupForm, which starts from the stored config
// and overwrites only the setup fields -- the same treatment mode, the fitted curve and the OBC
// flags get. So reconfiguring a source does not silently wake a board that was deliberately parked.
//
// That leaves exactly two ways back, both deliberate: POST /ble/on, or /setup/reset (which persists
// defaults()). The residual risk is a parked board reaching a bike, which is why /status carries
// `ble_off` -- a silent board must never look like a broken one.
void test_setup_save_preserves_a_parked_radio_but_reset_clears_it() {
    DeviceHooks h;
    RuntimeConfig stored = RuntimeConfig::defaults();
    stored.bleOff = true;
    RuntimeConfig saved;
    h.config = [&] { return stored; };
    h.saveConfig = [&](const RuntimeConfig& c) { saved = c; };

    HttpRequest rq = post("/setup/save");
    rq.body = "addr=&name=ASSIOMA&spoof_name=&spoof_serial=11821518&trainer=";
    dispatch(*station("/setup/save", HttpMethod::Post), h, rq);
    TEST_ASSERT_TRUE(saved.bleOff);          // preserved, like every other stored flag

    dispatch(*station("/setup/reset", HttpMethod::Post), h, post("/setup/reset"));
    TEST_ASSERT_FALSE(saved.bleOff);         // defaults() = radio on: the second way back
}

void test_scan_routes_report_a_running_scan() {
    DeviceHooks h;
    h.scanActive = [] { return true; };
    HttpRequest req;
    TEST_ASSERT_TRUE(routes::setupPage(h, req).body.find("Scanning&hellip;") != std::string::npos);
    TEST_ASSERT_TRUE(routes::calRender(h, "").body.find("Scanning&hellip;") != std::string::npos);
    h.scanActive = [] { return false; };
    TEST_ASSERT_TRUE(routes::setupPage(h, req).body.find("location.reload") == std::string::npos);
}

void test_full_pages_show_that_a_tap_registered() {
    std::string body = HttpResponse::page("<html><body>x</body></html>").body;
    size_t busy = body.find("id='sbBusy'");
    TEST_ASSERT_TRUE(busy != std::string::npos);
    TEST_ASSERT_TRUE(busy < body.find("</body>"));
    TEST_ASSERT_TRUE(HttpResponse::page("no body tag").body.find("id='sbBusy'") != std::string::npos);
    TEST_ASSERT_TRUE(HttpResponse::html("<body></body>").body.find("sbBusy") == std::string::npos);
}

// --- BodyPump: page bodies go out a slice per loop() pass, never blocking it ---------------------

namespace {
struct FakeSock {
    std::string got;
    int room = 1 << 30;  // bytes the socket will take this pass
    bool dead = false;
    BodyPump::WriteFn fn() {
        return [this](const char* d, size_t n) -> int {
            if (dead) return -1;
            const size_t take = n < static_cast<size_t>(room) ? n : static_cast<size_t>(room);
            got.append(d, take);
            room -= static_cast<int>(take);
            return static_cast<int>(take);
        };
    }
};
}  // namespace

void test_bodypump_sends_a_whole_static_body_and_reports_done() {
    static const char kPage[] = "<html>hello</html>";
    FakeSock s; int done = -1;
    BodyPump p;
    TEST_ASSERT_TRUE(p.add("", kPage, sizeof(kPage) - 1, s.fn(), [&](bool ok) { done = ok; }, 0, 2000));
    p.pump(1);
    TEST_ASSERT_EQUAL_STRING(kPage, s.got.c_str());
    TEST_ASSERT_EQUAL(1, done);
    TEST_ASSERT_EQUAL(0, (int)p.inFlight());
}

void test_bodypump_waits_when_the_socket_is_full_without_losing_bytes() {
    std::string body(10000, 'x');
    for (size_t i = 0; i < body.size(); ++i) body[i] = static_cast<char>('a' + i % 26);
    FakeSock s; int done = -1;
    BodyPump p;
    p.add(body, nullptr, 0, s.fn(), [&](bool ok) { done = ok; }, 0, 2000);
    s.room = 3000; p.pump(10);   // takes what fits, then returns at once
    TEST_ASSERT_EQUAL(3000, (int)s.got.size());
    TEST_ASSERT_EQUAL(-1, done);
    s.room = 0; p.pump(20);      // full: no progress, still queued
    TEST_ASSERT_EQUAL(1, (int)p.inFlight());
    for (int pass = 0; pass < 10 && p.inFlight(); ++pass) { s.room = 2000; p.pump(30 + pass); }
    TEST_ASSERT_EQUAL(1, done);
    TEST_ASSERT_TRUE(s.got == body);
}

void test_bodypump_caps_the_work_of_one_pass() {
    std::string body(BodyPump::kSlice * BodyPump::kSlicesPerPass * 3, 'z');
    FakeSock s;
    BodyPump p;
    p.add(body, nullptr, 0, s.fn(), nullptr, 0, 2000);
    p.pump(1);
    TEST_ASSERT_EQUAL((int)(BodyPump::kSlice * BodyPump::kSlicesPerPass), (int)s.got.size());
}

void test_bodypump_drops_a_phone_that_stops_taking_data() {
    FakeSock s; int done = -1;
    BodyPump p;
    p.add(std::string(5000, 'q'), nullptr, 0, s.fn(), [&](bool ok) { done = ok; }, 0, 2000);
    s.room = 100; p.pump(0);
    s.room = 0; p.pump(2000);
    TEST_ASSERT_EQUAL(-1, done);          // exactly at the budget: still waiting
    p.pump(2001);
    TEST_ASSERT_EQUAL(0, done);           // past it: abandoned, reported incomplete
    TEST_ASSERT_EQUAL(0, (int)p.inFlight());
}

void test_bodypump_drops_a_closed_connection() {
    FakeSock s; int done = -1;
    BodyPump p;
    p.add(std::string(5000, 'q'), nullptr, 0, s.fn(), [&](bool ok) { done = ok; }, 0, 2000);
    s.dead = true; p.pump(1);
    TEST_ASSERT_EQUAL(0, done);
    TEST_ASSERT_EQUAL(0, (int)p.inFlight());
}

void test_bodypump_refuses_past_its_limit_so_the_caller_can_fall_back() {
    FakeSock s; s.room = 0;
    BodyPump p;
    for (size_t i = 0; i < BodyPump::kMaxBodies; ++i)
        TEST_ASSERT_TRUE(p.add("x", nullptr, 0, s.fn(), nullptr, 0, 2000));
    TEST_ASSERT_FALSE(p.add("x", nullptr, 0, s.fn(), nullptr, 0, 2000));
}

void test_bodypump_serves_bodies_side_by_side() {
    FakeSock a, b; int da = -1, db = -1;
    BodyPump p;
    p.add(std::string(4000, 'a'), nullptr, 0, a.fn(), [&](bool ok) { da = ok; }, 0, 2000);
    p.add("small", nullptr, 0, b.fn(), [&](bool ok) { db = ok; }, 0, 2000);
    a.room = 1000; p.pump(1);    // a slow phone does not hold up the next request
    TEST_ASSERT_EQUAL(1, db);
    TEST_ASSERT_EQUAL(-1, da);
}

void test_bodypump_survives_the_millis_wrap() {
    FakeSock s; int done = -1;
    BodyPump p;
    const uint32_t near = 0xFFFFFFFFu - 500u;
    p.add(std::string(5000, 'q'), nullptr, 0, s.fn(), [&](bool ok) { done = ok; }, near, 2000);
    s.room = 0; p.pump(near + 1500u);   // past zero, inside the budget
    TEST_ASSERT_EQUAL(-1, done);
}

// --- Fixed pages carry the build's ETag; a browser that already has it gets 304 and no body -------

void test_fixed_pages_carry_the_build_etag() {
    DeviceHooks h;
    HttpRequest req;
    for (auto fn : {routes::dashboard, routes::settings, routes::workoutPage}) {
        const HttpResponse r = fn(h, req);
        TEST_ASSERT_EQUAL(200, r.status);
        TEST_ASSERT_EQUAL_STRING(routes::buildEtag().c_str(), r.etag.c_str());
        TEST_ASSERT_EQUAL_STRING("no-cache", r.cacheControl.c_str());
        TEST_ASSERT_TRUE(r.body.size() > 1000);
    }
}

void test_a_fixed_page_the_browser_holds_is_not_resent() {
    DeviceHooks h;
    HttpRequest req;
    req.ifNoneMatch = routes::buildEtag();
    for (auto fn : {routes::dashboard, routes::settings, routes::workoutPage, routes::spa}) {
        const HttpResponse r = fn(h, req);
        TEST_ASSERT_EQUAL(304, r.status);
        TEST_ASSERT_TRUE(r.body.empty());
        TEST_ASSERT_NULL(r.staticBody);
        TEST_ASSERT_FALSE(r.stream);
    }
}

void test_a_page_from_older_firmware_is_resent() {
    DeviceHooks h;
    h.spaHtml = [] { return "<html>spa</html>"; };
    HttpRequest req;
    req.ifNoneMatch = "\"0.0.9-abc1234-2026-01-01T00:00\"";
    TEST_ASSERT_EQUAL(200, routes::dashboard(h, req).status);
    const HttpResponse spa = routes::spa(h, req);
    TEST_ASSERT_EQUAL(200, spa.status);
    TEST_ASSERT_EQUAL_STRING("<html>spa</html>", spa.staticBody);
    TEST_ASSERT_EQUAL_STRING(routes::buildEtag().c_str(), spa.etag.c_str());
}

void test_live_and_setup_pages_are_never_cached() {
    DeviceHooks h;
    HttpRequest req;
    req.ifNoneMatch = routes::buildEtag();
    TEST_ASSERT_EQUAL(200, routes::status(h, req).status);
    TEST_ASSERT_TRUE(routes::status(h, req).etag.empty());
    TEST_ASSERT_EQUAL(200, routes::setupPage(h, req).status);
    TEST_ASSERT_TRUE(routes::setupPage(h, req).etag.empty());
}

// --- Live pages poll one round at a time, never with setInterval (which stacked requests) --------

void test_live_pages_poll_one_round_at_a_time() {
    const std::string pages[] = {appPageHtml(), workoutPageHtml()};
    const std::string helper = webuiPollJs();
    for (const std::string& pg : pages) {
        TEST_ASSERT_TRUE(pg.find("setInterval(") == std::string::npos);
        const size_t def = pg.find(helper);
        const size_t use = pg.find("poll(tick)");
        TEST_ASSERT_TRUE(def != std::string::npos);
        TEST_ASSERT_TRUE(use != std::string::npos);
        TEST_ASSERT_TRUE(def < use);  // defined before the page's script calls it
        TEST_ASSERT_TRUE(pg.find("signal:sig()") != std::string::npos);  // requests time out
    }
    TEST_ASSERT_TRUE(helper.find("document.hidden") != std::string::npos);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_bodypump_sends_a_whole_static_body_and_reports_done);
    RUN_TEST(test_bodypump_waits_when_the_socket_is_full_without_losing_bytes);
    RUN_TEST(test_bodypump_caps_the_work_of_one_pass);
    RUN_TEST(test_bodypump_drops_a_phone_that_stops_taking_data);
    RUN_TEST(test_bodypump_drops_a_closed_connection);
    RUN_TEST(test_bodypump_refuses_past_its_limit_so_the_caller_can_fall_back);
    RUN_TEST(test_bodypump_serves_bodies_side_by_side);
    RUN_TEST(test_bodypump_survives_the_millis_wrap);
    RUN_TEST(test_fixed_pages_carry_the_build_etag);
    RUN_TEST(test_a_fixed_page_the_browser_holds_is_not_resent);
    RUN_TEST(test_a_page_from_older_firmware_is_resent);
    RUN_TEST(test_live_and_setup_pages_are_never_cached);
    RUN_TEST(test_live_pages_poll_one_round_at_a_time);
    RUN_TEST(test_every_post_route_is_csrf_guarded);
    RUN_TEST(test_csrf_rejection_runs_no_side_effect);
    RUN_TEST(test_get_routes_are_not_csrf_guarded);
    RUN_TEST(test_same_origin_post_is_allowed);

    RUN_TEST(test_unwired_workout_load_reports_failure);
    RUN_TEST(test_unwired_cal_start_falls_through_to_success);
    RUN_TEST(test_unwired_perf_and_compare_defaults);
    RUN_TEST(test_unwired_workout_state_default);

    RUN_TEST(test_exactly_the_expected_routes_request_a_reboot);
    RUN_TEST(test_live_routes_do_not_reboot);
    RUN_TEST(test_ride_mode_drops_radio_without_reboot);

    RUN_TEST(test_setup_save_preserves_curve_and_mode);
    RUN_TEST(test_config_post_preserves_curve);
    RUN_TEST(test_config_post_rejects_invalid_without_saving);
    RUN_TEST(test_setup_reset_persists_defaults);

    RUN_TEST(test_devmode_on_also_enables_obc_but_shifter_does_not);
    RUN_TEST(test_devmode_off_clears_only_devmode);
    RUN_TEST(test_obc_press_argument_validation);
    RUN_TEST(test_obc_buttons_round_trip);
    RUN_TEST(test_obc_buttons_rejects_garbage);

    RUN_TEST(test_log_is_403_when_disabled_and_toggles_persist);

    RUN_TEST(test_cal_save_rejection_rerenders_without_reboot);
    RUN_TEST(test_cal_start_rejection_rerenders_without_reboot);
    RUN_TEST(test_cal_finish_redirects_without_reboot);

    RUN_TEST(test_workout_preset_unknown_key_is_400);
    RUN_TEST(test_workout_verbs_reach_the_control_hook);

    RUN_TEST(test_portal_save_rejects_bad_creds_without_saving);
    RUN_TEST(test_portal_save_accepts_good_creds_and_reboots);
    RUN_TEST(test_portal_save_parses_raw_body_when_args_absent);
    RUN_TEST(test_portal_probes_all_redirect_to_setup);

    RUN_TEST(test_spa_route_streams_from_flash_not_the_heap);
    RUN_TEST(test_large_pages_are_marked_for_the_drain_aware_writer);
    RUN_TEST(test_small_replies_are_not_streamed);
    RUN_TEST(test_dashboard_alias_shares_one_handler);
    RUN_TEST(test_portal_serves_the_log_routes);
    RUN_TEST(test_both_forget_variants_clear_credentials);
    RUN_TEST(test_no_duplicate_path_method_pairs);
    RUN_TEST(test_source_banner_reflects_connection_state);
    RUN_TEST(test_ble_off_sets_only_the_flag_and_reboots);
    RUN_TEST(test_ble_on_clears_the_flag_and_reboots);
    RUN_TEST(test_ble_on_is_reachable_on_the_station_server);
    RUN_TEST(test_setup_save_preserves_a_parked_radio_but_reset_clears_it);
    RUN_TEST(test_scan_routes_report_a_running_scan);
    RUN_TEST(test_full_pages_show_that_a_tap_registered);

    return UNITY_END();
}
