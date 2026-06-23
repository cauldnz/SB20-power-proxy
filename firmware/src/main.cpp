// sb20proxy ESP32 — dual-role BLE proxy (the only Arduino/NimBLE file).
//   real meter (BLE central) -> [correction] -> spoofed Stages crank (BLE peripheral) -> SB20
// Wires the platform-agnostic ProxyCore (lib/proxy) to the real BLE impls.

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_timer.h>

#include "Config.h"
#include "ConfigStore.h"  // NVS-backed RuntimeConfig (the user's source/doubling)
#include "Correction.h"
#include "PerfMonitor.h"
#include "PerfStats.h"
#include "ProxyCore.h"
#include "StatusLed.h"
#include "ble/BleCrankPeripheral.h"

#if USE_MOCK_METER
  #include "MockMeter.h"
  static sb20proxy::MockMeter meter;
#else
  #include "ble/BleMeterClient.h"
  #include "CalibrationSession.h"  // on-device meter-to-meter calibration orchestration
  static sb20proxy::BleMeterClient meter;
  static sb20proxy::BleMeterClient refMeter;    // calibration REFERENCE (2nd BLE central; live only)
  static sb20proxy::CalibrationSession g_cal;    // the wizard's session (Idle/Collecting/Fitted)
  static bool g_calibrating = false;             // this boot is a live calibration session
  // The DUT/ref readings arrive on the NimBLE host task; g_cal is otherwise only touched from loop()
  // (the drain below + the HTTP handlers, which run inside wifi.handle()). So the notify callbacks just
  // stash the latest sample in these single-writer/single-reader volatiles and the loop drains them
  // into g_cal — keeping ALL g_cal access on the loop context (no cross-task race on its pairs vector).
  static volatile bool g_pendDut = false, g_pendRef = false;
  static volatile int16_t g_pendDutP = 0, g_pendRefP = 0;
  static volatile uint32_t g_pendDutT = 0, g_pendRefT = 0;
#endif

#if USE_WIFI
  #include <ArduinoOTA.h>
  #include <Preferences.h>
  #include <WiFi.h>
  #include <esp_heap_caps.h>
  #include "net/WifiLink.h"
#endif

#if USE_OLED
  #include "disp/OledDisplay.h"
#endif

using namespace sb20proxy;

static BleCrankPeripheral crank;
static ProxyCore proxy(meter, crank,
                       Correction{Config::CORRECTION_SCALE, Config::CORRECTION_OFFSET});

#if USE_WIFI
static WifiLink wifi;
#endif

#if USE_OLED
static OledDisplay oled;
#endif

// --- perf instrumentation (Phase A/B) -----------------------------------------
static PerfMonitor perf;
volatile uint32_t g_loopBeat = 0;  // bumped each loop AND during OTA; the stall watchdog watches it

#if USE_WIFI
static uint32_t g_rebootCount = 0;       // persisted across reboots (NVS)
static int g_resetReason = 0;            // (int)esp_reset_reason() captured at boot
static std::string g_swReason;           // why the last sw-reset happened ("loop_stall" / "")
static uint32_t g_perfWindowStartMs = 0; // for /stats window_ms (reset by /stats/reset)
static esp_timer_handle_t s_stallTimer = nullptr;

// GET /stats — fill the pure PerfStats from the real esp_* reads (the seam), render via PerfStats.h.
static std::string perfStatsJson() {
    PerfStats p;
    p.loop = perf.summary();
    p.windowMs = millis() - g_perfWindowStartMs;
    p.freeHeap = ESP.getFreeHeap();
    p.minFreeHeap = ESP.getMinFreeHeap();
    p.largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    p.loopStackHwm = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);  // calling = loop task
    // CPU idle %: ulTaskGetIdleRunTimeCounter() isn't linkable in this Arduino FreeRTOS build, so
    // idle_pct stays -1 and we use loops/sec (loop_count / window_ms) as the headroom proxy instead
    // (computed in perf_soak.py — fewer loops/sec under load = less headroom).
    p.rebootCount = g_rebootCount;
    p.resetReasonCode = g_resetReason;
    p.swReason = g_swReason;
    p.uptimeMs = millis();
    return renderPerfJson(p);
}

