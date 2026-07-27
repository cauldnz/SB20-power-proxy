#pragma once
#include <functional>
#include <string>
#include <vector>

#include "WebRoutes.h"                // pure request -> response layer + the route tables
#include "net/ProvisioningDisplay.h"  // injectable setup-UX seam (Serial default)

class WebServer;  // ESP32 Arduino (global namespace); kept out of the header
class DNSServer;

namespace sb20proxy {

// WiFi connectivity + observability + OTA for the proxy, mirroring the raedian-probe failsafe
// idiom. Credentials are provisioned at runtime via a captive portal (no rebuild needed) and
// stored in NVS (WifiCreds); a compile-time wifi_secret.h, if present, only seeds the first
// boot. Two modes:
//   * STATION  — creds known & join succeeds: serves the dashboard + JSON API, and self-resets if
//                it never becomes healthy (boot-guard). There is deliberately NO browser-reachable
//                flash route (the open /update form was removed, 2026-06-24 security review); push
//                OTA is authenticated ArduinoOTA, enabled only when OTA_PASSWORD is set (fail-closed),
//                and networked updates use the signed-pull path (code/findings/ota-update-plan.md).
//   * PORTAL   — no creds, or the join failed: raises a SoftAP + captive DNS and serves the
//                setup page so the user can pick a network. The boot-guard is disarmed here —
//                waiting for the user is a stable state, not a failed flash.
//
// THIS CLASS IS AN ADAPTER, NOT A ROUTER. Every routing decision — which URL maps to which
// behaviour, what it returns, whether it is CSRF-guarded, whether it reboots — lives in the pure,
// host-tested WebRoutes.h. What remains here is the Arduino half that cannot be tested off-device:
// joining WiFi, raising the SoftAP, scanning, and translating WebServer <-> HttpRequest/Response.
// If you are adding or changing a route, you almost certainly want WebRoutes.h instead.
//
// Compiled only when USE_WIFI=1; the default build leaves it out.
class WifiLink {
public:
    // Kept as public aliases so existing call sites (main.cpp) compile unchanged; each setter
    // simply stores into the DeviceHooks the pure routes read.
    using StatusProvider = std::function<ProxyStatus()>;
    using PerfProvider = std::function<std::string()>;
    using PerfResetHook = std::function<void()>;
    using CompareProvider = std::function<std::string()>;
    using ConfigProvider = std::function<RuntimeConfig()>;
    using SourcesProvider = std::function<std::vector<SourceCandidate>()>;
    using ConfigSaveHook = std::function<void(const RuntimeConfig&)>;
    using ScanHook = std::function<void()>;
    using DiagFramesProvider = std::function<std::vector<std::string>()>;
    using CalViewProvider = std::function<CalWizardView()>;
    using CalStartHook = std::function<bool(const std::string& dutAddr, const std::string& refAddr)>;
    using CalFinishHook = std::function<bool()>;
    using CalSaveHook = std::function<bool(const std::string& deviceName)>;
    using CalSimpleHook = std::function<void()>;
    using WorkoutStateProvider = std::function<std::string()>;
    using WorkoutLoadHook = std::function<bool(const std::string& json)>;
    using WorkoutControlHook = std::function<void(const std::string& action)>;
    using CurveSetHook = std::function<void(const CorrectionCurve&)>;
    using ObcPressHook = std::function<void(uint8_t id, uint8_t state)>;
    using ObcButtonsHook = std::function<void(bool enabled, const Sb20ButtonMap&)>;

    // Join WiFi (or raise the setup portal), start OTA + the HTTP server. `provider` renders
    // the live status per request. `display` (optional) reports setup state; defaults to Serial.
    void begin(const char* hostname, StatusProvider provider,
               IProvisioningDisplay* display = nullptr);

