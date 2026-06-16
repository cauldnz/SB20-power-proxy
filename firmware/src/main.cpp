// sb20proxy ESP32 — dual-role BLE proxy (the only Arduino/NimBLE file).
//   real meter (BLE central) -> [correction] -> spoofed Stages crank (BLE peripheral) -> SB20
// Wires the platform-agnostic ProxyCore (lib/proxy) to the real BLE impls.

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "Config.h"
#include "Correction.h"
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
  #include <WiFi.h>
  #include "net/WifiLink.h"
#endif

using namespace sb20proxy;

static BleCrankPeripheral crank;
static ProxyCore proxy(meter, crank,
                       Correction{Config::CORRECTION_SCALE, Config::CORRECTION_OFFSET});

#if USE_WIFI
static WifiLink wifi;
#endif

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[sb20proxy] BLE crank proxy starting");

    NimBLEDevice::init(Config::SPOOF_NAME);
    proxy.begin();  // crank advertises; source begins (scan, or nothing for mock)

    Serial.printf("[sb20proxy] advertising as '%s'; source=%s\n",
                  Config::SPOOF_NAME, USE_MOCK_METER ? "MOCK" : Config::METER_NAME_FILTER);

#if USE_WIFI
    pinMode(Config::STATUS_LED_PIN, OUTPUT);  // onboard status LED (active-low)
    // Join WiFi + bring up OTA and the status HTTP server. The provider renders live state
    // from the ProxyCore each request (curl http://<ip>/ — the reliable window into the C3).
    wifi.begin("sb20proxy", []() {
        ProxyStatus s;
#if USE_MOCK_METER
        s.mock = true;
#endif
        s.forwarded = proxy.forwarded();
        s.lastPowerW = proxy.lastOutput().power_w;
        s.lastCadenceRpm = proxy.lastOutput().cadence_rpm;
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
#endif
}

void loop() {
    proxy.loop();

#if USE_WIFI
    wifi.handle();  // service HTTP + OTA, promote to healthy

    // Onboard status LED: fast blink = setup portal / joining, slow pulse = connected.
    const LinkState ls = wifi.isUp() ? LinkState::Connected : LinkState::Searching;
    digitalWrite(Config::STATUS_LED_PIN, StatusLed::lit(ls, millis()) ? LOW : HIGH);  // active-low
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