// Phase B — software loop-stall watchdog. Runs from the esp_timer task (alive even when the Arduino
// loopTask is wedged): if loop() hasn't advanced for a full period, the device is hung — record why
// (so /stats shows it after the reboot) and restart. OTA-safe: ArduinoOTA.onProgress also bumps
// g_loopBeat, so a slow OTA transfer doesn't read as a stall.
static void stallWatchdogCb(void*) {
    static uint32_t lastBeat = 0;
    static bool primed = false;
    uint32_t beat = g_loopBeat;
    if (primed && beat == lastBeat) {
        Preferences p;
        if (p.begin("sb20perf", false)) {
            p.putString("swreason", "loop_stall");
            p.end();
        }
        esp_restart();
    }
    lastBeat = beat;
    primed = true;
}
#endif  // USE_WIFI

#if USE_OLED
// Dedicated OLED task: keeps the ~94 ms I2C render OFF the hot loop. Polls the live state and only
// redraws when the 4 displayed rows actually change (render-on-change). The full-buffer I2C send
// blocks THIS task (the IDF i2c driver yields on the transfer), so loop() no longer stalls on the
// panel — the per-render cost moves off the path that services BLE/WiFi.
static void oledTask(void*) {
    std::array<std::string, 4> last = {};
    for (;;) {
        OledMode mode = OledMode::Connected;
        std::string ip;
        int rssi = 0;
#if USE_WIFI
        mode = wifi.inPortal() ? OledMode::Portal
             : (wifi.isUp() ? OledMode::Connected : OledMode::Connecting);
        ip = std::string(WiFi.localIP().toString().c_str());
        rssi = wifi.isUp() ? WiFi.RSSI() : 0;
#endif
        const int balPct = proxy.lastSource().balance_half_pct >= 0
                               ? proxy.lastSource().balance_half_pct / 2 : -1;  // left %, -1 = none
        auto lines = formatOledLines(mode, ip, proxy.lastOutput().power_w,
                                     proxy.lastOutput().cadence_rpm, rssi, balPct);
        if (lines != last) {
            oled.drawLines(lines);
            last = lines;
        }
        vTaskDelay(pdMS_TO_TICKS(250));  // poll 4x/s; render only on change
    }
}
#endif  // USE_OLED

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);  // never block on USB-serial if no host is reading (raedian gotcha)
    delay(200);
    Serial.println("[sb20proxy] BLE crank proxy starting");

#if USE_WIFI
    // Capture reboot evidence ASAP (before anything can hang): the IDF reset reason + a persisted
    // reboot counter + the sw-reset detail the stall watchdog may have left last time.
    g_resetReason = (int)esp_reset_reason();
    {
        Preferences prefs;
        if (prefs.begin("sb20perf", false)) {
            g_rebootCount = prefs.getUInt("reboots", 0) + 1;
            prefs.putUInt("reboots", g_rebootCount);
            g_swReason = std::string(prefs.getString("swreason", "").c_str());
            prefs.remove("swreason");  // consume it; only the most recent sw-reset detail persists
            prefs.end();
        }
    }
    g_perfWindowStartMs = millis();
#endif

#if USE_OLED
    oled.begin();  // bring the panel up early so it can show portal / connecting
#endif

    // Load the user's saved config FIRST (NVS, set from the web UI) so the BLE stack + crank come up
    // under the configured identity. Defaults to compile-time Config when nothing is stored.
    RuntimeConfig cfg = ConfigStore::load();
#if defined(CORRECTOR_DEMO) && CORRECTOR_DEMO
    // Bench/demo only (the *-corrector-bench env): seed a CORRECTOR config so the corrector identity +
    // run-mode correction can be validated on real hardware before the calibration wizard (M4) exists.
    // A flat 1.10× curve makes the rebroadcast easy to verify (200 W in → 220 W out). Never in prod.
    cfg.mode = ProxyMode::Corrector;
    cfg.spoofName = Config::CORRECTOR_NAME;
    cfg.curve = CorrectionCurve{};
    cfg.curve.add(50.0f, 1.10f);
    cfg.curve.add(600.0f, 1.10f);
