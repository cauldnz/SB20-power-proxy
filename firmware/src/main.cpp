// sb20proxy ESP32 — dual-role BLE proxy (the only Arduino/NimBLE file).
//   real meter (BLE central) -> [correction] -> spoofed Stages crank (BLE peripheral) -> SB20
// Wires the platform-agnostic ProxyCore (lib/proxy) to the real BLE impls.

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "Config.h"
#include "Correction.h"
#include "ProxyCore.h"
#include "ble/BleCrankPeripheral.h"

#if USE_MOCK_METER
  #include "MockMeter.h"
  static sb20proxy::MockMeter meter;
#else
  #include "ble/BleMeterClient.h"
  static sb20proxy::BleMeterClient meter;
#endif

using namespace sb20proxy;

static BleCrankPeripheral crank;
static ProxyCore proxy(meter, crank,
                       Correction{Config::CORRECTION_SCALE, Config::CORRECTION_OFFSET});

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[sb20proxy] BLE crank proxy starting");

    NimBLEDevice::init(Config::SPOOF_NAME);
    proxy.begin();  // crank advertises; source begins (scan, or nothing for mock)

    Serial.printf("[sb20proxy] advertising as '%s'; source=%s\n",
                  Config::SPOOF_NAME, USE_MOCK_METER ? "MOCK" : Config::METER_NAME_FILTER);
}

void loop() {
    proxy.loop();

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
