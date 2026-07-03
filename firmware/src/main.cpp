// sb20proxy ESP32 — dual-role BLE proxy (the only Arduino/NimBLE file).
//   real meter (BLE central) -> [correction] -> spoofed Stages crank (BLE peripheral) -> SB20
// Wires the platform-agnostic ProxyCore (lib/proxy) to the real BLE impls.

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_timer.h>

#include "Config.h"
#include "ConfigStore.h"  // NVS-backed RuntimeConfig (the user's source/doubling)
#include "Correction.h"
#include "WorkoutRuntime.h"  // live workout clock + engine (lib/proxy)
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
  // Set by the crank peripheral's CP-write callback when the SB20/app asks for a zero-reset (0x0C/0x10);
  // loop() drains it into meter.requestZeroOffset() so the REAL source meter (Assioma) gets zeroed —
  // off the BLE callback context, never a re-entrant central op. forward-plan §10.
  static volatile bool g_pendZeroReset = false;
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
#if USE_LCD
  #include "LcdUi.h"              // pure head-unit UI (screens + tap routing; LCD_W x LCD_H)
  #if defined(LCD_DRIVER_CYD) && LCD_DRIVER_CYD
    #include "disp/CydDisplay.h"  // ILI9341/ST7789 + XPT2046 seam (ESP32-2432S028R "CYD")
  #else
    #include "disp/LcdDisplay.h"  // JD9853 LCD + AXS5106 touch seam (S3-Touch board)
  #endif
#endif

using namespace sb20proxy;

static BleCrankPeripheral crank;
static ProxyCore proxy(meter, crank,
                       Correction{Config::CORRECTION_SCALE, Config::CORRECTION_OFFSET});

#if USE_WIFI
static WifiLink wifi;
#include "WorkoutStore.h"            // NVS-backed workout JSON (the loaded structured workout)
#endif

#if USE_WIFI || USE_LCD
static WorkoutRuntime g_wk;          // the live workout clock the /workout screen + LCD drive
// One recursive mutex guards g_wk (+ the LCD UI state) so a screen tap and an HTTP /workout control
// never mutate the runtime concurrently. Null (no-op) until the LCD task creates it; on WiFi-only
// builds it stays null and the lock is a no-op (single-threaded access via the loop task).
static SemaphoreHandle_t g_lcdMux = nullptr;
static inline void lcdLock() { if (g_lcdMux) xSemaphoreTakeRecursive(g_lcdMux, portMAX_DELAY); }
static inline void lcdUnlock() { if (g_lcdMux) xSemaphoreGiveRecursive(g_lcdMux); }
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
        std::string setupPin;
        int rssi = 0;
#if USE_WIFI
        mode = wifi.inPortal() ? OledMode::Portal
             : (wifi.isUp() ? OledMode::Connected : OledMode::Connecting);
        ip = std::string(WiFi.localIP().toString().c_str());
        rssi = wifi.isUp() ? WiFi.RSSI() : 0;
        if (mode == OledMode::Portal) setupPin = wifi.setupPin();  // shown so the rider can join the AP
#endif
        const int balPct = proxy.lastSource().balance_half_pct >= 0
                               ? proxy.lastSource().balance_half_pct / 2 : -1;  // left %, -1 = none
        auto lines = formatOledLines(mode, ip, proxy.lastOutput().power_w,
                                     proxy.lastOutput().cadence_rpm, rssi, balPct, setupPin);
        if (lines != last) {
            oled.drawLines(lines);
            last = lines;
        }
        vTaskDelay(pdMS_TO_TICKS(250));  // poll 4x/s; render only on change
    }
}
#endif  // USE_OLED

