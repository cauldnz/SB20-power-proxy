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
  static sb20proxy::BleMeterClient meter;
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

    NimBLEDevice::init(Config::SPOOF_NAME);

    // Apply the user's saved source/doubling (NVS, set from the web UI) before the source starts.
    // Defaults to the compile-time Config when nothing is stored. Single-sided ×2 folds into the
    // correction scale (a surviving R crank / single-sided meter → doubled for total).
    RuntimeConfig cfg = ConfigStore::load();
    proxy.setCorrection(Correction{Config::CORRECTION_SCALE * (cfg.singleSidedDouble ? 2.0f : 1.0f),
                                   Config::CORRECTION_OFFSET});
#if !USE_MOCK_METER
    meter.setMatch(cfg.meterAddress, cfg.meterNameFilter);
#endif

    proxy.begin();  // crank advertises; source begins (scan, or nothing for mock)

    Serial.printf("[sb20proxy] advertising as '%s'; source=%s%s%s\n", Config::SPOOF_NAME,
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
