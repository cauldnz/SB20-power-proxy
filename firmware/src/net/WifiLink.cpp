// WiFi connectivity + observability + OTA + captive-portal provisioning, mirroring
// cauldnz/raedian-probe's failsafe idiom. The ENTIRE body is compiled only when USE_WIFI=1,
// so the default build never pulls in WiFi. Note: WiFi + dual-role NimBLE share the C3 radio
// (coex) — fine for OTA/observability; heavy concurrent use is a later tuning job.
//
// This file is the ARDUINO ADAPTER for the pure route layer in WebRoutes.h. It owns only the
// things that need a radio: joining, the SoftAP + captive DNS, scanning, OTA, and translating
// WebServer <-> HttpRequest/HttpResponse. It makes no routing decisions and contains no page
// or JSON rendering — add or change routes in WebRoutes.h, where they are host-tested.
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

#include <esp_mac.h>       // esp_read_mac — the efuse MAC, valid before any WiFi init
#include "Config.h"        // SETUP_PIN_SECRET (the setup-AP PIN derivation key)
#include "SetupPin.h"      // pure per-device setup-AP PIN derivation + SSID suffix (host-tested)
#include "WebSpa.h"        // the shared SPA (web/index.html) embedded, served at GET /app
#include "net/DebugLog.h"  // recent-log ring served at GET /log (serial is flaky on the C3)
#include "net/WifiCreds.h" // NVS-backed credential storage

// wifi_secret.h is now OPTIONAL: NVS (the captive portal) is the source of truth. If the
// file is present it only SEEDS the first boot; without it the build still compiles and the
// device comes up in the setup portal.
#if __has_include("../../wifi_secret.h")
#include "../../wifi_secret.h"
#define HAVE_WIFI_SECRET 1
#endif

// ota_secret.h is OPTIONAL and gitignored: if present it defines OTA_PASSWORD, which turns ON the
// authenticated ArduinoOTA push path (a dev convenience). Absent => push OTA is DISABLED (fail-closed),
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
#define WIFI_AP_SSID "Setup"  // base for the SoftAP raised for provisioning; per-device suffix added
                             // at runtime (apSsid() -> "Setup-A6E9") so multiple boards don't collide
#endif

// How long to let a reply flush before a reboot or dropping the radio. Measured 2026-07-27: a
// 25-byte JSON reply on the C3 completed in 157 ms, so this is ~2.5x headroom. Multi-KB pages
// don't rely on it — they are written by the drain-aware writer, which returns only once the
// body is out (or the client is gone).
static const uint32_t kFlushDelayMs = 400;

// Recover the POST body for our form routes. The ESP32 WebServer fills arg("plain") with the RAW
// body ONLY when the content type is NOT application/x-www-form-urlencoded — but a real <form> POST
// (and `curl --data`) sends exactly that, in which case the body is parsed into NAMED args and
// arg("plain") is EMPTY. So: use the raw body when present (text/plain, fetch), else rebuild a
// urlencoded body from the parsed named args, re-encoding the (already-decoded) values so the pure
// parsers decode them back correctly.
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
    if (s_bootGuard) return;
    const esp_timer_create_args_t a = {bootGuardCb, nullptr, ESP_TIMER_TASK, "bootguard", true};
    if (esp_timer_create(&a, &s_bootGuard) == ESP_OK)
        esp_timer_start_once(s_bootGuard, (uint64_t)WIFI_HEALTH_DEADLINE_MS * 1000ULL);
}

