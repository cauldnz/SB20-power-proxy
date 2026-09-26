// FTMS trainer-server test firmware (env esp32c3-ftms-server). Presents the ESP32 as an
// FTMS Fitness Machine: streams a mock Indoor Bike Data ramp and accepts Set Target Power
// on the Control Point. A real BLE controller (the host bleak loop, or our FtmsErgClient on
// the other board) drives it; the serial log shows the received target. Bench-test only.

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "ble/FtmsTrainerServer.h"
#if USE_OLED
#include "SimScreen.h"
#include "disp/OledDisplay.h"
#endif

using namespace sb20proxy;

static FtmsTrainerServer server;
#if USE_OLED
// Without this the board is indistinguishable from a dead one - and on a board that shipped with
// demo firmware it keeps showing THAT, because an OLED retains its last frame when nothing drives
// it. With a box of identical C3s on the bench, "which one is the simulator?" is a question worth
// answering by looking (2026-09-25).
static OledDisplay oled;
static std::array<std::string, 4> lastRows;
#endif
static uint32_t lastPub = 0, lastLog = 0;
static int16_t mockPower = 120;
static int16_t dir = 5;

void setup() {
    Serial.begin(115200);
    delay(300);
    NimBLEDevice::init("SB20-FTMS-Server");
    server.begin("SB20-FTMS-Server");
    Serial.println("[ftms-server] advertising as SB20-FTMS-Server (FTMS 0x1826)");
#if USE_OLED
    oled.begin();
    lastRows = formatSimOledLines(false, false, false, 0, 0);
    oled.drawLines(lastRows);   // claim the panel immediately: whatever was there is not us
#endif
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
#if USE_OLED
        // Render on change only: the full-buffer I2C send at 50 kHz is slow, and this loop also
        // has a 2 Hz notify to keep.
        const auto rows = formatSimOledLines(server.controlled(), server.started(),
                                             server.hasTarget(), server.targetPower(), mockPower);
        if (rows != lastRows) {
            lastRows = rows;
            oled.drawLines(rows);
        }
#endif
    }
    delay(10);
}