#endif

    NimBLEDevice::init(cfg.spoofName.c_str());  // the device name = our advertised identity
    crank.setMode(cfg.mode);                    // SPOOF crank vs CORRECTOR (own honest CPS identity)
    crank.setIdentity(cfg.spoofName, cfg.spoofSerial);  // advertised name + DIS serial

    // The correction between source and crank: CORRECTOR applies the fitted calibration curve
    // (DUT → reference) — and with an EMPTY curve falls through to identity (1.0×), NEVER the spoof's
    // linear scale / single-sided ×2 (those belong to SPOOF mode: a surviving R crank → total).
    Correction corr;
    if (cfg.mode == ProxyMode::Corrector) {
        corr.curve = cfg.curve;  // empty -> Correction.apply uses scale 1.0 / offset 0.0 (passthrough)
    } else {
        corr.scale = Config::CORRECTION_SCALE * (cfg.singleSidedDouble ? 2.0f : 1.0f);
        corr.offset = Config::CORRECTION_OFFSET;
    }
    proxy.setCorrection(corr);
#if !USE_MOCK_METER
    meter.setMatch(cfg.meterAddress, cfg.meterNameFilter);
    meter.setSpoofName(cfg.spoofName);  // keep the loop guard in sync with the runtime identity

    // Calibration wiring: the DUT (primary meter) feeds the session via the proxy tap; the reference
    // (2nd central) feeds it directly. Both no-op unless a session is collecting, so this is inert in
    // normal spoof/corrector runs. On a calibration boot (cfg.calibrating, set by /calibrate/start)
    // the reference is pinned + begun and the session starts — both meters then stream into it.
    // Stash each reading (NimBLE-task context) for the loop to drain into g_cal — never touch g_cal here.
    proxy.setTap([](const PowerReading& r) { g_pendDutP = r.power_w; g_pendDutT = r.t_ms; g_pendDut = true; });
    refMeter.onReading([](const PowerReading& r) { g_pendRefP = r.power_w; g_pendRefT = r.t_ms; g_pendRef = true; });
    g_calibrating = cfg.calibrating;
    if (g_calibrating) {
        refMeter.setMatch(cfg.refMeterAddress, cfg.refMeterNameFilter);
        refMeter.setSpoofName(cfg.spoofName);  // never read our own crank as the reference
        g_cal.start();
    }
#endif

    proxy.begin();  // crank advertises; source begins (scan, or nothing for mock)
#if !USE_MOCK_METER
    if (g_calibrating) refMeter.begin();  // 2nd central joins the shared scan, pinned to the reference
#endif

    Serial.printf("[sb20proxy] %s as '%s'; source=%s%s%s\n",
                  cfg.mode == ProxyMode::Corrector ? "corrector" : "spoofing", cfg.spoofName.c_str(),
                  USE_MOCK_METER ? "MOCK"
                                 : (cfg.meterAddress.empty() ? cfg.meterNameFilter.c_str()
                                                             : cfg.meterAddress.c_str()),
                  cfg.singleSidedDouble ? " (single-sided ×2)" : "",
                  USE_MOCK_METER ? "" : " [configurable]");

