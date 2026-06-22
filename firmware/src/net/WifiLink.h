#pragma once
#include <functional>
#include <string>
#include <vector>

#include "ConfigPage.h"               // pure source-config page (RuntimeConfig + SourceCandidate + render)
#include "Provisioning.h"             // pure ScannedNet + page render/parse/validate (host-tested)
#include "Status.h"                   // pure ProxyStatus + renderStatusJson (host-tested)
#include "net/ProvisioningDisplay.h"  // injectable setup-UX seam (Serial default)

class WebServer;  // ESP32 Arduino (global namespace); kept out of the header
class DNSServer;

namespace sb20proxy {

// WiFi connectivity + observability + OTA for the proxy, mirroring the raedian-probe failsafe
// idiom. Credentials are provisioned at runtime via a captive portal (no rebuild needed) and
// stored in NVS (WifiCreds); a compile-time wifi_secret.h, if present, only seeds the first
// boot. Two modes:
//   * STATION  — creds known & join succeeds: serves status JSON at GET /, an OTA upload form
//                at /update, and self-resets if it never becomes healthy (boot-guard).
//   * PORTAL   — no creds, or the join failed: raises a SoftAP + captive DNS and serves the
//                setup page so the user can pick a network. The boot-guard is disarmed here —
//                waiting for the user is a stable state, not a failed flash.
// Compiled only when USE_WIFI=1 (the esp32c3-ota env); the default build leaves it out.
// Arduino-only — the page/parse/validation logic it serves (Provisioning.h) is host-tested.
class WifiLink {
public:
    using StatusProvider = std::function<ProxyStatus()>;
    using PerfProvider = std::function<std::string()>;  // returns the GET /stats JSON
    using PerfResetHook = std::function<void()>;         // zero the perf window (GET /stats/reset)

    // Join WiFi (or raise the setup portal), start OTA + the HTTP server. `provider` renders
    // the live status JSON per request. `display` (optional) reports setup state; defaults to
    // logging over Serial.
    void begin(const char* hostname, StatusProvider provider,
               IProvisioningDisplay* display = nullptr);

    // Wire the perf observability endpoints (GET /stats + /stats/reset). Optional; call after
    // begin(). The handlers read these at request time, so ordering with begin() doesn't matter.
    void setPerf(PerfProvider stats, PerfResetHook reset) {
        perfProvider_ = stats;
        perfReset_ = reset;
    }

    // Wire the source-setup UI (GET /setup picker, POST /setup/save, GET /setup/scan). Optional;
    // call after begin(). Kept decoupled from the BLE layer via hooks: `cfg` returns the stored
    // RuntimeConfig, `sources` the discovered candidates, `save` persists a new config (WifiLink
    // then reboots to apply it), `scan` clears + refreshes the candidate list.
    using ConfigProvider = std::function<RuntimeConfig()>;
    using SourcesProvider = std::function<std::vector<SourceCandidate>()>;
    using ConfigSaveHook = std::function<void(const RuntimeConfig&)>;
    using ScanHook = std::function<void()>;
    void setConfigUi(ConfigProvider cfg, SourcesProvider sources, ConfigSaveHook save, ScanHook scan) {
        configProvider_ = cfg;
        sourcesProvider_ = sources;
        configSave_ = save;
        configScan_ = scan;
    }

    // Call from loop(): services HTTP + OTA (station) or the captive DNS + portal (setup), and
    // promotes to healthy (which cancels the boot-guard and validates the running OTA image).
    void handle();

    bool isUp() const { return healthy_; }
    bool inPortal() const { return portal_; }

private:
    void startStationServer_();  // OTA + status/update/forget routes (assumes WiFi joined)
    void addConfigRoutes_();     // GET /setup picker + POST /setup/save + GET /setup/scan
    void addRideModeRoute_();    // GET /wifi/off (confirm) + POST /wifi/off (radio down for riding)
    void startPortal_();         // SoftAP + captive DNS + setup routes
    void addLogRoutes_();        // GET /log + /log/on + /log/off (shared by both modes)
    void addForgetRoute_(const char* msg);  // GET /forget: clear creds + reboot (shared by both modes)
    void populateFromScan_(int n);  // fill networks_ from a finished WiFi scan (Arduino WiFi.*)
    bool collectScan_();            // harvest a completed async scan; true if one is still running

    WebServer* server_ = nullptr;
    DNSServer* dns_ = nullptr;
    StatusProvider provider_;
    PerfProvider perfProvider_;
    PerfResetHook perfReset_;
    ConfigProvider configProvider_;
    SourcesProvider sourcesProvider_;
    ConfigSaveHook configSave_;
    ScanHook configScan_;
    IProvisioningDisplay* display_ = nullptr;
    const char* hostname_ = "sb20proxy";
    std::vector<ScannedNet> networks_;  // APs scanned for the portal picker (RSSI + secured)
    bool healthy_ = false;
    bool portal_ = false;
    bool radioOff_ = false;  // ride mode: WiFi powered down (BLE-only); handle() then no-ops
    bool logEnabled_ = true;  // serve GET /log? (persisted in NVS via WifiCreds)
};

}  // namespace sb20proxy
