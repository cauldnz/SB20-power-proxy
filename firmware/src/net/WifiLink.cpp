// WiFi connectivity + observability + OTA + captive-portal provisioning, mirroring
// cauldnz/raedian-probe's failsafe idiom. The ENTIRE body is compiled only when USE_WIFI=1
// (the esp32c3-ota env), so the default build never pulls in WiFi. Note: WiFi + dual-role
// NimBLE share the C3 radio (coex) — fine for OTA/observability; heavy concurrent use is a
// later tuning job.
#if defined(USE_WIFI) && USE_WIFI

#include "net/WifiLink.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#if __has_include(<esp_coexist.h>)
#include <esp_coexist.h>  // coex preference (classic-ESP32 BLE starves multi-KB HTTP otherwise)
#define HAVE_ESP_COEX 1
#endif

#include "Config.h"            // SETUP_PIN_SECRET (the setup-AP PIN derivation key)
#include "HttpSecurity.h"      // pure same-origin (CSRF) check for state-changing routes (host-tested)
#include "Provisioning.h"      // pure page render + form parse + validation (host-tested)
#include "SetupPin.h"          // pure per-device setup-AP PIN derivation (host-tested)
#include "DiagReport.h"        // pure tester /diag report (config + status + raw meter frames)
#include "WebApp.h"            // static streaming dashboard served at GET /ui (renders in the phone)
#include "WorkoutPresets.h"    // built-in workouts (presetJson) for the /workout/preset route
#include "net/DebugLog.h"      // recent-log ring served at GET /log (serial is flaky on the C3)
#include "net/WifiCreds.h"     // NVS-backed credential storage

// wifi_secret.h is now OPTIONAL: NVS (the captive portal) is the source of truth. If the
// file is present it only SEEDS the first boot; without it the build still compiles and the
// device comes up in the setup portal.
#if __has_include("../../wifi_secret.h")
#include "../../wifi_secret.h"
#define HAVE_WIFI_SECRET 1
#endif

// ota_secret.h is OPTIONAL and gitignored: if present it defines OTA_PASSWORD, which turns ON the
// authenticated ArduinoOTA push path (a dev convenience). Absent ⇒ push OTA is DISABLED (fail-closed),
// the device flashes over USB, and networked updates use the signed-pull path (see
// code/findings/ota-update-plan.md). This replaces the old open /update form + open ArduinoOTA, which
// let anyone on the LAN flash arbitrary firmware (2026-06-24 security review, Vuln 1).
#if __has_include("../../ota_secret.h")
#include "../../ota_secret.h"
#endif

using namespace sb20proxy;

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 15000  // how long begin() blocks trying to join
#endif
#ifndef WIFI_HEALTH_DEADLINE_MS
#define WIFI_HEALTH_DEADLINE_MS 35000  // reset if not healthy (WiFi + HTTP up) within this
#endif
#ifndef WIFI_AP_SSID
#define WIFI_AP_SSID "SB20-Setup"  // the open SoftAP raised for provisioning
#endif

static const char* kPortalUrl = "http://192.168.4.1/";

// Recover the POST body for our form routes. The ESP32 WebServer fills arg("plain") with the RAW
// body ONLY when the content type is NOT application/x-www-form-urlencoded — but a real <form> POST
// (and `curl --data`) sends exactly that, in which case the body is parsed into NAMED args and
// arg("plain") is EMPTY. So: use the raw body when present (text/plain, fetch), else rebuild a
// urlencoded body from the parsed named args, re-encoding the (already-decoded) values so the pure
// parser (parseConfigForm / parseCalibrationForm) decodes them back correctly. (The captive-portal
// save reads named args directly for the same reason — this generalises that fix to every form route.)
static std::string formBody(WebServer* s) {
    const std::string plain(s->arg("plain").c_str());
    if (!plain.empty()) return plain;
    std::string body;
    for (int i = 0; i < s->args(); ++i) {
        const std::string key(s->argName(i).c_str());
        if (key == "plain") continue;
        if (!body.empty()) body += "&";
        body += key + "=" + urlEncode(std::string(s->arg(i).c_str()));
    }
    return body;
}

