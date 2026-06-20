// FTMS trainer-server test firmware (env esp32c3-ftms-server). Presents the ESP32 as an
// FTMS Fitness Machine: streams a mock Indoor Bike Data ramp and accepts Set Target Power
// on the Control Point. A real BLE controller (the host bleak loop, or our FtmsErgClient on
// the other board) drives it; the serial log shows the received target. Bench-test only.

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "ble/FtmsTrainerServer.h"

using namespace sb20proxy;

static FtmsTrainerServer server;
static uint32_t lastPub = 0, lastLog = 0;
static int16_t mockPower = 120;
static int16_t dir = 5;

void setup() {
    Serial.begin(115200);
    delay(300);
    NimBLEDevice::init("SB20-FTMS-Server");
    server.begin("SB20-FTMS-Server");
    Serial.println("[ftms-server] advertising as SB20-FTMS-Server (FTMS 0x1826)");
}

void loop() {
    const uint32_t now = millis();
    if (now - lastPub >= 500) {  // 2 Hz Indoor Bike Data
        lastPub = now;
        mockPower += dir;
        if (mockPower >= 300 || mockPower <= 100) dir = -dir;
        server.publishPower(mockPower, 90.0f);
    }
    if (now - lastLog >= 1000) {
        lastLog = now;
        Serial.printf("[ftms-server] controlled=%d started=%d hasTarget=%d target=%dW power=%dW\n",
                      server.controlled(), server.started(), server.hasTarget(),
                      server.targetPower(), mockPower);
    }
    delay(10);
}