    // The hooks the routes reach through. All optional and order-independent: the handlers read
    // them at request time, and any left unset keeps the documented default in DeviceHooks.
    void setPerf(PerfProvider stats, PerfResetHook reset) {
        hooks_.perfJson = std::move(stats);
        hooks_.perfReset = std::move(reset);
    }
    void setCompare(CompareProvider p) { hooks_.compareJson = std::move(p); }
    void setConfigUi(ConfigProvider cfg, SourcesProvider sources, ConfigSaveHook save, ScanHook scan) {
        hooks_.config = std::move(cfg);
        hooks_.sources = std::move(sources);
        hooks_.saveConfig = std::move(save);
        hooks_.rescanSources = std::move(scan);
    }
    void setDiagFrames(DiagFramesProvider frames) { hooks_.diagFrames = std::move(frames); }
    void setCalibrationUi(CalViewProvider view, CalStartHook start, CalFinishHook finish,
                          CalSaveHook save, CalSimpleHook cancel, CalSimpleHook scan) {
        hooks_.calView = std::move(view);
        hooks_.calStart = std::move(start);
        hooks_.calFinish = std::move(finish);
        hooks_.calSave = std::move(save);
        hooks_.calCancel = std::move(cancel);
        hooks_.calScan = std::move(scan);
    }
    void setWorkoutUi(WorkoutStateProvider state, WorkoutLoadHook load, WorkoutControlHook control) {
        hooks_.workoutState = std::move(state);
        hooks_.workoutLoad = std::move(load);
        hooks_.workoutControl = std::move(control);
    }
    void setCurveHandler(CurveSetHook set) { hooks_.setCurve = std::move(set); }
    void setObcPressHook(ObcPressHook h) { hooks_.obcPress = std::move(h); }
    void setObcButtonsHook(ObcButtonsHook h) { hooks_.obcButtons = std::move(h); }

    // Call from loop(): services HTTP + OTA (station) or the captive DNS + portal (setup), and
    // promotes to healthy (which cancels the boot-guard and validates the running OTA image).
    void handle();

    bool isUp() const { return healthy_; }
    bool inPortal() const { return portal_; }
    // True when the portal is a JOIN-FAILURE fallback (stored creds stopped working) rather than
    // first-time onboarding — the caller should still start BLE so a ride isn't bricked by a
    // missing home network; only the fresh-onboarding portal holds BLE off (classic-ESP32 coex).
    bool portalAfterJoinFail() const { return portalJoinFail_; }
    // The setup AP's WPA2 password while the portal is up (a per-device PIN on OLED builds, else the
    // default) — surfaced so the OLED can display it. Empty until the portal starts.
    const std::string& setupPin() const { return setupPin_; }
    // The setup AP's SSID (constant) — surfaced so the LCD onboarding screen can render the
    // join-this-network QR without duplicating the name.
    static const char* apSsid();

private:
    void wireDeviceHooks_();  // install the Arduino-side hooks (log, creds, scan, embedded SPA)
    void installRoutes_(const std::vector<Route>& table);  // register a table through dispatch()
    void installWorkoutVerbs_();                           // the five shared control verbs
    HttpRequest buildRequest_(HttpMethod m) const;         // WebServer -> pure request
    void deliver_(const HttpResponse& r);                  // pure response -> WebServer (+ effects)
    void writeStream_(const char* data, size_t len, uint32_t stallMs);  // drain-aware body write

    void startStationServer_();  // dashboard/API/setup/calibrate routes + opt-in auth OTA
    void startPortal_();         // SoftAP + captive DNS + setup routes
    void collectCsrfHeaders_();  // retain Origin/Referer so the CSRF guard can read them
    void populateFromScan_(int n);  // fill networks_ from a finished WiFi scan (Arduino WiFi.*)
    bool collectScan_();            // harvest a completed async scan; true if one is still running
    void enterRideMode_();          // power the radio down after a /wifi/off reply

    WebServer* server_ = nullptr;
    DNSServer* dns_ = nullptr;
    DeviceHooks hooks_;
    IProvisioningDisplay* display_ = nullptr;
    const char* hostname_ = "sb20proxy";
    std::vector<ScannedNet> networks_;  // APs scanned for the portal picker (RSSI + secured)
    bool healthy_ = false;
    bool portal_ = false;
    bool portalJoinFail_ = false;  // portal is the join-failed fallback, not fresh onboarding
    bool radioOff_ = false;  // ride mode: WiFi powered down (BLE-only); handle() then no-ops
    bool logEnabled_ = true;  // serve GET /log? (persisted in NVS via WifiCreds)
    bool otaEnabled_ = false;  // authenticated ArduinoOTA started? (only when OTA_PASSWORD is set)
    std::string setupPin_;     // the setup AP's WPA2 password while in the portal (PIN on OLED builds)
};

}  // namespace sb20proxy