// Boot-guard: if we never become healthy in time, reset and retry — a bad OTA that can't
// rejoin the network recovers on its own (the raedian-probe failsafe). Armed only on the
// station path; the portal disarms it (see startPortal_).
static esp_timer_handle_t s_bootGuard = nullptr;
static void bootGuardCb(void*) { esp_restart(); }
static SerialProvisioningDisplay s_defaultDisplay;

static void armBootGuard() {
    esp_timer_create_args_t guardArgs = {};
    guardArgs.callback = &bootGuardCb;
    guardArgs.dispatch_method = ESP_TIMER_TASK;
    guardArgs.name = "bootguard";
    esp_timer_create(&guardArgs, &s_bootGuard);
    esp_timer_start_once(s_bootGuard, (uint64_t)WIFI_HEALTH_DEADLINE_MS * 1000);
}

static void disarmBootGuard() {
    if (s_bootGuard) {
        esp_timer_stop(s_bootGuard);
        esp_timer_delete(s_bootGuard);
        s_bootGuard = nullptr;
    }
}

void WifiLink::begin(const char* hostname, StatusProvider provider,
                     IProvisioningDisplay* display) {
    provider_ = provider;
    hostname_ = hostname;
    display_ = display ? display : &s_defaultDisplay;
    logEnabled_ = WifiCreds::logEnabled(/*dflt=*/true);  // persisted; on by default

#ifdef HAVE_ESP_COEX
    // Radio-share preference: favour WiFi over BLE. On the classic ESP32 (CYD) the default
    // balance starves multi-packet TCP while BLE runs — small JSON replies get through, multi-KB
    // pages time out (2026-07-04). Our BLE traffic is 1 Hz meter/crank notifications, which
    // tolerate the reduced airtime; the web UI does not. (setSleep(false) is NOT an option:
    // the classic core hard-aborts when modem sleep is disabled with BT enabled.)
    esp_coex_preference_set(ESP_COEX_PREFER_WIFI);
#endif

    // Credentials: NVS (portal-provisioned) wins; wifi_secret.h, if present, seeds boot one.
    WifiCredentials creds;
    bool have = WifiCreds::load(creds);
#ifdef HAVE_WIFI_SECRET
    if (!have) {
        creds.ssid = WIFI_SSID;
        creds.pass = WIFI_PASS;
        // Ignore the unedited example placeholder so a stale template doesn't block setup.
        have = !creds.ssid.empty() && creds.ssid != std::string("your-2.4GHz-ssid");
    }
#endif

    if (!have) {
        startPortal_();
        return;
    }

    // Arm the boot-guard before the (blocking) join — a bad image that can't rejoin resets.
    armBootGuard();
    logf("[wifi] joining '%s'", creds.ssid.c_str());  // never log the password
    display_->showJoining(creds.ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(hostname_);
    WiFi.begin(creds.ssid.c_str(), creds.pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_CONNECT_TIMEOUT_MS) {
        delay(100);
    }

    if (WiFi.status() != WL_CONNECTED) {
        // Stored creds don't work (moved router, wrong password, or just out of range) — fall
        // back to the portal so the user can re-provision without a USB reflash. Marked as a
        // join-fail portal: main still starts BLE so a ride away from home WiFi isn't bricked.
        logf("[wifi] join failed; falling back to setup portal");
        disarmBootGuard();
        portalJoinFail_ = true;
        startPortal_();
        return;
    }

    startStationServer_();
    logf("[wifi] connected; status at http://%s/", WiFi.localIP().toString().c_str());
    display_->showConnected(WiFi.localIP().toString().c_str());
}

// GET /log -> recent log lines (text/plain) when enabled; /log/on + /log/off flip the toggle
// and persist it to NVS. Shared by both station and portal servers. Logs never carry secrets,
// so this is safe to expose over the open setup AP.
void WifiLink::addLogRoutes_() {
    server_->on("/log", HTTP_GET, [this]() {
        if (!logEnabled_) {
            server_->send(403, "text/plain", "log disabled - enable at /log/on\n");
            return;
        }
        server_->send(200, "text/plain", debugLog().text().c_str());
    });
    server_->on("/log/on", HTTP_GET, [this]() {
        logEnabled_ = true;
        WifiCreds::setLogEnabled(true);
        logf("[wifi] /log enabled");
        server_->send(200, "text/plain", "log enabled\n");
    });
    server_->on("/log/off", HTTP_GET, [this]() {
        logEnabled_ = false;
        WifiCreds::setLogEnabled(false);
        server_->send(200, "text/plain", "log disabled\n");
    });
}

// Drain-aware HTML page send. Arduino's WebServer::send() writes the body with WiFiClient::write
// and IGNORES short writes — under lwIP memory pressure (the no-PSRAM CYD idles ~30 KB free with
// WiFi+BLE+LVGL up) multi-KB pages get silently TRUNCATED mid-stream (2026-07-04). This streams
// the body in small slices and, on a short write, waits for the TCP buffers to drain instead of
// dropping the tail. JSON/plain replies are small enough for plain send().
void WifiLink::sendHtml_(const std::string& body) {
    server_->setContentLength(body.size());
    server_->send(200, "text/html", "");  // status + headers only; body streamed below
    WiFiClient c = server_->client();
    size_t off = 0;
    uint32_t lastProgress = millis();
    while (off < body.size() && c.connected()) {
        const size_t want = body.size() - off > 1024 ? 1024 : body.size() - off;
        const size_t n = c.write(reinterpret_cast<const uint8_t*>(body.data()) + off, want);
        if (n > 0) {
            off += n;
            lastProgress = millis();
        } else {
            if (millis() - lastProgress > 5000) break;  // client gone / stuck: give up
            delay(5);  // lwIP send buffers full — let the WiFi task drain them
        }
    }
}

// CSRF guard for state-changing routes (2026-06-24 security review, Vuln 2). The on-device web server
// has no auth, so a malicious page the user opens on the same LAN could otherwise POST to us behind their
// back (e.g. wipe creds, re-point the source). We reject any request whose Origin/Referer authority isn't
// our own Host; requests with no Origin/Referer (curl, our tools) are allowed (decision logic + tests in
// HttpSecurity.h). Returns true to proceed; on false it has already sent a 403, so the handler must return.
bool WifiLink::csrfOk_() {
    const std::string host(server_->hostHeader().c_str());
    const std::string origin(server_->hasHeader("Origin") ? server_->header("Origin").c_str() : "");
    const std::string referer(server_->hasHeader("Referer") ? server_->header("Referer").c_str() : "");
    if (isSameOriginRequest(host, origin, referer)) return true;
    logf("[sec] blocked cross-site %s (origin='%s' host='%s')", server_->uri().c_str(),
         origin.c_str(), host.c_str());
    server_->send(403, "text/plain", "cross-site request blocked\n");
    return false;
}

// Tell the WebServer to retain the Origin/Referer request headers (it drops all headers by default), so
// csrfOk_() can read them. Called once per server, before begin(). Shared by station + portal.
void WifiLink::collectCsrfHeaders_() {
    static const char* kHeaders[] = {"Origin", "Referer"};
    server_->collectHeaders(kHeaders, 2);
}

void WifiLink::addForgetRoute_(const char* msg) {
    // POST /forget: wipe stored creds and reboot. POST (not GET) + the CSRF guard so a cross-site <img>/
    // form can't wipe a tester's WiFi config (Vuln 2). `msg` is the only thing the station and portal
    // versions differed by, so both share this installer. CLI: curl -X POST http://<ip>/forget
    server_->on("/forget", HTTP_POST, [this, msg]() {
        if (!csrfOk_()) return;
        WifiCreds::clear();
        server_->send(200, "text/plain", msg);
        delay(400);
        esp_restart();
    });
}

void WifiLink::startStationServer_() {
    // Push OTA is OFF unless a build-time OTA_PASSWORD is set (ota_secret.h) — fail-closed. Without it
    // there is NO networked flash listener at all; dev flashes over USB and production updates come from
    // the signed-pull path. With it, ArduinoOTA is authenticated (espota -a / flash.ps1 reads the secret).
#ifdef OTA_PASSWORD
    ArduinoOTA.setHostname(hostname_);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
    otaEnabled_ = true;
    logf("[ota] authenticated push OTA enabled (ArduinoOTA :3232)");
#else
    logf("[ota] push OTA DISABLED (no ota_secret.h) — flash over USB; networked updates use signed pull");
#endif

    server_ = new WebServer(80);
    collectCsrfHeaders_();  // so csrfOk_() can read Origin/Referer on the mutating POST routes
    // GET / -> the dashboard (what a tester sees opening the board's IP); /ui is kept as an alias.
    auto serveDash = [this]() { sendHtml_(appPageHtml()); };
    server_->on("/", HTTP_GET, serveDash);
    server_->on("/ui", HTTP_GET, serveDash);
    // GET /more -> the Settings / "More" tab (status summary + nav hub; fills from /status client-side).
    server_->on("/more", HTTP_GET, [this]() { sendHtml_(settingsPageHtml()); });
    // GET /status -> the status JSON the dashboard polls (was GET /; tools that curled / should
    // use /status now). Kept compact + unchanged in shape.
    server_->on("/status", HTTP_GET, [this]() {
        std::string j = provider_ ? renderStatusJson(provider_()) : std::string("{}");
        server_->send(200, "application/json", j.c_str());
    });
    // Perf observability (Phase A): loop timing, heap/frag, stack, idle, reboot evidence.
    server_->on("/stats", HTTP_GET, [this]() {
        std::string j = perfProvider_ ? perfProvider_() : std::string("{}");
        server_->send(200, "application/json", j.c_str());
    });
    server_->on("/stats/reset", HTTP_GET, [this]() {  // zero the window (perf_soak calls this)
        if (perfReset_) perfReset_();
        server_->send(200, "text/plain", "perf window reset\n");
    });
    // NOTE: the old unauthenticated `POST /update` firmware-upload form was REMOVED (2026-06-24 security
    // review, Vuln 1) — it let anyone on the LAN, or any website via CSRF, flash arbitrary firmware. There
    // is deliberately no browser-reachable flash route. Networked updates come from the signed-pull path
    // (code/findings/ota-update-plan.md); dev push uses authenticated ArduinoOTA above (USB otherwise).
    // Re-provision from the station too: forget creds, reboot into the portal.
    addForgetRoute_("credentials cleared - rebooting into setup\n");
    addConfigRoutes_();  // GET /setup picker + POST /setup/save + GET /setup/scan
    addCalibrationRoutes_();  // GET /calibrate + POST start/finish/save/cancel — the meter-to-meter wizard
    addRideModeRoute_();  // GET/POST /wifi/off — turn WiFi off for a BLE-only ride
    addWorkoutRoutes_();  // GET /workout (+ /state) + POST /workout/{load,preset,controls}
    addLogRoutes_();
    server_->begin();
}

// Ride mode: turn WiFi off so the C3 is BLE-only for the ride (frees the radio; avoids the rare
// WiFi+BLE+OLED coex freeze). Opt-in + reversible — a power-cycle brings WiFi back. We reply first,
// then power the radio down; handle() then no-ops so nothing touches the dead network.
void WifiLink::addRideModeRoute_() {
    server_->on("/wifi/off", HTTP_GET, [this]() {
        sendHtml_(rideModeConfirmHtml());
    });
    server_->on("/wifi/off", HTTP_POST, [this]() {
        if (!csrfOk_()) return;
        sendHtml_(rideModeDoneHtml());
        delay(400);            // let the reply flush before the radio drops
        if (otaEnabled_) ArduinoOTA.end();
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF);
        radioOff_ = true;
        logf("[wifi] ride mode: WiFi off (BLE-only) until power-cycle");
    });
}