#if USE_WIFI
    pinMode(Config::STATUS_LED_PIN, OUTPUT);  // onboard status LED (active-low)
    // Join WiFi + bring up OTA and the status HTTP server. The provider renders live state
    // from the ProxyCore each request (curl http://<ip>/ — the reliable window into the C3).
    wifi.begin("sb20proxy", []() {
        ProxyStatus s;
#if USE_MOCK_METER
        s.mock = true;
#else
        s.sourceConnected = meter.connected();  // real BLE-central link to the meter (goal #1)
        if (meter.connected()) s.srcName = meter.sourceName();  // show which meter on the dashboard
#endif
        s.forwarded = proxy.forwarded();
        s.srcPowerW = proxy.lastSource().power_w;        // received from the meter
        s.srcCadenceRpm = proxy.lastSource().cadence_rpm;
        s.srcBalanceHalfPct = proxy.lastSource().balance_half_pct;
        s.lastPowerW = proxy.lastOutput().power_w;        // broadcast to the crank
        s.lastCadenceRpm = proxy.lastOutput().cadence_rpm;
        s.lastBalanceHalfPct = proxy.lastOutput().balance_half_pct;
        s.rssi = WiFi.RSSI();
        s.freeHeap = ESP.getFreeHeap();
        s.uptimeMs = millis();
        return s;
    });
    if (wifi.inPortal()) {
        Serial.println("[sb20proxy] WiFi not configured; setup portal up on AP 'SB20-Setup' "
                       "-> http://192.168.4.1/");
    } else {
        Serial.printf("[sb20proxy] WiFi connected; status at http://%s/\n",
                      WiFi.localIP().toString().c_str());
    }

    // Phase A/B: wire GET /stats + /stats/reset, and arm the loop-stall watchdog.
    wifi.setPerf(perfStatsJson, []() {
        perf.reset();
        g_perfWindowStartMs = millis();
    });
    // Source-setup UI (GET/POST /setup): pick the meter / surviving crank over WiFi, persist to NVS,
    // reboot to apply. Decoupled via hooks — the candidate list + rescan come from the live central;
    // a mock build has no sources to offer.
    wifi.setConfigUi(
        []() { return ConfigStore::load(); },
#if USE_MOCK_METER
        []() { return std::vector<sb20proxy::SourceCandidate>{}; },
        [](const RuntimeConfig& c) { ConfigStore::save(c); },
        []() {});
#else
        []() { return meter.candidates(); },
        [](const RuntimeConfig& c) { ConfigStore::save(c); },
        []() { meter.clearCandidates(); });
#endif
    // The tester /diag report's raw meter frames (the bytes we add a new meter from). Live only.
#if USE_MOCK_METER
    wifi.setDiagFrames([]() { return std::vector<std::string>{}; });
#else
    wifi.setDiagFrames([]() { return meter.recentFrames(); });

    // The meter-to-meter calibration wizard (GET /calibrate). The view reflects the live session;
    // start/save/cancel persist + reboot (the wizard moves in/out of a calibration boot). Live only.
    wifi.setCalibrationUi(
        []() {  // build the wizard view from the live session + meter state
            CalWizardView v;
            const RuntimeConfig c = ConfigStore::load();
            if (!c.calibrating) {
                v.state = CalState::Idle;
                v.devices = meter.candidates();
            } else if (g_cal.fitted()) {
                v.state = CalState::Fitted;
                const Correction& fit = g_cal.fit();
                v.residualW = g_cal.residualW();
                if (fit.curve.empty()) { v.linear = true; v.scale = fit.scale; v.offset = fit.offset; }
                else { v.curve = fit.curve; }
            } else {
                v.state = CalState::Collecting;
                v.dutConnected = meter.connected();
                v.refConnected = refMeter.connected();
                v.pairCount = (int)g_cal.pairCount();
                v.minPairs = g_cal.minPairs();
                v.enoughToFit = g_cal.enoughToFit();
                v.coverage = g_cal.coverage();
            }
            return v;
        },
        [](const std::string& dut, const std::string& ref) -> bool {  // start: persist a calibration boot
            if (ConfigStore::load().calibrating) return false;   // already calibrating — don't discard it
            RuntimeConfig c = ConfigStore::load();
            c.calibrating = true;
            c.meterAddress = dut; c.meterNameFilter = "";       // primary meter = DUT
            c.refMeterAddress = ref; c.refMeterNameFilter = "";  // reference = ref
            ConfigStore::save(c);
            return true;
        },
        []() { return g_cal.finish(); },                         // fit the collected pairs
        [](const std::string& name) -> bool {                    // save: persist the corrector config
            if (!g_cal.fitted()) return false;                   // never ship an un-fit (1.0×) corrector
            RuntimeConfig c = ConfigStore::load();
            c.mode = ProxyMode::Corrector;
            c.spoofName = name.empty() ? std::string(Config::CORRECTOR_NAME) : name;
            c.curve = correctionToCurve(g_cal.fit());            // store the fit as a curve
            c.calibrating = false;
            c.refMeterAddress = ""; c.refMeterNameFilter = "";   // (meterAddress stays = the DUT)
            ConfigStore::save(c);
            return true;
        },
        []() {                                                   // cancel: clear the calibration marker
            RuntimeConfig c = ConfigStore::load();
            c.calibrating = false;
            c.refMeterAddress = ""; c.refMeterNameFilter = "";   // clear the stale calibration ref pins
            ConfigStore::save(c);
        },
        []() { meter.clearCandidates(); });                      // scan: refresh the candidate list
