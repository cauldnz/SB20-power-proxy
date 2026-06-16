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
        disarmBootGuard();
        startPortal_();
        return;
    }

    startStationServer_();
    display_->showConnected(WiFi.localIP().toString().c_str());
}

void WifiLink::startStationServer_() {
    ArduinoOTA.setHostname(hostname_);
    ArduinoOTA.begin();

    server_ = new WebServer(80);
    server_->on("/", HTTP_GET, [this]() {
        std::string j = provider_ ? renderStatusJson(provider_()) : std::string("{}");
        server_->send(200, "application/json", j.c_str());
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
    server_->on("/forget", HTTP_GET, [this]() {
        WifiCreds::clear();
        server_->send(200, "text/plain", "credentials cleared - rebooting into setup\n");
        delay(400);
        esp_restart();
    });
    server_->begin();
}

void WifiLink::startPortal_() {
    portal_ = true;
    // Waiting for the user to enter creds is a stable state, not a failed flash — make sure
    // the boot-guard can't reboot us out of setup.
    disarmBootGuard();

    WiFi.mode(WIFI_AP_STA);  // AP for the portal; STA enabled so we can scan for networks
    WiFi.softAP(WIFI_AP_SSID);
    IPAddress apIP = WiFi.softAPIP();

    // Best-effort scan so the page can offer a pick-list; an empty list still renders fine.
    networks_.clear();
    int found = WiFi.scanNetworks();
    for (int i = 0; i < found && (int)networks_.size() < 20; ++i) {
        std::string s(WiFi.SSID(i).c_str());
        if (!s.empty()) networks_.push_back(s);
    }

    // Wildcard DNS: every lookup resolves to us, which triggers the OS captive-portal popup.
    dns_ = new DNSServer();
    dns_->start(53, "*", apIP);

    server_ = new WebServer(80);

    auto renderRoot = [this](const std::string& message) {
        std::string body = renderProvisioningPage(networks_, message);
        server_->send(200, "text/html", body.c_str());
    };

    server_->on("/", HTTP_GET, [renderRoot]() { renderRoot(std::string()); });

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
        std::string ok = "<!DOCTYPE html><html><body><h1>Saved</h1><p>Connecting to '" +
                         htmlEscape(c.ssid) + "'. This device will restart now.</p></body></html>";
        server_->send(200, "text/html", ok.c_str());
        delay(500);
        esp_restart();
    });

    server_->on("/forget", HTTP_GET, [this]() {
        WifiCreds::clear();
        server_->send(200, "text/plain", "credentials cleared - restarting\n");
        delay(400);
        esp_restart();
    });

    // OS captive-portal probes -> redirect to the setup page (drives the auto-popup), and a
    // catch-all so any other URL the phone tries lands on setup too.
    auto redirect = [this]() {
        server_->sendHeader("Location", kPortalUrl, true);
        server_->send(302, "text/plain", "");
    };
    const char* probes[] = {"/generate_204", "/gen_204",      "/hotspot-detect.html",
                            "/ncsi.txt",     "/connecttest.txt", "/redirect"};
    for (const char* p : probes) server_->on(p, HTTP_GET, redirect);
    server_->onNotFound(redirect);
    server_->begin();

    display_->showPortal(WIFI_AP_SSID, kPortalUrl);
}

void WifiLink::handle() {
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