// The Workout screen: GET /workout serves the page (pure workoutPageHtml), GET /workout/state the
// live cursor JSON the page polls, and the POSTs drive the engine via hooks. Loading is live (no
// reboot) — a workout is data, not identity. Presets are resolved here (WorkoutPresets.h) and fed
// through the same load hook as a pasted workout.
void WifiLink::addWorkoutRoutes_() {
    server_->on("/workout", HTTP_GET, [this]() {
        sendHtml_(workoutPageHtml());
    });
    server_->on("/workout/state", HTTP_GET, [this]() {
        const std::string j = workoutState_ ? workoutState_() : std::string("{\"loaded\":false}");
        server_->send(200, "application/json", j.c_str());
    });
    server_->on("/workout/load", HTTP_POST, [this]() {
        if (!csrfOk_()) return;
        const bool ok = workoutLoad_ && workoutLoad_(formBody(server_));
        server_->send(ok ? 200 : 400, "text/plain", ok ? "loaded\n" : "bad workout\n");
    });
    server_->on("/workout/preset", HTTP_POST, [this]() {
        if (!csrfOk_()) return;
        const std::string j = presetJson(std::string(server_->arg("key").c_str()));
        const bool ok = !j.empty() && workoutLoad_ && workoutLoad_(j);
        server_->send(ok ? 200 : 400, "text/plain", ok ? "loaded\n" : "unknown preset\n");
    });
    // The control verbs all share one hook; register them from a small table.
    static const char* kVerbs[] = {"start", "pause", "resume", "skip", "stop"};
    for (const char* v : kVerbs) {
        const std::string path = std::string("/workout/") + v;
        const std::string verb = v;
        server_->on(path.c_str(), HTTP_POST, [this, verb]() {
            if (!csrfOk_()) return;
            if (workoutControl_) workoutControl_(verb);
            server_->send(200, "text/plain", "ok\n");
        });
    }
}