#if USE_LCD
// The LCD head-unit (S3-Touch JD9853 or the CYD's ILI9341/ST7789). The pure UI (lib/proxy/LcdUi.h)
// renders into a canvas from view structs built from live state; this task blits it over SPI +
// reads touches. Runs on core 1 so the SPI blit never competes with BLE/loop on core 0. All shared
// state (g_wk, g_lcdUi) is touched only under g_lcdMux, shared with the WiFi /workout hooks so a
// tap and an HTTP control can't race.
#if defined(LCD_DRIVER_CYD) && LCD_DRIVER_CYD
using PanelDisplay = CydDisplay;
#else
using PanelDisplay = LcdDisplay;
#endif
static PanelDisplay lcd;
static LcdUiState g_lcdUi;
// LCD_BANDS: on no-PSRAM boards (the classic-ESP32 CYD) the full frame can't sit beside WiFi+BLE,
// so the canvas holds one horizontal band and the render loop sweeps it down the frame (the pure
// renderer is band-agnostic — proven pixel-identical by test_lcd_banded_render_matches_full).
#ifndef LCD_BANDS
#define LCD_BANDS 1
#endif
static_assert(LCD_H % LCD_BANDS == 0, "LCD_BANDS must divide LCD_H");
// The framebuffer is allocated LAZILY (first use, at runtime) — a std::vector this large in a
// global constructor runs during C++ static init, before the Arduino heap is fully ready on the S3,
// and aborts the boot. A function-local static defers it to the LCD task's first iteration.
static LcdCanvas& g_lcdFrame() {
#if LCD_BANDS > 1
    static LcdCanvas frame(LCD_H / LCD_BANDS);
#else
    static LcdCanvas frame;
#endif
    return frame;
}
static std::string g_lcdIdentity, g_lcdSource, g_lcdMeterAddr;  // captured from cfg at boot
static bool g_lcdCorrector = false;
static int16_t g_lcdHist[160] = {0};       // power-history ring for the Ride sparkline
static int g_lcdHistN = 0;
static volatile int g_lcdInjX = -1, g_lcdInjY = -1;  // synthetic tap from the serial bench console

#if defined(LCD_DRIVER_CYD) && LCD_DRIVER_CYD
// --- resistive touch calibration ritual (TouchCal.h) — CYD only; the S3 is capacitive -------
// Old-school tap-the-crosshair: 4 corner targets -> per-axis least-squares fit -> NVS. Runs in
// the LCD task INSTEAD of the normal UI while active. Auto-runs on first boot (no stored cal);
// serial: CALTOUCH re-runs it, RAWTAP <rx> <ry> injects a synthetic raw press (headless twin
// test of the whole ritual), CALINFO prints the active fit.
struct CalRitual {
    volatile bool active = false;
    int idx = 0;                       // which crosshair
    TouchCalPoint pts[TOUCH_CAL_POINTS] = {};
    float accX = 0, accY = 0;          // raw accumulation for the current press
    int nAcc = 0;
    bool wasDown = false;
    int done = -1;                     // -1 collecting · 1 saved · 0 failed (brief, then retry)
    uint32_t doneAt = 0;
    int testX = -1, testY = -1;        // verification-tap marker on the success screen
    volatile int injLeft = 0;          // synthetic raw press: remaining pressed ticks
    volatile uint16_t injRx = 0, injRy = 0;
};
static CalRitual g_tcal;

static void touchCalSave(const TouchCalFit& f) {
    Preferences p;
    if (p.begin("sb20touch", false)) {
        p.putFloat("sx", f.sx); p.putFloat("ox", f.ox);
        p.putFloat("sy", f.sy); p.putFloat("oy", f.oy);
        p.putBool("valid", f.valid);
        p.end();
    }
}
static TouchCalFit touchCalLoad() {
    TouchCalFit f;
    Preferences p;
    if (p.begin("sb20touch", true)) {
        f.sx = p.getFloat("sx", 0); f.ox = p.getFloat("ox", 0);
        f.sy = p.getFloat("sy", 0); f.oy = p.getFloat("oy", 0);
        f.valid = p.getBool("valid", false);
        p.end();
    }
    return f;
}