static void disarmBootGuard() {
    if (!s_bootGuard) return;
    esp_timer_stop(s_bootGuard);
    esp_timer_delete(s_bootGuard);
    s_bootGuard = nullptr;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void WifiLink::begin(const char* hostname, StatusProvider provider,
                     IProvisioningDisplay* display) {
    if (provider) hooks_.status = std::move(provider);
    hostname_ = hostname;
    display_ = display ? display : &s_defaultDisplay;
    logEnabled_ = WifiCreds::logEnabled(/*dflt=*/true);  // persisted; on by default
    wireDeviceHooks_();

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

// The hooks that need Arduino: the log ring, NVS credentials, the portal's scan state, and the
// embedded SPA. Everything else is wired by the public setters from main.
void WifiLink::wireDeviceHooks_() {
    hooks_.logText = [] { return debugLog().text(); };
    hooks_.logEnabled = [this] { return logEnabled_; };
    hooks_.setLogEnabled = [this](bool on) {
        logEnabled_ = on;
        WifiCreds::setLogEnabled(on);
        if (on) logf("[wifi] /log enabled");
    };
    hooks_.clearCreds = [] { WifiCreds::clear(); };
    hooks_.saveCreds = [](const WifiCredentials& c) { WifiCreds::save(c); };
    hooks_.portalScan = [this] {
        PortalScan s;
        // Pick up a finished async rescan (and learn if one is still running) before rendering.
        s.scanning = collectScan_();
        s.networks = networks_;
        return s;
    };
    hooks_.startRescan = [] {
        // Non-blocking: a synchronous in-handler scan would stall the captive DNS.
        if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) WiFi.scanNetworks(/*async=*/true);
    };
    hooks_.spaHtml = [] { return webSpaHtml(); };
}

// ---------------------------------------------------------------------------
// WebServer <-> pure layer
// ---------------------------------------------------------------------------

HttpRequest WifiLink::buildRequest_(HttpMethod m) const {
    HttpRequest r;
    r.method = m;
    r.uri = std::string(server_->uri().c_str());
    r.host = std::string(server_->hostHeader().c_str());
    if (server_->hasHeader("Origin")) r.origin = std::string(server_->header("Origin").c_str());
    if (server_->hasHeader("Referer")) r.referer = std::string(server_->header("Referer").c_str());
    if (m == HttpMethod::Post) r.body = formBody(server_);
    for (int i = 0; i < server_->args(); ++i) {
        const std::string k(server_->argName(i).c_str());
        if (k == "plain") continue;
        r.args.emplace_back(k, std::string(server_->arg(i).c_str()));
    }
    return r;
}

// Drain-aware body write. Arduino's WebServer::send() writes the body with WiFiClient::write and
// IGNORES short writes — under lwIP memory pressure (the no-PSRAM CYD idles ~30 KB free with
// WiFi+BLE+LVGL up) multi-KB pages get silently TRUNCATED mid-stream (2026-07-04). This streams
// the body in small slices and, on a short write, waits for the TCP buffers to drain instead of
// dropping the tail.
void WifiLink::writeStream_(const char* data, size_t len, uint32_t stallMs) {
    server_->setContentLength(len);
    server_->send(200, "text/html", "");  // status + headers only; body streamed below
    WiFiClient c = server_->client();
    size_t off = 0;
    uint32_t lastProgress = millis();
    while (off < len && c.connected()) {
        const size_t want = (len - off > 1024) ? 1024 : (len - off);
        const size_t n = c.write(reinterpret_cast<const uint8_t*>(data) + off, want);
        if (n > 0) {
            off += n;
            lastProgress = millis();
        } else {
            if (millis() - lastProgress > stallMs) break;  // client gone / stuck: give up
            delay(5);  // lwIP send buffers full — let the WiFi task drain them
        }
    }
}

void WifiLink::deliver_(const HttpResponse& r) {
    if (r.staticBody) {
        // Straight from flash: the embedded SPA is ~34 KB and the C3's heap is tight beside BLE,
        // so it must never be materialised as a std::string. 2 s stall budget (a phone that walks
        // out of range shouldn't hold the loop for 5).
        writeStream_(r.staticBody, strlen(r.staticBody), 2000);
    } else if (r.stream) {
        writeStream_(r.body.data(), r.body.size(), 5000);
    } else {
        if (!r.location.empty()) server_->sendHeader("Location", r.location.c_str(), true);
        server_->send(r.status, r.contentType.c_str(), r.body.c_str());
    }

    // Effects the pure layer asked for, applied in one place instead of scattered through the
    // handlers. Both reply FIRST, then act.
    if (r.reboot) {
        delay(kFlushDelayMs);
        esp_restart();
    }
    if (r.radioOff) {
        delay(kFlushDelayMs);
        enterRideMode_();
    }
}

// Ride mode: turn WiFi off so the board is BLE-only for the ride (frees the radio; avoids the rare
// WiFi+BLE+OLED coex freeze). Opt-in + reversible — a power-cycle brings WiFi back.
void WifiLink::enterRideMode_() {
    if (otaEnabled_) ArduinoOTA.end();
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    radioOff_ = true;
    logf("[wifi] ride mode: WiFi off (BLE-only) until power-cycle");
}

// Register a whole table. Every request goes through dispatch(), which is where the CSRF guard
// lives — so a route physically cannot be registered without it.
void WifiLink::installRoutes_(const std::vector<Route>& table) {
    for (const Route& r : table) {
        const Route* route = &r;  // the tables are function-local statics: stable for the run
        server_->on(r.path, r.method == HttpMethod::Get ? HTTP_GET : HTTP_POST, [this, route]() {
            deliver_(dispatch(*route, hooks_, buildRequest_(route->method)));
        });
    }
}

void WifiLink::installWorkoutVerbs_() {
    for (const char* v : workoutVerbs()) {
        const std::string path = std::string("/workout/") + v;
        const std::string verb = v;
        server_->on(path.c_str(), HTTP_POST, [this, verb]() {
            const HttpRequest req = buildRequest_(HttpMethod::Post);
            // Same guard the table routes get; the verbs share one hook rather than one entry.
            if (!isSameOriginRequest(req.host, req.origin, req.referer)) {
                logf("[sec] blocked cross-site %s", req.uri.c_str());
                deliver_(csrfRejection());
                return;
            }
            deliver_(workoutControl(hooks_, verb));
        });
    }
}

// Tell the WebServer to retain the Origin/Referer request headers (it drops all headers by
// default), so the CSRF guard in dispatch() can see them.
void WifiLink::collectCsrfHeaders_() {
    static const char* kHeaders[] = {"Origin", "Referer"};
    server_->collectHeaders(kHeaders, 2);
}

// ---------------------------------------------------------------------------
// Station mode
// ---------------------------------------------------------------------------

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
    logf("[ota] push OTA DISABLED (no ota_secret.h) - flash over USB; networked updates use signed pull");
#endif

    // NOTE: the old unauthenticated `POST /update` firmware-upload form was REMOVED (2026-06-24 security
    // review, Vuln 1) — it let anyone on the LAN, or any website via CSRF, flash arbitrary firmware. There
    // is deliberately no browser-reachable flash route.
    server_ = new WebServer(80);
    collectCsrfHeaders_();
    installRoutes_(stationRoutes());
    installWorkoutVerbs_();
    server_->begin();
}