// The source-setup UI: pick which power meter / surviving crank the proxy reads, over WiFi. The
// page + form parse + validation are the pure, host-tested ConfigPage.h; here we just wire the
// hooks (current config, discovered sources, persist, rescan) and reboot on save to apply it.
void WifiLink::addConfigRoutes_() {
    server_->on("/setup", HTTP_GET, [this]() {
        const RuntimeConfig cfg = configProvider_ ? configProvider_() : RuntimeConfig::defaults();
        const std::vector<SourceCandidate> srcs = sourcesProvider_ ? sourcesProvider_()
                                                                    : std::vector<SourceCandidate>{};
        // Live status banner so the tester can verify the source is connected before riding.
        std::string status;
        if (provider_) {
            const ProxyStatus st = provider_();
            if (st.mock) status = "Running a simulated meter (test build).";
            else if (st.sourceConnected)
                status = "Reading " + (st.srcName.empty() ? std::string("your source") : st.srcName) +
                         " \xE2\x9C\x93";  // checkmark
            else status = "Searching for your source\xE2\x80\xA6";  // ellipsis
        }
        sendHtml_(renderConfigPage(cfg, srcs, std::string(), false, -1, status));
    });
    server_->on("/setup/scan", HTTP_GET, [this]() {  // clear + let the central refill, back to /setup
        if (configScan_) configScan_();
        server_->sendHeader("Location", "/setup");
        server_->send(303, "text/plain", "scanning\n");
    });
    server_->on("/setup/save", HTTP_POST, [this]() {
        if (!csrfOk_()) return;
        const std::string body = formBody(server_);
        RuntimeConfig cfg = parseConfigForm(body);
        // A body WITHOUT the trainer field (an old cached page, or a curl that predates it) must
        // PRESERVE the stored trainer; present-but-empty is the explicit "erg off" clear.
        if (!formHasField(body, "trainer") && configProvider_) {
            cfg.trainerNameFilter = configProvider_().trainerNameFilter;
        }
        const char* err = configValidationError(cfg);
        if (err) {
            const std::vector<SourceCandidate> srcs =
                sourcesProvider_ ? sourcesProvider_() : std::vector<SourceCandidate>{};
            sendHtml_(renderConfigPage(cfg, srcs, err));
            return;
        }
        if (configSave_) configSave_(cfg);  // persist to NVS
        sendHtml_(renderConfigSavedPage(cfg));
        delay(400);
        esp_restart();  // reboot to apply the new source (mirrors /update)
    });
    // POST /setup/reset -> clear the saved source + identity back to the shipped defaults (recovery
    // for a tester who mis-picked). Persisting defaults() == clearing: next boot loads the defaults.
    server_->on("/setup/reset", HTTP_POST, [this]() {
        if (!csrfOk_()) return;
        if (configSave_) configSave_(RuntimeConfig::defaults());
        server_->send(200, "text/html",
                      "<!DOCTYPE html><meta charset='utf-8'><meta name='viewport' "
                      "content='width=device-width,initial-scale=1'><body style='font-family:"
                      "system-ui,sans-serif;max-width:480px;margin:0 auto;padding:16px'>"
                      "<h1>Reset &#10003;</h1><p>Source and crank identity restored to defaults &mdash; "
                      "restarting. Open <a href='/'>the dashboard</a> in a moment to set up again.</p>");
        delay(400);
        esp_restart();
    });
    // GET /diag -> the plain-text tester report (config + status + raw meter frames). A tester saves
    // it and sends it when their meter isn't recognised, so we add support offline (real-data-first).
    server_->on("/diag", HTTP_GET, [this]() {
        const RuntimeConfig cfg = configProvider_ ? configProvider_() : RuntimeConfig::defaults();
        const ProxyStatus st = provider_ ? provider_() : ProxyStatus{};
        const std::vector<std::string> frames = diagFrames_ ? diagFrames_() : std::vector<std::string>{};
        server_->send(200, "text/plain", renderDiagReport(cfg, st, frames).c_str());
    });
    // GET /report -> the tester-facing "review & send" page. Consent-first: it fetches /diag, shows it
    // for review, and offers Download / Copy / Email — nothing leaves the device until the tester acts.
    server_->on("/report", HTTP_GET, [this]() {
        sendHtml_(diagReportPageHtml());
    });
}