// One 20 ms tick of the ritual: sample (real film or injected), accumulate while pressed,
// record on release, fit + persist after the 4th point. Returns the screen to render.
static void touchCalTick() {
    uint16_t rx = 0, ry = 0, z = 0;
    bool down;
    if (g_tcal.injLeft > 0) { down = true; rx = g_tcal.injRx; ry = g_tcal.injRy; --g_tcal.injLeft; }
    else down = lcd.readRaw(rx, ry, z);

    if (g_tcal.done == 1) {                       // success close-out: taps show where they map
        if (down && !g_tcal.wasDown) {
            lcd.rawToScreen(rx, ry, g_tcal.testX, g_tcal.testY);
            g_tcal.doneAt = millis();             // keep testing? keep the screen up
        }
        g_tcal.wasDown = down;
        if (millis() - g_tcal.doneAt > 6000) { g_tcal.active = false; g_tcal.done = -1; }
        return;
    }
    if (g_tcal.done == 0) {                       // failure flash, then restart the ritual
        if (millis() - g_tcal.doneAt > 2000) { g_tcal.idx = 0; g_tcal.done = -1; }
        return;
    }
    if (down) {
        g_tcal.accX += rx; g_tcal.accY += ry; ++g_tcal.nAcc;
    } else if (g_tcal.wasDown && g_tcal.nAcc >= 5) {  // a solid press just released -> record
        int tx, ty;
        touchCalTarget(g_tcal.idx, tx, ty);
        g_tcal.pts[g_tcal.idx] = {g_tcal.accX / g_tcal.nAcc, g_tcal.accY / g_tcal.nAcc,
                                  (float)tx, (float)ty};
        Serial.printf("[tcal] point %d/%d raw=(%.0f,%.0f) target=(%d,%d)\n", g_tcal.idx + 1,
                      TOUCH_CAL_POINTS, (double)g_tcal.pts[g_tcal.idx].rawX,
                      (double)g_tcal.pts[g_tcal.idx].rawY, tx, ty);
        g_tcal.accX = g_tcal.accY = 0; g_tcal.nAcc = 0;
        if (++g_tcal.idx >= TOUCH_CAL_POINTS) {
            TouchCalFit f = touchCalFit(g_tcal.pts, TOUCH_CAL_POINTS);
            if (f.valid) {
                lcd.setCal(f);
                touchCalSave(f);
                Serial.printf("[tcal] SAVED sx=%.4f ox=%.1f sy=%.4f oy=%.1f\n", (double)f.sx,
                              (double)f.ox, (double)f.sy, (double)f.oy);
                g_tcal.done = 1;
            } else {
                Serial.println("[tcal] fit REJECTED (taps too clustered) - retrying");
                g_tcal.idx = 0;
                g_tcal.done = 0;
            }
            g_tcal.doneAt = millis();
            g_tcal.testX = g_tcal.testY = -1;
        }
    } else {
        g_tcal.accX = g_tcal.accY = 0; g_tcal.nAcc = 0;  // too-short blip: discard
    }
    g_tcal.wasDown = down;
}
#endif  // LCD_DRIVER_CYD

