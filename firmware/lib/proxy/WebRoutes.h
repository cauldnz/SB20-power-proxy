#pragma once
// Pure HTTP route layer for the on-device web UI.
//
// WifiLink.cpp used to be 48 route registrations, each a lambda closing over the
// WebServer: read a hook (guarded by its own `hook_ ? hook_() : default` ternary),
// call a pure page/JSON renderer, send, and sometimes `delay(400); esp_restart()`.
// The renderers were already pure and host-tested (ConfigPage.h, Provisioning.h,
// CalibrationPage.h, Status.h, WebJson.h, DiagReport.h); the *wiring* around them
// was not testable at all, because it only existed inside Arduino lambdas.
//
// This header is that wiring, as data plus pure functions:
//   * HttpRequest / HttpResponse  — what a route consumes and produces.
//   * DeviceHooks                 — every hook, each with a non-null default, so a
//                                   handler never writes a null check.
//   * Route + stationRoutes()/portalRoutes() — the route table as a value.
//   * dispatch()                  — the one place CSRF is enforced and the only
//                                   thing that decides a request's outcome.
//
// WifiLink.cpp is now an adapter: translate WebServer -> HttpRequest, dispatch,
// translate HttpResponse -> WebServer. It contains no routing decisions.
//
// THE SECURITY INVARIANT THIS BUYS. Every state-changing route was CSRF-guarded by
// a hand-written `if (!csrfOk_()) return;` as its first line — 21 of them, correct
// but only by inspection, and one forgotten line would have been a silent hole.
// Measured against the running board, the rule was exactly "every POST is guarded,
// no GET is" with zero exceptions, so dispatch() enforces it structurally on the
// method. A new POST route cannot be added unguarded; there is no per-route opt-out
// to forget. (GET routes with effects — /log/on, /stats/reset, /obc/press — keep
// their documented GET semantics; they are transient and non-destructive.)
//
// Reboots are returned as intent (HttpResponse::reboot), not performed in the
// handler. The adapter owns the single "flush, then restart" sequence, so the
// twelve copies of `delay(400); esp_restart();` became one.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "CalibrationPage.h"
#include "Config.h"
#include "ConfigPage.h"
#include "Correction.h"
#include "DiagReport.h"
#include "HttpSecurity.h"
#include "Provisioning.h"
#include "RuntimeConfig.h"
#include "Sb20ButtonMap.h"
#include "SourceCandidate.h"
#include "Status.h"
#include "WebApp.h"
#include "WebJson.h"
#include "WorkoutPresets.h"

namespace sb20proxy {

enum class HttpMethod { Get, Post };

struct HttpRequest {
    HttpMethod method = HttpMethod::Get;
    std::string uri;
    std::string host;
    std::string origin;
    std::string referer;
    std::string body;  // form body, or the raw payload for a JSON POST
    std::vector<std::pair<std::string, std::string>> args;

    bool hasArg(const std::string& k) const {
        for (const auto& a : args)
            if (a.first == k) return true;
        return false;
    }
    std::string arg(const std::string& k) const {
        for (const auto& a : args)
            if (a.first == k) return a.second;
        return std::string();
    }
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "text/plain";
    std::string body;
    // Stream this straight from flash instead of `body`. The embedded SPA is ~34 KB
    // and the C3's heap is tight beside BLE, so /app must never materialise it as a
    // std::string — the adapter writes it in slices from the pointer.
    const char* staticBody = nullptr;
    std::string location;  // non-empty => send a Location header with this status
    bool stream = false;   // use the drain-aware slice writer (multi-KB HTML)
    bool reboot = false;   // restart once the response has been flushed
    bool radioOff = false; // ride mode: power the radio down after replying

