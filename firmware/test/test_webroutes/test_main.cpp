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

#include "WebRoutes.h"
#include "WorkoutPresets.h"
#include "WorkoutRuntime.h"

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
    // The unwired workout default, plus the erg leg's unwired default (#347): a dead link, no bias.
    DeviceHooks h;
    TEST_ASSERT_EQUAL_STRING(
        "{\"loaded\":false,\"erg_connected\":false,\"erg_controlled\":false,\"bias_w\":0}",
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
        {"/workout/trainer", HttpMethod::Post, "name=SB20-FTMS-Server"},  // a CHANGED trainer
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

// --- the shared SPA's routes (#347) ------------------------------------------

// Every route web/index.html's HttpTransport calls must exist with the method it uses. This is the
// list the SPA was written against (web/HTTP-API.md); four of these were phantoms until #347.
void test_every_route_the_spa_calls_exists() {
    struct Want { const char* path; HttpMethod m; };
    const std::vector<Want> spa = {
        {"/status", HttpMethod::Get},          {"/workout/state", HttpMethod::Get},
        {"/scan", HttpMethod::Get},            {"/calibrate/state", HttpMethod::Get},
        {"/config", HttpMethod::Get},          {"/config", HttpMethod::Post},
        {"/curve", HttpMethod::Get},           {"/curve", HttpMethod::Post},
        {"/obc/buttons.json", HttpMethod::Get}, {"/obc/buttons.json", HttpMethod::Post},
        {"/compare", HttpMethod::Get},         {"/setup/scan", HttpMethod::Post},
        {"/calibrate/start", HttpMethod::Post}, {"/calibrate/finish", HttpMethod::Post},
        {"/calibrate/cancel", HttpMethod::Post}, {"/calibrate/save", HttpMethod::Post},
        {"/workout/trainer", HttpMethod::Post}, {"/workout/preset", HttpMethod::Post},
        {"/workout/bias", HttpMethod::Post},
    };
    for (const auto& w : spa) TEST_ASSERT_NOT_NULL_MESSAGE(station(w.path, w.m), w.path);
    // The workout verbs (start/pause/resume/stop) are registered from workoutVerbs(), not the table.
    for (const char* v : {"start", "pause", "resume", "stop"}) {
        bool found = false;
        for (const char* x : workoutVerbs()) found = found || std::string(x) == v;
        TEST_ASSERT_TRUE_MESSAGE(found, v);
    }
    // The legacy page's GET /setup/scan link is untouched by the new POST twin.
    TEST_ASSERT_NOT_NULL(station("/setup/scan", HttpMethod::Get));
}

// /workout/state = the engine's JSON, byte for byte, then the three erg keys. Uses the real engine
// output for a running preset so the splice is tested against what the board actually emits.
void test_workout_state_appends_erg_fields_to_the_engine_json() {
    WorkoutRuntime rt;
    rt.load(parseWorkout(presetJson("4x8")));
    rt.start(0);
    const std::string engine = rt.json(700000);  // 700 s in: Interval 1
    DeviceHooks h;
    h.workoutState = [&] { return engine; };
    h.ergLink = [] { ErgLink e; e.connected = true; e.controlled = true; e.biasW = -10; return e; };
    const std::string body = routes::workoutStateJson(h, get("/workout/state")).body;
    const std::string tail = ",\"erg_connected\":true,\"erg_controlled\":true,\"bias_w\":-10}";
    TEST_ASSERT_EQUAL_STRING((engine.substr(0, engine.size() - 1) + tail).c_str(), body.c_str());
    TEST_ASSERT_TRUE(body.find("\"seg_label\":\"Interval 1\"") != std::string::npos);
}

void test_workout_state_json_leaves_a_non_object_alone() {
    ErgLink e;
    TEST_ASSERT_EQUAL_STRING("", renderWorkoutStateJson("", e).c_str());
    TEST_ASSERT_EQUAL_STRING("oops", renderWorkoutStateJson("oops", e).c_str());
}

void test_workout_bias_nudges_through_the_hook() {
    DeviceHooks h;
    int got = 0;
    h.ergBias = [&](int d) { got = d; return 20; };
    HttpRequest r = post("/workout/bias");
    r.args = {{"d", "-10"}};  // the SPA sends ?d= in the query string
    const HttpResponse resp = dispatch(*station("/workout/bias", HttpMethod::Post), h, r);
    TEST_ASSERT_EQUAL_INT(200, resp.status);
    TEST_ASSERT_EQUAL_INT(-10, got);
    TEST_ASSERT_EQUAL_STRING("{\"bias_w\":20}", resp.body.c_str());
    TEST_ASSERT_FALSE(resp.reboot);  // a nudge is live, never a restart mid-ride
    // A form body works too (curl -d d=10).
    got = 0;
    routes::workoutBias(h, post("/workout/bias", "d=10"));
    TEST_ASSERT_EQUAL_INT(10, got);
}

void test_workout_bias_rejects_bad_deltas_without_calling_the_hook() {
    DeviceHooks h;
    bool called = false;
    h.ergBias = [&](int) { called = true; return 0; };
    for (const char* d : {"", "abc", "10x", "51", "-51", "9999"}) {
        HttpRequest r = post("/workout/bias");
        if (*d) r.args = {{"d", d}};
        TEST_ASSERT_EQUAL_INT_MESSAGE(400, routes::workoutBias(h, r).status, d);
    }
    TEST_ASSERT_FALSE(called);
}

void test_erg_bias_is_clamped() {
    TEST_ASSERT_EQUAL_INT(10, nudgeErgBias(0, 10));
    TEST_ASSERT_EQUAL_INT(-20, nudgeErgBias(-10, -10));
    TEST_ASSERT_EQUAL_INT(kErgBiasLimitW, nudgeErgBias(195, 10));
    TEST_ASSERT_EQUAL_INT(-kErgBiasLimitW, nudgeErgBias(-195, -10));
}

// Set trainer persists the name on top of the STORED config (never a wipe of the curve, identity or
// meter pin) and reboots, because the erg client is built at boot.
void test_workout_trainer_persists_and_reboots_only_on_change() {
    DeviceHooks h;
    RuntimeConfig stored = RuntimeConfig::defaults();
    stored.meterAddress = "aa:bb:cc:dd:ee:ff";
    stored.spoofName = "Stages 99744";
    stored.curve = curveFromString("100.0:1.0500,200.0:0.9800");
    int saves = 0;
    RuntimeConfig saved;
    h.config = [&] { return stored; };
    h.saveConfig = [&](const RuntimeConfig& c) { saved = c; ++saves; };

    // The SPA posts text/plain, so the board hands the raw body over, not parsed args.
    HttpResponse r = dispatch(*station("/workout/trainer", HttpMethod::Post), h,
                              post("/workout/trainer", "name=SB20-FTMS-Server"));
    TEST_ASSERT_TRUE(r.reboot);
    TEST_ASSERT_EQUAL_INT(1, saves);
    TEST_ASSERT_EQUAL_STRING("SB20-FTMS-Server", saved.trainerNameFilter.c_str());
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", saved.meterAddress.c_str());
    TEST_ASSERT_EQUAL_STRING("Stages 99744", saved.spoofName.c_str());
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)saved.curve.points.size());

    // Unchanged: a reply, no save, no restart.
    stored.trainerNameFilter = "SB20-FTMS-Server";
    r = routes::workoutTrainer(h, post("/workout/trainer", "name=SB20-FTMS-Server"));
    TEST_ASSERT_FALSE(r.reboot);
    TEST_ASSERT_EQUAL_INT(1, saves);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"reboot\":false}", r.body.c_str());

    // Blank clears (erg off) - still a change, so it persists and reboots.
    r = routes::workoutTrainer(h, post("/workout/trainer", "name="));
    TEST_ASSERT_TRUE(r.reboot);
    TEST_ASSERT_EQUAL_STRING("", saved.trainerNameFilter.c_str());
}