// Fill the view structs from live proxy/meter/workout/wifi state. Called under lock.
static void buildLcdViews(LcdViews& v) {
    RideView& r = v.ride;
    r.outName = g_lcdIdentity;
    r.version = std::string(Config::FIRMWARE_VERSION);
    r.watts = proxy.lastOutput().power_w;
    r.srcWatts = proxy.lastSource().power_w;
    r.cadence = proxy.lastOutput().cadence_rpm;
    r.balancePct = proxy.lastOutput().balance_half_pct >= 0
                       ? proxy.lastOutput().balance_half_pct / 2 : -1;
    r.hist = g_lcdHist;
    r.nHist = g_lcdHistN;
    r.histMax = 300;
    r.freeHeap = ESP.getFreeHeap();
    r.uptimeMs = millis();
    r.outOn = true;
#if USE_MOCK_METER
    r.srcName = "mock meter";
    r.srcOn = true;
#else
    r.srcOn = meter.connected();
    r.srcName = meter.connected() ? meter.sourceName() : std::string("searching...");
#endif
#if USE_WIFI
    r.wifiRssi = WiFi.RSSI();
    v.more.ip = wifi.isUp() ? std::string(WiFi.localIP().toString().c_str()) : std::string("no wifi");
#endif

    // Workout (from the shared runtime)
    WorkoutView& w = v.wk;
    w.loaded = !g_wk.workout.segments.empty();
    w.running = g_wk.running;
    w.paused = g_wk.paused;
    w.w = w.loaded ? &g_wk.workout : nullptr;
    w.st = g_wk.state(millis());
    w.nowW = proxy.lastOutput().power_w;
    w.nowCad = proxy.lastOutput().cadence_rpm;
    w.ergConfigured = false;  // trainer/erg wiring lands in the next PR (§14 phase 4)

    // Ride's live-workout strip mirrors the workout state
    r.wkRunning = w.running;
    r.wkPaused = w.paused;
    r.wkTarget = w.st.targetW;
    r.wkRemainS = w.st.segRemainingS;

    // Setup: scanned candidates (live builds only)
    SetupView& s = v.setup;
#if !USE_MOCK_METER
    s.devices = meter.candidates();
#endif
    s.meterAddr = g_lcdMeterAddr;

    // More / Settings summary
    MoreView& m = v.more;
    m.mode = g_lcdCorrector ? "Corrector" : "Crank spoof";
    m.identity = g_lcdIdentity;
    m.source = g_lcdSource;
    m.trainer = "not set";
    m.version = std::string(Config::FIRMWARE_VERSION);
    m.brightness = g_lcdUi.brightness;

    // Calibrate wizard view (live builds carry the session; bench shows the idle prompt)
    CalWizardView& cal = v.cal;
#if !USE_MOCK_METER
    const RuntimeConfig cc = ConfigStore::load();
    if (cc.calibrating) {
        if (g_cal.fitted()) {
            cal.state = CalState::Fitted;
            const Correction& fit = g_cal.fit();
            cal.residualW = g_cal.residualW();
            if (fit.curve.empty()) { cal.linear = true; cal.scale = fit.scale; cal.offset = fit.offset; }
            else cal.curve = fit.curve;
        } else {
            cal.state = CalState::Collecting;
            cal.dutConnected = meter.connected();
            cal.refConnected = refMeter.connected();
            cal.pairCount = (int)g_cal.pairCount();
            cal.minPairs = g_cal.minPairs();
            cal.enoughToFit = g_cal.enoughToFit();
            cal.coverage = g_cal.coverage();
        }
    } else {
        cal.state = CalState::Idle;
        cal.devices = meter.candidates();
        // DUT/Ref selection from the LCD lands with the erg/calibrate-action PR; use the phone
        // wizard (/calibrate) meanwhile.
    }
#endif
}

// Execute a device-level action produced by a tap. Called under lock, on the LCD task.
static void lcdExecute(const UiAction& a, const LcdViews& v) {
    switch (a.type) {
        case UiAction::WorkoutPreset:
            if (a.index >= 0 && a.index < (int)workoutPresets().size()) {
                const std::string j = workoutPresets()[a.index].json;
                Workout w = parseWorkout(j);
                if (!w.segments.empty()) {
                    g_wk.load(w);
#if USE_WIFI
                    WorkoutStore::save(j);
#endif
                }
            }
            break;
        case UiAction::WorkoutStart:  g_wk.start(millis()); break;
        case UiAction::WorkoutPause:  g_wk.pause(millis()); break;
        case UiAction::WorkoutResume: g_wk.resume(millis()); break;
        case UiAction::WorkoutSkip:   g_wk.skip(millis()); break;
        case UiAction::WorkoutStop:   g_wk.stop(); break;
        case UiAction::SetBrightness: lcd.setBrightness((uint8_t)a.index); break;
        case UiAction::SetupPick: {
            const auto ds = dedupeAndSortSources(v.setup.devices);
            if (a.index >= 0 && a.index < (int)ds.size()) {
                g_lcdMeterAddr = ds[a.index].address;
                g_lcdSource = ds[a.index].name.empty() ? ds[a.index].address : ds[a.index].name;
            }
            break;
        }
        case UiAction::SetupSave:
            if (!g_lcdMeterAddr.empty()) {
                RuntimeConfig c = ConfigStore::load();
                c.meterAddress = g_lcdMeterAddr;
                ConfigStore::save(c);
                Serial.println("[lcd] source saved; rebooting to apply");
                delay(300);
                esp_restart();
            }
            break;
        // Calibrate actions are wired in the FTMS/erg PR (they need the live g_cal session).
        default: break;
    }
}