    static HttpResponse text(std::string b, int st = 200) {
        HttpResponse r;
        r.status = st;
        r.contentType = "text/plain";
        r.body = std::move(b);
        return r;
    }
    static HttpResponse json(std::string b, int st = 200) {
        HttpResponse r;
        r.status = st;
        r.contentType = "application/json";
        r.body = std::move(b);
        return r;
    }
    // Small HTML replies go out with a plain send; `page()` marks the multi-KB ones
    // that need the drain-aware writer (Arduino's WebServer ignores short writes, so
    // under lwIP pressure a big page is silently truncated — measured on the CYD).
    static HttpResponse html(std::string b, int st = 200) {
        HttpResponse r;
        r.status = st;
        r.contentType = "text/html";
        r.body = std::move(b);
        return r;
    }
    static HttpResponse page(std::string b) {
        HttpResponse r = html(std::move(b));
        r.stream = true;
        return r;
    }
    static HttpResponse redirect(std::string to, int st = 303, std::string note = "") {
        HttpResponse r = text(std::move(note), st);
        r.location = std::move(to);
        return r;
    }
};

// A completed portal scan: the networks found, plus whether another scan is still
// in flight (the page shows "Scanning..." and auto-refreshes until it lands).
struct PortalScan {
    std::vector<ScannedNet> networks;
    bool scanning = false;
};

// Every seam the routes reach through, each defaulted so a handler never null-checks.
// The defaults reproduce exactly what the pre-refactor ternaries did when a hook was
// unset — including the two that are load-bearing: `workoutLoad` returns false (an
// unwired engine must 400, not silently claim success), and `calStart`/`calSave`
// return true (their call sites read `hook && !hook(...)`, so an unset hook fell
// through to the success path).
struct DeviceHooks {
    std::function<ProxyStatus()> status = [] { return ProxyStatus{}; };
    std::function<RuntimeConfig()> config = [] { return RuntimeConfig::defaults(); };
    std::function<void(const RuntimeConfig&)> saveConfig = [](const RuntimeConfig&) {};
    std::function<std::vector<SourceCandidate>()> sources = [] {
        return std::vector<SourceCandidate>{};
    };
    std::function<void()> rescanSources = [] {};

    std::function<std::string()> perfJson = [] { return std::string("{}"); };
    std::function<void()> perfReset = [] {};
    std::function<std::string()> compareJson = [] { return std::string("{\"valid\":false}"); };
    std::function<std::vector<std::string>()> diagFrames = [] {
        return std::vector<std::string>{};
    };
    std::function<void(const CorrectionCurve&)> setCurve = [](const CorrectionCurve&) {};

    std::function<CalWizardView()> calView = [] { return CalWizardView{}; };
    std::function<bool(const std::string&, const std::string&)> calStart =
        [](const std::string&, const std::string&) { return true; };
    std::function<bool()> calFinish = [] { return true; };
    std::function<bool(const std::string&)> calSave = [](const std::string&) { return true; };
    std::function<void()> calCancel = [] {};
    std::function<void()> calScan = [] {};

    std::function<std::string()> workoutState = [] { return std::string("{\"loaded\":false}"); };
    std::function<bool(const std::string&)> workoutLoad = [](const std::string&) { return false; };
    std::function<void(const std::string&)> workoutControl = [](const std::string&) {};

    std::function<void(uint8_t, uint8_t)> obcPress = [](uint8_t, uint8_t) {};
    std::function<void(bool, const Sb20ButtonMap&)> obcButtons = [](bool, const Sb20ButtonMap&) {};

    std::function<std::string()> logText = [] { return std::string(); };
    std::function<bool()> logEnabled = [] { return true; };
    std::function<void(bool)> setLogEnabled = [](bool) {};

    std::function<void()> clearCreds = [] {};
    std::function<void(const WifiCredentials&)> saveCreds = [](const WifiCredentials&) {};
    std::function<PortalScan()> portalScan = [] { return PortalScan{}; };
    std::function<void()> startRescan = [] {};