void test_setup_scan_post_rescans_and_answers_json() {
    DeviceHooks h;
    bool rescanned = false;
    h.rescanSources = [&] { rescanned = true; };
    const HttpResponse r = dispatch(*station("/setup/scan", HttpMethod::Post), h, post("/setup/scan"));
    TEST_ASSERT_TRUE(rescanned);
    TEST_ASSERT_EQUAL_INT(200, r.status);
    TEST_ASSERT_EQUAL_STRING("application/json", r.contentType.c_str());
    TEST_ASSERT_FALSE(r.reboot);
}

// GET /calibrate/state is the wizard view as JSON. Idle carries the picker list WITH addresses (the
// start route pins by address); the state numbering is the nRF Cal characteristic's 0/1/2.
void test_calibrate_state_idle_lists_devices_with_addresses() {
    DeviceHooks h;
    h.calView = [] {
        CalWizardView v;
        SourceCandidate a;
        a.address = "e6:20:90:8c:f3:fe"; a.name = "ASSIOMA17039L"; a.rssi = -60; a.isCps = true;
        SourceCandidate b;
        b.address = "c0:ff:ee:00:00:01"; b.name = "XCADEY"; b.rssi = -48; b.isCps = true;
        v.devices = {a, b};
        return v;
    };
    const std::string j = routes::calibrateState(h, get("/calibrate/state")).body;
    TEST_ASSERT_TRUE(j.rfind("{\"state\":0,", 0) == 0);
    TEST_ASSERT_TRUE(j.find("\"addr\":\"e6:20:90:8c:f3:fe\"") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"name\":\"XCADEY\"") != std::string::npos);
    TEST_ASSERT_TRUE(j.find("\"coverage\":[]") != std::string::npos);
}