// ---------------------------------------------------------------------------
// Setup portal
// ---------------------------------------------------------------------------

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

// Per-device setup-AP SSID: WIFI_AP_SSID + a MAC-derived hex suffix (e.g. "Setup-A6E9"), so
// two boards in setup mode at once don't raise identically-named APs that collide on 2.4 GHz.
// Computed once from the efuse MAC (esp_read_mac works before WiFi is initialised) and cached.
const char* WifiLink::apSsid() {
    static const std::string ssid = [] {
        uint8_t mac[6] = {0};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        return setupApSsid(WIFI_AP_SSID, mac, sizeof(mac));
    }();
    return ssid.c_str();
}

void WifiLink::startPortal_() {
    portal_ = true;
    // Waiting for the user to enter creds is a stable state, not a failed flash — make sure
    // the boot-guard can't reboot us out of setup.
    disarmBootGuard();

    WiFi.mode(WIFI_AP_STA);  // AP for the portal; STA enabled so we can scan for networks
    // Halt the STA side before raising the AP. After a failed join (the join-fail portal) the STA
    // keeps auto-reconnecting to the absent stored network in the background, and on the ESP32-C3
    // that thrashes the single shared 2.4 GHz radio: the SoftAP never holds a channel long enough to
    // beacon (the AP is "up" but invisible/unconnectable) and even the picker scan comes back "0
    // networks" (confirmed on hardware 2026-07-11). A fresh onboarding portal has no stored creds so
    // it never thrashes — which is why fresh worked and the join-fail recovery didn't. Keep the radio
    // on (AP + the one-shot scan below still need it) and keep the stored creds (a Save reboots to
    // apply); just stop the background retry.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
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
    // sometimes never starts at all. softAPConfig forces the DHCPS up on the portal /24.
    const IPAddress apAddr(Config::SETUP_AP_IP[0], Config::SETUP_AP_IP[1],
                           Config::SETUP_AP_IP[2], Config::SETUP_AP_IP[3]);
    WiFi.softAPConfig(apAddr, apAddr, IPAddress(255, 255, 255, 0));
    WiFi.softAP(apSsid(), setupPin_.c_str());  // per-device SSID so multiple boards don't collide
    IPAddress apIP = WiFi.softAPIP();

    // Wildcard DNS: every lookup resolves to us, which triggers the OS captive-portal popup.
    dns_ = new DNSServer();
    dns_->start(53, "*", apIP);

    server_ = new WebServer(80);
    collectCsrfHeaders_();
    installRoutes_(portalRoutes());

    // OS captive-portal probes -> redirect to the setup page (drives the auto-popup), and a
    // catch-all so any other URL the phone tries lands on setup too.
    auto redirect = [this]() { deliver_(routes::portalRedirect(hooks_, buildRequest_(HttpMethod::Get))); };
    for (const char* p : portalProbes()) server_->on(p, HTTP_GET, redirect);
    server_->onNotFound(redirect);
    server_->begin();

    logf("[wifi] setup portal up: AP '%s' (WPA2; %d networks scanned)", apSsid(),
         (int)networks_.size());
    display_->showPortal(apSsid(), Config::SETUP_PORTAL_URL, setupPin_.c_str());
}

// ---------------------------------------------------------------------------

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