    std::function<const char*()> spaHtml = []() -> const char* { return ""; };
};

// ---------------------------------------------------------------------------
// Handlers. Each is a pure function of (hooks, request) -> response.
// ---------------------------------------------------------------------------

namespace routes {

// -- dashboard / observability ---------------------------------------------

inline HttpResponse dashboard(DeviceHooks&, const HttpRequest&) {
    return HttpResponse::page(appPageHtml());
}
inline HttpResponse settings(DeviceHooks&, const HttpRequest&) {
    return HttpResponse::page(settingsPageHtml());
}
inline HttpResponse status(DeviceHooks& h, const HttpRequest&) {
    return HttpResponse::json(renderStatusJson(h.status()));
}
inline HttpResponse spa(DeviceHooks& h, const HttpRequest&) {
    HttpResponse r;
    r.contentType = "text/html";
    r.staticBody = h.spaHtml();  // streamed from flash; never copied to the heap
    return r;
}
inline HttpResponse stats(DeviceHooks& h, const HttpRequest&) {
    return HttpResponse::json(h.perfJson());
}
inline HttpResponse compare(DeviceHooks& h, const HttpRequest&) {
    return HttpResponse::json(h.compareJson());
}
inline HttpResponse statsReset(DeviceHooks& h, const HttpRequest&) {
    h.perfReset();
    return HttpResponse::text("perf window reset\n");
}

// -- serial-over-HTTP log ---------------------------------------------------

inline HttpResponse log(DeviceHooks& h, const HttpRequest&) {
    if (!h.logEnabled()) return HttpResponse::text("log disabled - enable at /log/on\n", 403);
    return HttpResponse::text(h.logText());
}
inline HttpResponse logOn(DeviceHooks& h, const HttpRequest&) {
    h.setLogEnabled(true);
    return HttpResponse::text("log enabled\n");
}
inline HttpResponse logOff(DeviceHooks& h, const HttpRequest&) {
    h.setLogEnabled(false);
    return HttpResponse::text("log disabled\n");
}

// -- source config ----------------------------------------------------------

inline std::string sourceBanner(const ProxyStatus& st) {
    if (st.mock) return "Running a simulated meter (test build).";
    if (st.sourceConnected)
        return "Reading " + (st.srcName.empty() ? std::string("your source") : st.srcName) +
               " \xE2\x9C\x93";  // checkmark
    return "Searching for your source\xE2\x80\xA6";  // ellipsis
}

inline HttpResponse setupPage(DeviceHooks& h, const HttpRequest&) {
    return HttpResponse::page(renderConfigPage(h.config(), h.sources(), std::string(), false, -1,
                                               sourceBanner(h.status())));
}
inline HttpResponse setupScan(DeviceHooks& h, const HttpRequest&) {
    h.rescanSources();
    return HttpResponse::redirect("/setup", 303, "scanning\n");
}
inline HttpResponse setupSave(DeviceHooks& h, const HttpRequest& req) {
    // Merge onto the STORED config: this page owns only the source, spoof identity and
    // trainer, so it must not wipe the broadcast mode, fitted curve or reference meter
    // that the SPA / calibration wizard set.
    const RuntimeConfig cfg = mergeSetupForm(h.config(), req.body);
    if (const char* err = configValidationError(cfg))
        return HttpResponse::page(renderConfigPage(cfg, h.sources(), err));
    h.saveConfig(cfg);
    HttpResponse r = HttpResponse::page(renderConfigSavedPage(cfg));
    r.reboot = true;  // reboot to apply the new source
    return r;
}
inline HttpResponse setupReset(DeviceHooks& h, const HttpRequest&) {
    // Persisting defaults() == clearing: the next boot loads the shipped defaults.
    h.saveConfig(RuntimeConfig::defaults());
    HttpResponse r = HttpResponse::html(
        "<!DOCTYPE html><meta charset='utf-8'><meta name='viewport' "
        "content='width=device-width,initial-scale=1'><body style='font-family:"
        "system-ui,sans-serif;max-width:480px;margin:0 auto;padding:16px'>"
        "<h1>Reset &#10003;</h1><p>Source and crank identity restored to defaults &mdash; "
        "restarting. Open <a href='/'>the dashboard</a> in a moment to set up again.</p>");
    r.reboot = true;
    return r;
}
inline HttpResponse scanJson(DeviceHooks& h, const HttpRequest&) {
    return HttpResponse::json(renderScanJson(h.sources()));
}
inline HttpResponse configJson(DeviceHooks& h, const HttpRequest&) {
    return HttpResponse::json(renderConfigJson(h.config()));
}
inline HttpResponse configSave(DeviceHooks& h, const HttpRequest& req) {
    // The SPA's own field vocabulary; merges so it never wipes the curve / reference
    // meter / trainer, then reboots to apply the identity (built at boot).
    const RuntimeConfig cfg = mergeSpaConfigForm(h.config(), req.body);
    if (const char* err = configValidationError(cfg))
        return HttpResponse::json(std::string("{\"error\":\"") + err + "\"}", 400);
    h.saveConfig(cfg);
    HttpResponse r = HttpResponse::json("{\"ok\":true,\"reboot\":true}");
    r.reboot = true;
    return r;
}
inline HttpResponse curveGet(DeviceHooks& h, const HttpRequest&) {
    return HttpResponse::json(renderCurveJson(h.config().curve));
}
inline HttpResponse curveSet(DeviceHooks& h, const HttpRequest& req) {
    const CorrectionCurve curve = curveFromString(req.body);  // empty body clears it
    h.setCurve(curve);                                        // live; no reboot
    return HttpResponse::json(renderCurveJson(curve));
}
inline HttpResponse diag(DeviceHooks& h, const HttpRequest&) {
    return HttpResponse::text(renderDiagReport(h.config(), h.status(), h.diagFrames()));
}
inline HttpResponse report(DeviceHooks&, const HttpRequest&) {
    return HttpResponse::page(diagReportPageHtml());
}

// -- calibration wizard -----------------------------------------------------

inline HttpResponse calRender(DeviceHooks& h, const std::string& message) {
    CalWizardView v = h.calView();
    if (!message.empty()) v.message = message;
    return HttpResponse::page(renderCalibrationPage(v));
}
inline HttpResponse calibrate(DeviceHooks& h, const HttpRequest&) {
    return calRender(h, std::string());
}
inline HttpResponse calibrateScan(DeviceHooks& h, const HttpRequest&) {
    h.calScan();
    return HttpResponse::redirect("/calibrate", 303, "scanning\n");
}
inline HttpResponse calibrateStart(DeviceHooks& h, const HttpRequest& req) {
    const CalForm f = parseCalibrationForm(req.body);
    if (const char* err = calibrationStartError(f)) return calRender(h, err);
    if (!h.calStart(f.dutAddr, f.refAddr))
        return calRender(h,
                         "A calibration is already in progress \xE2\x80\x94 reopen the wizard "
                         "to continue it.");
    HttpResponse r = HttpResponse::html(
        "<!DOCTYPE html><meta charset='utf-8'><meta name='viewport' "
        "content='width=device-width,initial-scale=1'><body style='font-family:"
        "system-ui,sans-serif;max-width:480px;margin:0 auto;padding:16px'>"
        "<h1>Starting calibration&hellip;</h1><p>Connecting to both meters &mdash; the "
        "device is restarting. Reopen <a href='/calibrate'>the wizard</a> in a moment "
        "and start your power sweep.</p>");
    r.reboot = true;
    return r;
}
inline HttpResponse calibrateFinish(DeviceHooks& h, const HttpRequest&) {
    h.calFinish();  // fits in place (or no-ops if too few pairs); no reboot
    return HttpResponse::redirect("/calibrate", 303, "fitting\n");
}
inline HttpResponse calibrateSave(DeviceHooks& h, const HttpRequest& req) {
    const CalForm f = parseCalibrationForm(req.body);
    if (!h.calSave(f.deviceName))  // not fitted yet — don't reboot
        return calRender(h,
                         "Finish the calibration first \xE2\x80\x94 there's no fitted "
                         "correction to save yet.");
    HttpResponse r = HttpResponse::html(
        "<!DOCTYPE html><meta charset='utf-8'><meta name='viewport' "
        "content='width=device-width,initial-scale=1'><body style='font-family:"
        "system-ui,sans-serif;max-width:480px;margin:0 auto;padding:16px'>"
        "<h1>Saved &#10003;</h1><p>Switching to corrector mode &mdash; restarting. Now "
        "remove the reference meter; the corrected meter is rebroadcast under your "
        "chosen name. Open <a href='/'>the dashboard</a> in a moment.</p>");
    r.reboot = true;
    return r;
}
inline HttpResponse calibrateCancel(DeviceHooks& h, const HttpRequest&) {
    h.calCancel();
    HttpResponse r = HttpResponse::html(
        "<!DOCTYPE html><meta charset='utf-8'><meta name='viewport' "
        "content='width=device-width,initial-scale=1'><body style='font-family:"
        "system-ui,sans-serif;max-width:480px;margin:0 auto;padding:16px'>"
        "<h1>Calibration cancelled</h1><p>Restarting. Open <a href='/'>the dashboard</a> "
        "in a moment.</p>");
    r.reboot = true;
    return r;
}

// -- workout ----------------------------------------------------------------

inline HttpResponse workoutPage(DeviceHooks&, const HttpRequest&) {
    return HttpResponse::page(workoutPageHtml());
}
inline HttpResponse workoutStateJson(DeviceHooks& h, const HttpRequest&) {
    return HttpResponse::json(h.workoutState());
}
inline HttpResponse workoutLoad(DeviceHooks& h, const HttpRequest& req) {
    const bool ok = h.workoutLoad(req.body);
    return HttpResponse::text(ok ? "loaded\n" : "bad workout\n", ok ? 200 : 400);
}
inline HttpResponse workoutPreset(DeviceHooks& h, const HttpRequest& req) {
    const std::string j = presetJson(req.arg("key"));
    const bool ok = !j.empty() && h.workoutLoad(j);
    return HttpResponse::text(ok ? "loaded\n" : "unknown preset\n", ok ? 200 : 400);
}

// -- OBC --------------------------------------------------------------------

inline HttpResponse obcHelp(DeviceHooks& h, const HttpRequest&) {
    const RuntimeConfig cfg = h.config();
    std::string s = "OpenBikeControl (OBC)\n";
    s += std::string("devmode: ") + (cfg.obcDevmode ? "ON (advertising as OBC-SB20)\n" : "off\n");
    s += std::string("sink SB20 shifter: ") + (cfg.obcSinkShifter ? "ON\n" : "off\n");
    s += "\nSink the SB20's own shifter buttons -> OBC (the bike add-on; persists + reboots):\n";
    s += "  curl -X POST http://sb20proxy.local/obc/shifter/on\n";
    s += "  curl -X POST http://sb20proxy.local/obc/shifter/off\n";
    s += "\nDevmode: advertise as OBC-SB20 for a listener test (persists + reboots):\n";
    s += "  curl -X POST http://sb20proxy.local/obc/devmode/on\n";
    s += "  curl -X POST http://sb20proxy.local/obc/devmode/off\n";
    s += "\nFire a virtual button press (OBC id, hex or dec; optional &state=, default 1):\n";
    s += "  curl 'http://sb20proxy.local/obc/press?id=0x30'   # ERG Up\n";
    s += "  curl 'http://sb20proxy.local/obc/press?id=0x01'   # Shift Up\n";
    s += "  ids: 0x01 ShiftUp  0x02 ShiftDown  0x30 ErgUp  0x31 ErgDown  0x35 Lap\n";
    s += "\nBind each SB20 button to an action in the web app (http://sb20proxy.local/app),\n";
    s += "or over the API: GET/POST http://sb20proxy.local/obc/buttons.json {enabled,actions[6]}\n";
    return HttpResponse::text(s);
}
inline HttpResponse obcButtonsGet(DeviceHooks& h, const HttpRequest&) {
    const RuntimeConfig cfg = h.config();
    return HttpResponse::json(buttonsToJson(cfg.obcSinkShifter, cfg.obcButtons));
}
inline HttpResponse obcButtonsSet(DeviceHooks& h, const HttpRequest& req) {
    bool enabled = false;
    Sb20ButtonMap m;
    if (!buttonsFromJson(req.body, enabled, m))
        return HttpResponse::json("{\"error\":\"expected {enabled,actions[6]}\"}", 400);
    h.obcButtons(enabled, m);  // persist to NVS + apply live
    return HttpResponse::json(buttonsToJson(enabled, m));
}
inline HttpResponse obcPress(DeviceHooks& h, const HttpRequest& req) {
    if (!req.hasArg("id"))
        return HttpResponse::text("missing ?id= (OBC button id, e.g. 0x30). See /obc\n", 400);
    const long id = strtol(req.arg("id").c_str(), nullptr, 0);  // base 0: 0x30 or 48
    const long st = req.hasArg("state") ? strtol(req.arg("state").c_str(), nullptr, 0) : 1;
    if (id < 0 || id > 255 || st < 0 || st > 255)
        return HttpResponse::text("id/state out of range [0,255]\n", 400);
    h.obcPress((uint8_t)id, (uint8_t)st);
    char msg[64];
    std::snprintf(msg, sizeof(msg), "OBC press id=0x%02lX state=%ld sent\n", id & 0xFF, st);
    return HttpResponse::text(msg);
}

// The four toggles differ only in which flag they set and what they say, so they share
// one body. `devmode/on` additionally sets obcEnabled — Devmode implies the OBC service
// is present. That asymmetry used to be invisible across four near-identical handlers.
inline HttpResponse obcToggle(DeviceHooks& h, bool RuntimeConfig::*flag, bool on,
                              const char* msg, bool alsoEnable = false) {
    RuntimeConfig cfg = h.config();
    cfg.*flag = on;
    if (alsoEnable && on) cfg.obcEnabled = true;
    h.saveConfig(cfg);
    HttpResponse r = HttpResponse::text(msg);
    r.reboot = true;
    return r;
}
inline HttpResponse obcDevmodeOn(DeviceHooks& h, const HttpRequest&) {
    return obcToggle(h, &RuntimeConfig::obcDevmode, true,
                     "OBC Devmode ON - advertising as OBC-SB20, restarting.\n", true);
}
inline HttpResponse obcDevmodeOff(DeviceHooks& h, const HttpRequest&) {
    return obcToggle(h, &RuntimeConfig::obcDevmode, false,
                     "OBC Devmode off - restarting with the normal identity.\n");
}
inline HttpResponse obcShifterOn(DeviceHooks& h, const HttpRequest&) {
    return obcToggle(h, &RuntimeConfig::obcSinkShifter, true,
                     "OBC sink-shifter ON - will read the SB20 buttons, restarting.\n");
}
inline HttpResponse obcShifterOff(DeviceHooks& h, const HttpRequest&) {
    return obcToggle(h, &RuntimeConfig::obcSinkShifter, false,
                     "OBC sink-shifter off - restarting.\n");
}

// -- credentials / ride mode -----------------------------------------------

inline HttpResponse forgetStation(DeviceHooks& h, const HttpRequest&) {
    h.clearCreds();
    HttpResponse r = HttpResponse::text("credentials cleared - rebooting into setup\n");
    r.reboot = true;
    return r;
}
inline HttpResponse forgetPortal(DeviceHooks& h, const HttpRequest&) {
    h.clearCreds();
    HttpResponse r = HttpResponse::text("credentials cleared - restarting\n");
    r.reboot = true;
    return r;
}
inline HttpResponse rideModeConfirm(DeviceHooks&, const HttpRequest&) {
    return HttpResponse::page(rideModeConfirmHtml());
}
inline HttpResponse rideModeGo(DeviceHooks&, const HttpRequest&) {
    HttpResponse r = HttpResponse::page(rideModeDoneHtml());
    r.radioOff = true;  // reply first, then the adapter drops the radio
    return r;
}

// -- setup portal -----------------------------------------------------------

inline HttpResponse portalRender(DeviceHooks& h, const std::string& message) {
    const PortalScan s = h.portalScan();
    return HttpResponse::page(
        renderProvisioningPage(s.networks, message, h.logEnabled() ? 1 : 0, s.scanning));
}
inline HttpResponse portalRoot(DeviceHooks& h, const HttpRequest&) {
    return portalRender(h, std::string());
}
inline HttpResponse portalRescan(DeviceHooks& h, const HttpRequest&) {
    h.startRescan();
    return HttpResponse::redirect(Config::SETUP_PORTAL_URL, 302);
}
inline HttpResponse portalSave(DeviceHooks& h, const HttpRequest& req) {
    // The WebServer parses urlencoded fields itself; fall back to the raw body parser
    // (host-tested in Provisioning.h) when it didn't.
    WifiCredentials c;
    if (req.hasArg("ssid")) {
        c.ssid = req.arg("ssid");
        c.pass = req.arg("pass");
    } else {
        c = parseFormUrlEncoded(req.body);
    }
    if (const char* err = credValidationError(c)) return portalRender(h, err);
    h.saveCreds(c);
    HttpResponse r = HttpResponse::page(renderSavedPage(c.ssid));
    r.reboot = true;
    return r;
}
inline HttpResponse portalRedirect(DeviceHooks&, const HttpRequest&) {
    return HttpResponse::redirect(Config::SETUP_PORTAL_URL, 302);
}

}  // namespace routes

// ---------------------------------------------------------------------------
// The route table
// ---------------------------------------------------------------------------

struct Route {
    const char* path;
    HttpMethod method;
    HttpResponse (*fn)(DeviceHooks&, const HttpRequest&);
};

inline const std::vector<Route>& stationRoutes() {
    static const std::vector<Route> t = {
        {"/", HttpMethod::Get, routes::dashboard},
        {"/ui", HttpMethod::Get, routes::dashboard},
        {"/more", HttpMethod::Get, routes::settings},
        {"/status", HttpMethod::Get, routes::status},
        {"/app", HttpMethod::Get, routes::spa},
        {"/stats", HttpMethod::Get, routes::stats},
        {"/compare", HttpMethod::Get, routes::compare},
        {"/stats/reset", HttpMethod::Get, routes::statsReset},

        {"/forget", HttpMethod::Post, routes::forgetStation},

        {"/setup", HttpMethod::Get, routes::setupPage},
        {"/setup/scan", HttpMethod::Get, routes::setupScan},
        {"/setup/save", HttpMethod::Post, routes::setupSave},
        {"/setup/reset", HttpMethod::Post, routes::setupReset},
        {"/scan", HttpMethod::Get, routes::scanJson},
        {"/config", HttpMethod::Get, routes::configJson},
        {"/config", HttpMethod::Post, routes::configSave},
        {"/curve", HttpMethod::Get, routes::curveGet},
        {"/curve", HttpMethod::Post, routes::curveSet},
        {"/diag", HttpMethod::Get, routes::diag},
        {"/report", HttpMethod::Get, routes::report},

        {"/calibrate", HttpMethod::Get, routes::calibrate},
        {"/calibrate/scan", HttpMethod::Get, routes::calibrateScan},
        {"/calibrate/start", HttpMethod::Post, routes::calibrateStart},
        {"/calibrate/finish", HttpMethod::Post, routes::calibrateFinish},
        {"/calibrate/save", HttpMethod::Post, routes::calibrateSave},
        {"/calibrate/cancel", HttpMethod::Post, routes::calibrateCancel},

        {"/wifi/off", HttpMethod::Get, routes::rideModeConfirm},
        {"/wifi/off", HttpMethod::Post, routes::rideModeGo},

        {"/workout", HttpMethod::Get, routes::workoutPage},
        {"/workout/state", HttpMethod::Get, routes::workoutStateJson},
        {"/workout/load", HttpMethod::Post, routes::workoutLoad},
        {"/workout/preset", HttpMethod::Post, routes::workoutPreset},

        {"/log", HttpMethod::Get, routes::log},
        {"/log/on", HttpMethod::Get, routes::logOn},
        {"/log/off", HttpMethod::Get, routes::logOff},

        {"/obc", HttpMethod::Get, routes::obcHelp},
        {"/obc/buttons.json", HttpMethod::Get, routes::obcButtonsGet},
        {"/obc/buttons.json", HttpMethod::Post, routes::obcButtonsSet},
        {"/obc/press", HttpMethod::Get, routes::obcPress},
        {"/obc/devmode/on", HttpMethod::Post, routes::obcDevmodeOn},
        {"/obc/devmode/off", HttpMethod::Post, routes::obcDevmodeOff},
        {"/obc/shifter/on", HttpMethod::Post, routes::obcShifterOn},
        {"/obc/shifter/off", HttpMethod::Post, routes::obcShifterOff},
    };
    return t;
}

// The five workout control verbs share one hook; kept as a table so the adapter
// registers them the same way it always did.
inline const std::vector<const char*>& workoutVerbs() {
    static const std::vector<const char*> v = {"start", "pause", "resume", "skip", "stop"};
    return v;
}
inline HttpResponse workoutControl(DeviceHooks& h, const std::string& verb) {
    h.workoutControl(verb);
    return HttpResponse::text("ok\n");
}

inline const std::vector<Route>& portalRoutes() {
    static const std::vector<Route> t = {
        {"/", HttpMethod::Get, routes::portalRoot},
        {"/rescan", HttpMethod::Get, routes::portalRescan},
        {"/save", HttpMethod::Post, routes::portalSave},
        {"/forget", HttpMethod::Post, routes::forgetPortal},
        {"/log", HttpMethod::Get, routes::log},
        {"/log/on", HttpMethod::Get, routes::logOn},
        {"/log/off", HttpMethod::Get, routes::logOff},
    };
    return t;
}

// The OS captive-portal probe URLs; every one redirects to the setup page so the
// phone raises its "sign in to network" popup.
inline const std::vector<const char*>& portalProbes() {
    static const std::vector<const char*> p = {"/generate_204",    "/gen_204",
                                               "/hotspot-detect.html", "/ncsi.txt",
                                               "/connecttest.txt", "/redirect"};
    return p;
}

// ---------------------------------------------------------------------------
// Dispatch — the ONLY place a request's outcome is decided.
// ---------------------------------------------------------------------------

// CSRF: the on-device server has no auth, so a malicious page the user opens on the
// same LAN could otherwise POST to us behind their back (2026-06-24 security review,
// Vuln 2). We reject any request whose Origin/Referer authority isn't our own Host.
// Requests with neither header (curl, our own tools) are allowed.
//
// Applied to every POST, structurally — not per route. There is no opt-out to forget.
inline bool requiresCsrfCheck(HttpMethod m) { return m == HttpMethod::Post; }

inline HttpResponse csrfRejection() {
    return HttpResponse::text("cross-site request blocked\n", 403);
}

inline HttpResponse dispatch(const Route& r, DeviceHooks& h, const HttpRequest& req) {
    if (requiresCsrfCheck(r.method) && !isSameOriginRequest(req.host, req.origin, req.referer))
        return csrfRejection();
    return r.fn(h, req);
}

}  // namespace sb20proxy