#endif
    ArduinoOTA.onProgress([](unsigned int, unsigned int) { ++g_loopBeat; });  // keep WD fed during OTA
    esp_timer_create_args_t wdArgs = {};
    wdArgs.callback = &stallWatchdogCb;
    wdArgs.dispatch_method = ESP_TIMER_TASK;
    wdArgs.name = "stallwd";
    if (esp_timer_create(&wdArgs, &s_stallTimer) == ESP_OK) {
        esp_timer_start_periodic(s_stallTimer, (uint64_t)15000 * 1000);  // 15 s window
    }
#endif

#if USE_OLED
    // Render the OLED on its own task so the I2C transfer never blocks the hot loop.
    xTaskCreate(oledTask, "oled", 4096, nullptr, 1, nullptr);
#endif
}

void loop() {
    ++g_loopBeat;                       // feed the stall watchdog (Phase B)
    perf.sample(esp_timer_get_time());  // record this loop's period (Phase A)
    proxy.loop();
#if !USE_MOCK_METER
    if (g_calibrating) {
        refMeter.loop();  // service the 2nd central during a calibration session
        // Drain the meters' stashed readings into g_cal HERE (loop context), before wifi.handle()
        // reads it — ref first so the accumulator has a reference when the DUT sample pairs.
        if (g_pendRef) { g_pendRef = false; g_cal.onRef(g_pendRefP, g_pendRefT); }
        if (g_pendDut) { g_pendDut = false; g_cal.onDut(g_pendDutP, g_pendDutT); }
    }
#endif

#if !USE_MOCK_METER
    // Meter just dropped -> clear the last readings so the OLED / /stats don't show stale numbers
    // (source already flips to "searching"; this stops a stale power_w lingering alongside it).
    static bool wasConnected = false;
    const bool nowConnected = meter.connected();
    if (wasConnected && !nowConnected) proxy.reset();
    wasConnected = nowConnected;
#endif

#if USE_WIFI
    wifi.handle();  // service HTTP + OTA, promote to healthy

    // Onboard status LED: fast blink = setup portal / joining, slow pulse = connected.
    const LinkState ls = wifi.isUp() ? LinkState::Connected : LinkState::Searching;
    digitalWrite(Config::STATUS_LED_PIN, StatusLed::lit(ls, millis()) ? LOW : HIGH);  // active-low
#endif

    // (OLED now renders on its own task — see oledTask — so it never blocks this loop.)

#if USE_MOCK_METER
    // Drive the mock source at 1 Hz with a gentle 100..300..100 W ramp so the spoofed
    // crank can be witnessed on a phone/Garmin with no real meter present.
    static uint32_t last = 0;
    static int t = 0;
    uint32_t now = millis();
    if (now - last >= 1000) {
        last = now;
        t = (t + 1) % 40;
        int p = 100 + (t < 20 ? t : 40 - t) * 10;
        meter.emit((int16_t)p, 85, now);
        Serial.printf("[proxy] mock=%dW -> crank=%dW\n", p, proxy.lastOutput().power_w);
    }
#endif
    delay(5);
}