// The meter-to-meter calibration wizard (GET /calibrate + the POST actions). Pure render/parse live
// in CalibrationPage.h; here we route + bridge to the BLE/session hooks. start/save/cancel persist
// then REBOOT (the wizard moves in/out of a dedicated calibration boot — see main); finish fits in
// place (no reboot) so the rider can review before saving.
void WifiLink::addCalibrationRoutes_() {
    auto render = [this](const std::string& message) {
        CalWizardView v = calView_ ? calView_() : CalWizardView{};
        if (!message.empty()) v.message = message;
        sendHtml_(renderCalibrationPage(v));
    };
    server_->on("/calibrate", HTTP_GET, [this, render]() { render(""); });
    server_->on("/calibrate/scan", HTTP_GET, [this]() {
        if (calScan_) calScan_();
        server_->sendHeader("Location", "/calibrate");
        server_->send(303, "text/plain", "scanning\n");
    });
    server_->on("/calibrate/start", HTTP_POST, [this, render]() {
        if (!csrfOk_()) return;
        const CalForm f = parseCalibrationForm(formBody(server_));
        const char* err = calibrationStartError(f);
        if (err) { render(err); return; }
        if (calStart_ && !calStart_(f.dutAddr, f.refAddr)) {  // rejected (already calibrating)
            render("A calibration is already in progress \xE2\x80\x94 reopen the wizard to continue it.");
            return;
        }
        server_->send(200, "text/html",
                      "<!DOCTYPE html><meta charset='utf-8'><meta name='viewport' "
                      "content='width=device-width,initial-scale=1'><body style='font-family:"
                      "system-ui,sans-serif;max-width:480px;margin:0 auto;padding:16px'>"
                      "<h1>Starting calibration&hellip;</h1><p>Connecting to both meters &mdash; the "
                      "device is restarting. Reopen <a href='/calibrate'>the wizard</a> in a moment "
                      "and start your power sweep.</p>");
        delay(400);
        esp_restart();
    });
    server_->on("/calibrate/finish", HTTP_POST, [this, render]() {
        if (!csrfOk_()) return;
        if (calFinish_) calFinish_();  // fit (or no-op if too few pairs); the view shows the result
        server_->sendHeader("Location", "/calibrate");
        server_->send(303, "text/plain", "fitting\n");
    });
    server_->on("/calibrate/save", HTTP_POST, [this, render]() {
        if (!csrfOk_()) return;
        const CalForm f = parseCalibrationForm(formBody(server_));
        if (calSave_ && !calSave_(f.deviceName)) {  // rejected (not fitted yet) — don't reboot
            render("Finish the calibration first \xE2\x80\x94 there's no fitted correction to save yet.");
            return;
        }
        server_->send(200, "text/html",
                      "<!DOCTYPE html><meta charset='utf-8'><meta name='viewport' "
                      "content='width=device-width,initial-scale=1'><body style='font-family:"
                      "system-ui,sans-serif;max-width:480px;margin:0 auto;padding:16px'>"
                      "<h1>Saved &#10003;</h1><p>Switching to corrector mode &mdash; restarting. Now "
                      "remove the reference meter; the corrected meter is rebroadcast under your "
                      "chosen name. Open <a href='/'>the dashboard</a> in a moment.</p>");
        delay(400);
        esp_restart();
    });
    server_->on("/calibrate/cancel", HTTP_POST, [this]() {
        if (!csrfOk_()) return;
        if (calCancel_) calCancel_();  // clear the calibration marker
        server_->send(200, "text/html",
                      "<!DOCTYPE html><meta charset='utf-8'><meta name='viewport' "
                      "content='width=device-width,initial-scale=1'><body style='font-family:"
                      "system-ui,sans-serif;max-width:480px;margin:0 auto;padding:16px'>"
                      "<h1>Calibration cancelled</h1><p>Restarting. Open <a href='/'>the dashboard</a> "
                      "in a moment.</p>");
        delay(400);
        esp_restart();
    });
}

