#pragma once
#include <functional>
#include <string>
#include <vector>

#include "CalibrationPage.h"          // pure calibration wizard page (CalWizardView + render/parse)
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
//   * STATION  — creds known & join succeeds: serves status JSON at GET /, and self-resets if it
//                never becomes healthy (boot-guard). There is deliberately NO browser-reachable
//                flash route (the open /update form was removed, 2026-06-24 security review); push
//                OTA is authenticated ArduinoOTA, enabled only when OTA_PASSWORD is set (fail-closed),
//                and networked updates use the signed-pull path (code/findings/ota-update-plan.md).
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

    // The raw recent meter frames for the GET /diag tester report (the bytes we need to add a meter).
    using DiagFramesProvider = std::function<std::vector<std::string>()>;
    void setDiagFrames(DiagFramesProvider frames) { diagFrames_ = frames; }

    // Wire the meter-to-meter calibration wizard (GET /calibrate + POST start/finish/save/cancel +
    // GET /calibrate/scan). `view` renders the current wizard state; `start` persists the chosen
    // DUT+reference and marks this a calibration boot (WifiLink reboots to apply); `finish` fits the
    // collected pairs (returns false if too few); `save` persists the fitted corrector config
    // (WifiLink reboots); `cancel` clears the calibration marker (WifiLink reboots); `scan` refreshes
    // the candidate list. All decoupled from the BLE layer via hooks.
    using CalViewProvider = std::function<CalWizardView()>;
    // start/save return false if rejected (already calibrating / not yet fitted) — the route then
    // re-renders an error instead of rebooting.
    using CalStartHook = std::function<bool(const std::string& dutAddr, const std::string& refAddr)>;
    using CalFinishHook = std::function<bool()>;
    using CalSaveHook = std::function<bool(const std::string& deviceName)>;
    using CalSimpleHook = std::function<void()>;
    void setCalibrationUi(CalViewProvider view, CalStartHook start, CalFinishHook finish,
                          CalSaveHook save, CalSimpleHook cancel, CalSimpleHook scan) {
        calView_ = view;
        calStart_ = start;
        calFinish_ = finish;
        calSave_ = save;
        calCancel_ = cancel;
        calScan_ = scan;
    }

    // Wire the Workout screen (GET /workout page, GET /workout/state JSON, POST /workout/{load,
    // preset,start,pause,resume,skip,stop}). `state` returns the live cursor JSON; `load` ingests a
    // workout JSON (true if accepted); `control` dispatches a verb to the runtime. Decoupled from the
    // engine via hooks; presets are resolved in the route (WorkoutPresets.h) and fed through `load`.
    using WorkoutStateProvider = std::function<std::string()>;
    using WorkoutLoadHook = std::function<bool(const std::string& json)>;
    using WorkoutControlHook = std::function<void(const std::string& action)>;
    void setWorkoutUi(WorkoutStateProvider state, WorkoutLoadHook load, WorkoutControlHook control) {
        workoutState_ = state;
        workoutLoad_ = load;
        workoutControl_ = control;
    }

    // POST /curve: load a portable correction curve live (import a calibration profile). The hook
    // persists it + applies it to the running proxy (no reboot) — the shared web SPA's import path.
    using CurveSetHook = std::function<void(const CorrectionCurve&)>;
    void setCurveHandler(CurveSetHook set) { curveSet_ = set; }

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
    void sendHtml_(const std::string& body);  // drain-aware page send (short-write safe)
    void startStationServer_();  // status/setup/calibrate/forget routes + opt-in auth OTA (WiFi joined)
    void addConfigRoutes_();     // GET /setup picker + POST /setup/save + GET /setup/scan
    void addCalibrationRoutes_();  // GET /calibrate + POST start/finish/save/cancel + GET scan
    void addRideModeRoute_();    // GET /wifi/off (confirm) + POST /wifi/off (radio down for riding)
    void addWorkoutRoutes_();    // GET /workout (+ /state) + POST /workout/{load,preset,controls}
    void startPortal_();         // SoftAP + captive DNS + setup routes
    void addLogRoutes_();        // GET /log + /log/on + /log/off (shared by both modes)
    void addForgetRoute_(const char* msg);  // POST /forget: clear creds + reboot (shared by both modes)
    void collectCsrfHeaders_();  // retain Origin/Referer on a server so csrfOk_() can read them
    bool csrfOk_();              // same-origin (CSRF) guard for state-changing routes; sends 403 on fail
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
    CurveSetHook curveSet_;
    ScanHook configScan_;
    DiagFramesProvider diagFrames_;
    CalViewProvider calView_;
    CalStartHook calStart_;
    CalFinishHook calFinish_;
    CalSaveHook calSave_;
    CalSimpleHook calCancel_;
    CalSimpleHook calScan_;
    WorkoutStateProvider workoutState_;
    WorkoutLoadHook workoutLoad_;
    WorkoutControlHook workoutControl_;
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
