// FTMS erg-client test firmware (env esp32c3-ftms-ergclient). Scans for an FTMS machine
// (the SB20-FTMS-Server board, or any 0x1826 device), connects, and drives Set Target Power
// through a cycle of targets (Request Control -> Start -> Set Target Power). The serial log
// shows the convergence. Bench-test only — the on-bike analogue writes to the real SB20.

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "ble/FtmsErgClient.h"

using namespace sb20proxy;

static FtmsErgClient client;
static const int16_t kTargets[] = {100, 200, 150};
static int targetIdx = 0;
static uint32_t lastChange = 0, lastLog = 0;

void setup() {
    Serial.begin(115200);
    delay(300);
    NimBLEDevice::init("SB20-FTMS-Client");
    client.begin("SB20-FTMS-Server");  // match our test server (any FTMS by 0x1826 if renamed)
    client.setDesiredPower(kTargets[0]);
    Serial.println("[ftms-client] scanning for an FTMS machine (SB20-FTMS-Server)");
}

void loop() {
    const uint32_t now = millis();
    if (now - lastChange >= 8000) {  // cycle the target every 8 s
        lastChange = now;
        targetIdx = (targetIdx + 1) % 3;
        client.setDesiredPower(kTargets[targetIdx]);
    }
    client.loop();
    if (now - lastLog >= 1000) {
        lastLog = now;
        Serial.printf("[ftms-client] connected=%d controlled=%d desired=%dW lastSent=%dW\n",
                      client.connected(), client.controlled(), kTargets[targetIdx],
                      client.lastSent());
    }
    delay(10);
}