// Turn a finished WiFi scan (n entries; n<0 = scan failed) into the portal's picker model: skip
// hidden SSIDs, record signal (RSSI) and whether the AP is secured. Dedup/sort happen in the
// host-tested renderer, so this stays a thin radio-read.
void WifiLink::populateFromScan_(int n) {
    networks_.clear();
    for (int i = 0; i < n && (int)networks_.size() < 20; ++i) {
        std::string ssid(WiFi.SSID(i).c_str());
        if (ssid.empty()) continue;  // hidden network — nothing to tap
        ScannedNet net;
        net.ssid = std::move(ssid);
        net.rssi = WiFi.RSSI(i);
        net.secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        networks_.push_back(std::move(net));
    }
}

// Harvest a completed async rescan into networks_ (freeing the radio's copy). Returns true while
// a scan is still running, so the page can show "Scanning..." and auto-refresh until it lands.
bool WifiLink::collectScan_() {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return true;  // -1: keep the current list, keep polling
    if (n >= 0) {                             // results ready
        populateFromScan_(n);
        WiFi.scanDelete();
    }
    return false;  // idle or failed (-2): nothing in flight
}

const char* WifiLink::apSsid() { return WIFI_AP_SSID; }

void WifiLink::startPortal_() {
    portal_ = true;
    // Waiting for the user to enter creds is a stable state, not a failed flash — make sure
    // the boot-guard can't reboot us out of setup.
    disarmBootGuard();

    WiFi.mode(WIFI_AP_STA);  // AP for the portal; STA enabled so we can scan for networks
    // The setup AP is WPA2-protected (closes the cleartext-PSK window when the user types their home
    // WiFi password into the portal). OLED builds use a per-device 8-digit PIN shown on the screen;
    // screenless builds fall back to a known default passphrase the user can type (Config).
#if defined(USE_OLED) && USE_OLED
    uint8_t mac[6];
    WiFi.macAddress(mac);
    setupPin_ = deriveSetupPin(mac, sizeof(mac), Config::SETUP_PIN_SECRET);
#else
    setupPin_ = Config::SETUP_AP_DEFAULT_PASSWORD;
#endif
    // Best-effort initial scan so the first page already offers a picker — run it BEFORE the AP
    // comes up: on the classic (Arduino 2.x) ESP32 core a synchronous STA scan right after
    // softAP() can wedge the AP's DHCP server (client associates but self-assigns 169.254.x.x —
    // CYD, 2026-07-04). Synchronous (~2-4 s) is fine here — nothing else is up yet. The Rescan
    // button later uses an async scan so it never blocks the portal.
    populateFromScan_(WiFi.scanNetworks());
    WiFi.scanDelete();

    // Explicit AP netif config: with the implicit defaults the classic core's DHCP server
    // sometimes never starts at all. softAPConfig forces the DHCPS up on 192.168.4.1/24.
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                      IPAddress(255, 255, 255, 0));
    WiFi.softAP(WIFI_AP_SSID, setupPin_.c_str());
    IPAddress apIP = WiFi.softAPIP();

    // Wildcard DNS: every lookup resolves to us, which triggers the OS captive-portal popup.
    dns_ = new DNSServer();
    dns_->start(53, "*", apIP);

    server_ = new WebServer(80);
    collectCsrfHeaders_();  // retain Origin/Referer so the /save + /forget POSTs are CSRF-guarded

    // 302 back to the setup page; reused by the OS captive-portal probes, the catch-all, and the
    // Rescan button below.
    auto redirect = [this]() {
        server_->sendHeader("Location", kPortalUrl, true);
        server_->send(302, "text/plain", "");
    };

    auto renderRoot = [this](const std::string& message) {
        // Pick up a finished async rescan (and learn if one is still running) before rendering.
        bool scanning = collectScan_();
        std::string body =
            renderProvisioningPage(networks_, message, logEnabled_ ? 1 : 0, scanning);
        sendHtml_(body);
    };

    server_->on("/", HTTP_GET, [renderRoot]() { renderRoot(std::string()); });

    // Kick off a non-blocking rescan and bounce back to '/', which shows "Scanning..." and
    // auto-refreshes until collectScan_ harvests the results (avoids stalling the captive DNS the
    // way a synchronous in-handler scan would).
    server_->on("/rescan", HTTP_GET, [this, redirect]() {
        if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) WiFi.scanNetworks(/*async=*/true);
        redirect();
    });

    server_->on("/save", HTTP_POST, [this, renderRoot]() {
        if (!csrfOk_()) return;
        // WebServer parses urlencoded form fields itself; fall back to the raw body parser
        // (host-tested in Provisioning.h) if it didn't.
        WifiCredentials c;
        if (server_->hasArg("ssid")) {
            c.ssid = std::string(server_->arg("ssid").c_str());
            c.pass = std::string(server_->arg("pass").c_str());
        } else {
            c = parseFormUrlEncoded(std::string(server_->arg("plain").c_str()));
        }
        if (const char* err = credValidationError(c)) {
            renderRoot(err);
            return;
        }
        WifiCreds::save(c);
        std::string ok = renderSavedPage(c.ssid);
        sendHtml_(ok);
        delay(500);
        esp_restart();
    });

    addForgetRoute_("credentials cleared - restarting\n");

    // OS captive-portal probes -> redirect to the setup page (drives the auto-popup), and a
    // catch-all so any other URL the phone tries lands on setup too.
    const char* probes[] = {"/generate_204", "/gen_204",      "/hotspot-detect.html",
                            "/ncsi.txt",     "/connecttest.txt", "/redirect"};
    for (const char* p : probes) server_->on(p, HTTP_GET, redirect);
    addLogRoutes_();
    server_->onNotFound(redirect);
    server_->begin();

    logf("[wifi] setup portal up: AP '%s' (WPA2; %d networks scanned)", WIFI_AP_SSID,
         (int)networks_.size());
    display_->showPortal(WIFI_AP_SSID, kPortalUrl, setupPin_.c_str());
}

void WifiLink::handle() {
    if (radioOff_) return;  // ride mode: WiFi is down, BLE-only — nothing to service
    if (portal_) {
        if (dns_) dns_->processNextRequest();
        if (server_) server_->handleClient();
        return;
    }
    if (server_) server_->handleClient();
    if (otaEnabled_) ArduinoOTA.handle();
    if (!healthy_ && server_ && WiFi.status() == WL_CONNECTED) {
        healthy_ = true;
        disarmBootGuard();
        esp_ota_mark_app_valid_cancel_rollback();  // confirm this image is good
    }
}

#endif  // USE_WIFI
