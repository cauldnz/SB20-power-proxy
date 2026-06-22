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
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "Provisioning.h"      // pure page render + form parse + validation (host-tested)
#include "WebApp.h"            // static streaming dashboard served at GET /ui (renders in the phone)
#include "net/DebugLog.h"      // recent-log ring served at GET /log (serial is flaky on the C3)
#include "net/WifiCreds.h"     // NVS-backed credential storage

// wifi_secret.h is now OPTIONAL: NVS (the captive portal) is the source of truth. If the
// file is present it only SEEDS the first boot; without it the build still compiles and the
// device comes up in the setup portal.
#if __has_include("../../wifi_secret.h")
#include "../../wifi_secret.h"
#define HAVE_WIFI_SECRET 1
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
        // Stored creds don't work (moved router, wrong password) — fall back to the portal so
        // the user can re-provision without a USB reflash.
        logf("[wifi] join failed; falling back to setup portal");
        disarmBootGuard();
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

void WifiLink::addForgetRoute_(const char* msg) {
    // GET /forget: wipe stored creds and reboot. `msg` (a string literal) is the only thing the
    // station and portal versions differed by, so both share this installer.
    server_->on("/forget", HTTP_GET, [this, msg]() {
        WifiCreds::clear();
        server_->send(200, "text/plain", msg);
        delay(400);
        esp_restart();
    });
}

void WifiLink::startStationServer_() {
    ArduinoOTA.setHostname(hostname_);
    ArduinoOTA.begin();

    server_ = new WebServer(80);
    // GET / -> the dashboard (what a tester sees opening the board's IP); /ui is kept as an alias.
    auto serveDash = [this]() { server_->send(200, "text/html", appPageHtml()); };
    server_->on("/", HTTP_GET, serveDash);
    server_->on("/ui", HTTP_GET, serveDash);
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
    server_->on("/update", HTTP_GET, [this]() {
        server_->send(200, "text/html",
                      "<form method='POST' action='/update' enctype='multipart/form-data'>"
                      "<input type='file' name='firmware'><input type='submit' value='Flash'></form>");
    });
    server_->on(
        "/update", HTTP_POST,
        [this]() {
            bool ok = !Update.hasError();
            server_->send(200, "text/plain", ok ? "OTA OK - rebooting\n" : "OTA FAILED\n");
            delay(400);
            esp_restart();
        },
        [this]() {
            HTTPUpload& up = server_->upload();
            if (up.status == UPLOAD_FILE_START) {
                Update.begin(UPDATE_SIZE_UNKNOWN);
            } else if (up.status == UPLOAD_FILE_WRITE) {
                Update.write(up.buf, up.currentSize);
            } else if (up.status == UPLOAD_FILE_END) {
                Update.end(true);
            }
        });
    // Re-provision from the station too: forget creds, reboot into the portal.
    addForgetRoute_("credentials cleared - rebooting into setup\n");
    addConfigRoutes_();  // GET /setup picker + POST /setup/save + GET /setup/scan
    addRideModeRoute_();  // GET/POST /wifi/off — turn WiFi off for a BLE-only ride
    addLogRoutes_();
    server_->begin();
}

// Ride mode: turn WiFi off so the C3 is BLE-only for the ride (frees the radio; avoids the rare
// WiFi+BLE+OLED coex freeze). Opt-in + reversible — a power-cycle brings WiFi back. We reply first,
// then power the radio down; handle() then no-ops so nothing touches the dead network.
void WifiLink::addRideModeRoute_() {
    server_->on("/wifi/off", HTTP_GET, [this]() {
        server_->send(200, "text/html", rideModeConfirmHtml());
    });
    server_->on("/wifi/off", HTTP_POST, [this]() {
        server_->send(200, "text/html", rideModeDoneHtml());
        delay(400);            // let the reply flush before the radio drops (mirrors /update)
        ArduinoOTA.end();
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF);
        radioOff_ = true;
        logf("[wifi] ride mode: WiFi off (BLE-only) until power-cycle");
    });
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
        server_->send(200, "text/html",
                      renderConfigPage(cfg, srcs, std::string(), false, -1, status).c_str());
    });
    server_->on("/setup/scan", HTTP_GET, [this]() {  // clear + let the central refill, back to /setup
        if (configScan_) configScan_();
        server_->sendHeader("Location", "/setup");
        server_->send(303, "text/plain", "scanning\n");
    });
    server_->on("/setup/save", HTTP_POST, [this]() {
        const std::string body(server_->arg("plain").c_str());
        RuntimeConfig cfg = parseConfigForm(body);
        const char* err = configValidationError(cfg);
        if (err) {
            const std::vector<SourceCandidate> srcs =
                sourcesProvider_ ? sourcesProvider_() : std::vector<SourceCandidate>{};
            server_->send(200, "text/html", renderConfigPage(cfg, srcs, err).c_str());
            return;
        }
        if (configSave_) configSave_(cfg);  // persist to NVS
        server_->send(200, "text/html", renderConfigSavedPage(cfg).c_str());
        delay(400);
        esp_restart();  // reboot to apply the new source (mirrors /update)
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

void WifiLink::startPortal_() {
    portal_ = true;
    // Waiting for the user to enter creds is a stable state, not a failed flash — make sure
    // the boot-guard can't reboot us out of setup.
    disarmBootGuard();

    WiFi.mode(WIFI_AP_STA);  // AP for the portal; STA enabled so we can scan for networks
    WiFi.softAP(WIFI_AP_SSID);
    IPAddress apIP = WiFi.softAPIP();

    // Best-effort initial scan so the first page already offers a picker. Synchronous (~2-4 s)
    // is fine here — the web server and captive DNS aren't up yet, so nothing is stalled. The
    // Rescan button later uses an async scan so it never blocks the portal.
    populateFromScan_(WiFi.scanNetworks());
    WiFi.scanDelete();

    // Wildcard DNS: every lookup resolves to us, which triggers the OS captive-portal popup.
    dns_ = new DNSServer();
    dns_->start(53, "*", apIP);

    server_ = new WebServer(80);

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
        server_->send(200, "text/html", body.c_str());
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
        server_->send(200, "text/html", ok.c_str());
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

    logf("[wifi] setup portal up: AP '%s' (%d networks scanned)", WIFI_AP_SSID,
         (int)networks_.size());
    display_->showPortal(WIFI_AP_SSID, kPortalUrl);
}

void WifiLink::handle() {
    if (radioOff_) return;  // ride mode: WiFi is down, BLE-only — nothing to service
    if (portal_) {
        if (dns_) dns_->processNextRequest();
        if (server_) server_->handleClient();
        return;
    }
    if (server_) server_->handleClient();
    ArduinoOTA.handle();
    if (!healthy_ && server_ && WiFi.status() == WL_CONNECTED) {
        healthy_ = true;
        disarmBootGuard();
        esp_ota_mark_app_valid_cancel_rollback();  // confirm this image is good
    }
}

#endif  // USE_WIFI