static void lcdTask(void*) {
    LcdViews views;
    uint32_t lastRender = 0;
    for (;;) {
#if defined(LCD_DRIVER_CYD) && LCD_DRIVER_CYD
        // Touch-calibration ritual: replaces the normal UI while active.
        if (g_tcal.active) {
            touchCalTick();
            const int rows = LCD_H / LCD_BANDS;
            for (int b = 0; b < LCD_BANDS; ++b) {
                g_lcdFrame().setBand(b * rows);
                g_lcdFrame().clear();
                renderTouchCalScreen(g_lcdFrame(), g_tcal.idx, g_tcal.done, g_tcal.testX,
                                     g_tcal.testY);
                lcd.blit(g_lcdFrame());
            }
            lastRender = 0;  // force a fresh UI paint when the ritual ends
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
#endif
        // 1) touch (real digitiser OR a synthetic tap injected over serial)
        int tx = -1, ty = -1;
        if (g_lcdInjX >= 0) { tx = g_lcdInjX; ty = g_lcdInjY; g_lcdInjX = -1; }
        else lcd.readTap(tx, ty);
        if (tx >= 0) {
            lcdLock();
            buildLcdViews(views);
            UiAction a = lcdHandleTap(g_lcdUi, views, tx, ty);
            lcdExecute(a, views);
            lcdUnlock();
            lastRender = 0;  // force an immediate repaint after a tap
        }
        // 2) render at ~5 Hz (or right after a tap). With LCD_BANDS>1 the canvas is one
        // horizontal slice: sweep it down the frame, rendering + blitting per band (the pure
        // renderer clips out-of-band writes, so it just re-runs per band).
        uint32_t now = millis();
        if (now - lastRender >= 200) {
            lastRender = now;
            const int rows = LCD_H / LCD_BANDS;
            for (int b = 0; b < LCD_BANDS; ++b) {
                lcdLock();
                buildLcdViews(views);
                g_lcdFrame().setBand(b * rows);
                g_lcdFrame().clear();
                lcdRender(g_lcdFrame(), g_lcdUi, views);
                lcdUnlock();
                lcd.blit(g_lcdFrame());
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));  // 50 Hz touch poll
    }
}

// Serial bench console (headless overnight verification, no WiFi/creds needed):
//   SCREEN  -> base64(BMP) of the current frame, one line, wrapped in <BMP.. ..BMP>
//   TAP x y -> inject a synthetic touch
//   STATE   -> a compact JSON of screen + key live values
static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void lcdSerialConsole() {
    static std::string line;
    while (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\r') continue;
        if (ch != '\n') { line += ch; if (line.size() > 64) line.clear(); continue; }
        std::string cmd = line; line.clear();
        if (cmd == "SCREEN") {
            // Stream a TOP-DOWN 24bpp BMP (negative biHeight) band by band, so it works with
            // the banded no-PSRAM canvas too (no full-frame buffer is ever allocated).
            const int rowBytes = ((LCD_W * 3 + 3) / 4) * 4;
            const uint32_t dataSize = (uint32_t)rowBytes * LCD_H;
            uint8_t hdr[54] = {0};
            auto w32 = [&](int off, uint32_t v) {
                hdr[off] = v & 0xFF; hdr[off + 1] = (v >> 8) & 0xFF;
                hdr[off + 2] = (v >> 16) & 0xFF; hdr[off + 3] = (v >> 24) & 0xFF;
            };
            hdr[0] = 'B'; hdr[1] = 'M';
            w32(2, 54 + dataSize); w32(10, 54); w32(14, 40);
            w32(18, (uint32_t)LCD_W); w32(22, (uint32_t)(-LCD_H));  // negative = top-down rows
            hdr[26] = 1; hdr[28] = 24; w32(34, dataSize);
            uint32_t acc = 0; int bits = 0;
            // batch the base64 stream (per-byte Serial.write takes a UART lock each call), and
            // FEED THE LOOP-STALL WATCHDOG from the emit path — a full-screen stream blocks
            // loop() for ~30 s at 115200, well past the 15 s stall window (found on the CYD:
            // the watchdog restarted the board mid-SCREEN).
            static uint8_t obuf[512]; size_t on = 0;
            auto emit = [&](char c) {
                obuf[on++] = (uint8_t)c;
                if (on == sizeof(obuf)) { Serial.write(obuf, on); on = 0; ++g_loopBeat; }
            };
            auto put = [&](uint8_t b) {
                acc = (acc << 8) | b; bits += 8;
                while (bits >= 6) { bits -= 6; emit(kB64[(acc >> bits) & 0x3F]); }
            };
            Serial.print("<BMP");
            for (int i = 0; i < 54; ++i) put(hdr[i]);
            const int rows = LCD_H / LCD_BANDS;
            LcdViews views;
            for (int b = 0; b < LCD_BANDS; ++b) {
                // hold the lock across the band's render AND row stream: the lcdTask shares
                // this canvas and would re-band/re-render it under our feet otherwise.
                lcdLock();
                buildLcdViews(views);
                g_lcdFrame().setBand(b * rows);
                g_lcdFrame().clear();
                lcdRender(g_lcdFrame(), g_lcdUi, views);
                for (int y = b * rows; y < (b + 1) * rows; ++y) {
                    for (int x = 0; x < LCD_W; ++x) {
                        uint16_t v = g_lcdFrame().get(x, y);
                        put((uint8_t)((v & 0x1F) << 3));           // B
                        put((uint8_t)(((v >> 5) & 0x3F) << 2));    // G
                        put((uint8_t)(((v >> 11) & 0x1F) << 3));   // R
                    }
                    for (int p = LCD_W * 3; p < rowBytes; ++p) put(0);
                }
                lcdUnlock();
            }
            if (bits > 0) emit(kB64[(acc << (6 - bits)) & 0x3F]);
            if (on) Serial.write(obuf, on);
            Serial.println("BMP>");
        } else if (cmd.rfind("TAP ", 0) == 0) {
            int x = 0, y = 0;
            if (sscanf(cmd.c_str() + 4, "%d %d", &x, &y) == 2) {
                g_lcdInjX = x; g_lcdInjY = y;
                Serial.printf("[lcd] tap injected %d,%d\n", x, y);
            }
#if defined(LCD_DRIVER_CYD) && LCD_DRIVER_CYD
        } else if (cmd == "CALTOUCH") {           // (re)run the touch-calibration ritual
            g_tcal = CalRitual{};
            g_tcal.active = true;
            Serial.println("[tcal] ritual started");
        } else if (cmd.rfind("RAWTAP ", 0) == 0) {  // synthetic raw press (headless cal test)
            unsigned rx = 0, ry = 0;
            if (sscanf(cmd.c_str() + 7, "%u %u", &rx, &ry) == 2) {
                g_tcal.injRx = (uint16_t)rx; g_tcal.injRy = (uint16_t)ry;
                g_tcal.injLeft = 8;               // ~8 pressed ticks then release
                Serial.printf("[tcal] raw press injected %u,%u\n", rx, ry);
            }
        } else if (cmd == "CALINFO") {
            const TouchCalFit& f = lcd.cal();
            Serial.printf("{\"cal_valid\":%d,\"sx\":%.5f,\"ox\":%.1f,\"sy\":%.5f,\"oy\":%.1f,"
                          "\"ritual\":%d,\"point\":%d}\n", f.valid, (double)f.sx, (double)f.ox,
                          (double)f.sy, (double)f.oy, g_tcal.active, g_tcal.idx);
        } else if (cmd == "CALCLEAR") {           // wipe the stored cal + restart the ritual
            Preferences p;
            if (p.begin("sb20touch", false)) { p.clear(); p.end(); }
            lcd.setCal(TouchCalFit{});
            g_tcal = CalRitual{};
            g_tcal.active = true;
            Serial.println("[tcal] cleared - ritual restarted");
#endif
        } else if (cmd == "STATE") {
            lcdLock();
            Serial.printf("{\"screen\":%d,\"details\":%d,\"power\":%d,\"cad\":%d,"
                          "\"wk_loaded\":%d,\"wk_running\":%d,\"wk_target\":%d,\"touch\":%d}\n",
                          (int)g_lcdUi.screen, g_lcdUi.rideDetails, proxy.lastOutput().power_w,
                          proxy.lastOutput().cadence_rpm, !g_wk.workout.segments.empty(),
                          g_wk.running, g_wk.state(millis()).targetW, lcd.touchAlive());
            lcdUnlock();
        }
    }
}
#endif  // USE_LCD

// Diagnostic breadcrumb (S3 bring-up): persist how far setup() got, so a crash/hang is pinpointed by
// dumping NVS afterwards — native-USB serial is flaky on these boards, but NVS survives the reboot.
// No-op in production (guarded by S3_DIAG, set only by the esp32s3-min diagnostic env) so a normal
// boot never spends an NVS write on it. See findings/advanced-board-s3-touch.md "bring-up status".
#if defined(S3_DIAG) && USE_WIFI
static void bootStage(const char* s) {
    Preferences p;
    if (p.begin("sb20perf", false)) { p.putString("bootstage", s); p.end(); }
    Serial.printf("[boot] %s\n", s);
}
#else
static inline void bootStage(const char*) {}
#endif

void setup() {
    Serial.begin(115200);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0);  // USB-CDC only: never block if no host is reading (raedian gotcha)
#endif
    delay(200);
    Serial.println("[sb20proxy] BLE crank proxy starting");
#ifdef S3_DIAG
    for (int i = 0; i < 8; i++) { Serial.printf("[diag] alive %d heap=%u\n", i, (unsigned)ESP.getFreeHeap()); delay(250); }
    Serial.println("[diag] stage: pre-config");
#endif
    bootStage("setup-start");

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
#ifdef S3_DIAG
    Serial.printf("[diag] stage: config loaded, spoof=%s\n", cfg.spoofName.c_str());
#endif
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

    bootStage("pre-ble");
#ifdef S3_DIAG
    Serial.println("[diag] stage: pre-NimBLE-init");
#endif
    NimBLEDevice::init(cfg.spoofName.c_str());
#ifdef S3_DIAG
    Serial.println("[diag] stage: NimBLE-init done");
#endif  // the device name = our advertised identity
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

    // The SB20/app's calibrate (CP 0x10) should perform a REAL zero on the source meter. The crank
    // peripheral's CP callback flags it; loop() drains it into meter.requestZeroOffset() (forward-plan §10).
    crank.setZeroResetHandler([]() { g_pendZeroReset = true; });

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
    bootStage("pre-wifi");
    // Join WiFi + bring up OTA and the status HTTP server. The provider renders live state
    // from the ProxyCore each request (curl http://<ip>/ — the reliable window into the C3).
    wifi.begin("sb20proxy",
               [identity = cfg.spoofName,
                corrector = (cfg.mode == ProxyMode::Corrector)]() {
        ProxyStatus s;
        s.identity = identity;   // the OUT name we advertise (stable until a reboot)
        s.corrector = corrector;
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

    // Workout screen (GET /workout): the live engine + presets driven over the web UI. Loading is
    // live (no reboot — a workout is data, not identity) and persisted to NVS so it survives a
    // power-cycle. The FTMS erg write of g_wk's target is the next phase (bench-gated).
    {
        const std::string saved = WorkoutStore::load();
        if (!saved.empty()) g_wk.load(parseWorkout(saved));
    }
    // The /workout hooks share g_wk with the LCD task (USE_LCD builds) — take the UI mutex so an
    // HTTP control and a screen tap can never mutate the runtime concurrently. lcdLock() is a no-op
    // (null mutex) on non-LCD builds.
    wifi.setWorkoutUi(
        []() { lcdLock(); std::string j = g_wk.json(millis()); lcdUnlock(); return j; },
        [](const std::string& json) -> bool {
            Workout w = parseWorkout(json);
            if (w.segments.empty()) return false;  // reject malformed / empty
            lcdLock(); g_wk.load(w); lcdUnlock();
            WorkoutStore::save(json);
            return true;
        },
        [](const std::string& action) { lcdLock(); g_wk.control(action, millis()); lcdUnlock(); });

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

#if USE_LCD
    g_lcdMux = xSemaphoreCreateRecursiveMutex();
    g_lcdIdentity = cfg.spoofName;
    g_lcdCorrector = (cfg.mode == ProxyMode::Corrector);
    g_lcdMeterAddr = cfg.meterAddress;
    g_lcdSource = cfg.meterAddress.empty() ? cfg.meterNameFilter : cfg.meterAddress;
    g_lcdUi.brightness = 100;
    bootStage("pre-lcd");
    lcd.begin();
    bootStage("post-lcd");
#if defined(LCD_DRIVER_CYD) && LCD_DRIVER_CYD
    Serial.printf("[lcd] CYD %dx%d up (%d band%s); touch(XPT2046)=%s\n", LCD_W, LCD_H, LCD_BANDS,
                  LCD_BANDS > 1 ? "s" : "", lcd.touchAlive() ? "alive" : "DEAD");
    {   // stored touch calibration -> apply; none yet -> run the tap-the-crosshair ritual
        TouchCalFit f = touchCalLoad();
        if (f.valid) {
            lcd.setCal(f);
            Serial.printf("[tcal] loaded sx=%.4f ox=%.1f sy=%.4f oy=%.1f\n", (double)f.sx,
                          (double)f.ox, (double)f.sy, (double)f.oy);
        } else if (lcd.touchAlive()) {
            Serial.println("[tcal] no stored calibration - entering the ritual (CALTOUCH re-runs)");
            g_tcal = CalRitual{};
            g_tcal.active = true;
        }
    }
#else
    Serial.printf("[lcd] JD9853 %dx%d up; touch(AXS5106)=%s\n", LCD_W, LCD_H,
                  lcd.touchAlive() ? "alive" : "DEAD");
#endif
    // Render on core 1 so the SPI blit never competes with BLE/loop on core 0.
    xTaskCreatePinnedToCore(lcdTask, "lcd", 12288, nullptr, 2, nullptr, 1);
#endif
    bootStage("setup-done");
}

void loop() {
    ++g_loopBeat;                       // feed the stall watchdog (Phase B)
#ifdef S3_DIAG
    { static uint32_t hb = 0; if (millis() - hb >= 1000) { hb = millis();
        Serial.printf("[diag] loop alive t=%lus\n", (unsigned long)(millis() / 1000)); } }
#endif
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
    // Drain a pending zero-reset: the SB20/app asked our spoof to calibrate -> forward a REAL zero to the
    // source meter HERE in loop() (off the CP-write callback, so no re-entrant central BLE op).
    if (g_pendZeroReset) {
        g_pendZeroReset = false;
        meter.requestZeroOffset();
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

    // (OLED / LCD render on their own tasks — see oledTask / lcdTask — so neither blocks this loop.)

#if USE_LCD
    lcdSerialConsole();  // headless bench window: SCREEN / TAP x y / STATE over USB serial
    // sample the broadcast power into the Ride sparkline ring at ~2 Hz
    static uint32_t lastHist = 0;
    if (millis() - lastHist >= 500) {
        lastHist = millis();
        const int N = (int)(sizeof(g_lcdHist) / sizeof(g_lcdHist[0]));
        if (g_lcdHistN < N) g_lcdHist[g_lcdHistN++] = proxy.lastOutput().power_w;
        else {
            memmove(g_lcdHist, g_lcdHist + 1, (N - 1) * sizeof(g_lcdHist[0]));
            g_lcdHist[N - 1] = proxy.lastOutput().power_w;
        }
    }
#endif

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