void test_calibrate_state_collecting_reports_progress() {
    CalWizardView v;
    v.state = CalState::Collecting;
    v.pairCount = 40;
    v.minPairs = 30;
    v.enoughToFit = true;
    v.dutConnected = true;
    v.coverage = {1, 2, 5, 5, 3, 0};
    TEST_ASSERT_EQUAL_STRING(
        "{\"state\":1,\"pairs\":40,\"min_pairs\":30,\"residual_w\":0.0,\"enough\":true,"
        "\"dut_connected\":true,\"ref_connected\":false,\"coverage\":[1,2,5,5,3,0],\"devices\":[]}",
        renderCalStateJson(v).c_str());
    v.state = CalState::Fitted;
    v.residualW = 3.25f;
    TEST_ASSERT_TRUE(renderCalStateJson(v).find("\"state\":2,") != std::string::npos);
    TEST_ASSERT_TRUE(renderCalStateJson(v).find("\"residual_w\":3.2") != std::string::npos);
}

// Device names are attacker-controlled (anything can advertise); the JSON must stay well-formed.
void test_calibrate_state_escapes_device_names() {
    CalWizardView v;
    SourceCandidate a;
    a.address = "aa:bb:cc:dd:ee:ff"; a.name = "x\"},{\"evil\":1";
    v.devices = {a};
    const std::string j = renderCalStateJson(v);
    TEST_ASSERT_TRUE(j.find("\"evil\"") == std::string::npos);
}

int main(int, char**) {
    UNITY_BEGIN();

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

    RUN_TEST(test_every_route_the_spa_calls_exists);
    RUN_TEST(test_workout_state_appends_erg_fields_to_the_engine_json);
    RUN_TEST(test_workout_state_json_leaves_a_non_object_alone);
    RUN_TEST(test_workout_bias_nudges_through_the_hook);
    RUN_TEST(test_workout_bias_rejects_bad_deltas_without_calling_the_hook);
    RUN_TEST(test_erg_bias_is_clamped);
    RUN_TEST(test_workout_trainer_persists_and_reboots_only_on_change);
    RUN_TEST(test_setup_scan_post_rescans_and_answers_json);
    RUN_TEST(test_calibrate_state_idle_lists_devices_with_addresses);
    RUN_TEST(test_calibrate_state_collecting_reports_progress);
    RUN_TEST(test_calibrate_state_escapes_device_names);

    return UNITY_END();
}
