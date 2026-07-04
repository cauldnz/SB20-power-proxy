// firmware-nrf bring-up probe: prove CDC serial, the RGB LED, BLE advertising, and the
// LSM6DS3TR-C IMU in one flash before any real feature lands (the same discipline as the
// ESP32 boards' probe firmwares — hardware facts first).
#include <Arduino.h>
#include <bluefruit.h>

#include "LSM6DS3.h"
#include "Wire.h"

static LSM6DS3 imu(I2C_MODE, 0x6A);  // the Sense's LSM6DS3TR-C on the internal I2C bus
static bool imuOk = false;

void setup() {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 4000) delay(10);  // CDC needs a host; don't block headless

    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_RED, HIGH);    // active low
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);

    Serial.println("[nrf] XIAO nRF52840 Sense bring-up probe");

    // IMU: the Sense gates the LSM6DS3's power from a GPIO — turn it on before probing.
#ifdef PIN_LSM6DS3TR_C_POWER
    pinMode(PIN_LSM6DS3TR_C_POWER, OUTPUT);
    digitalWrite(PIN_LSM6DS3TR_C_POWER, HIGH);
    delay(50);
#endif
    imuOk = (imu.begin() == 0);
    Serial.printf("[nrf] IMU LSM6DS3TR-C: %s\n", imuOk ? "OK" : "NOT FOUND");

    // BLE: advertise a placeholder identity to prove the stack.
    Bluefruit.begin(/*peripheral*/ 1, /*central*/ 1);  // dual role, like the ESP32 proxies
    Bluefruit.setName("SB20-nRF");
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.start(0);
    Serial.println("[nrf] BLE advertising as 'SB20-nRF' (dual role up)");
}

void loop() {
    static uint32_t last = 0;
    if (millis() - last >= 1000) {
        last = millis();
        digitalWrite(LED_GREEN, LOW);
        delay(30);
        digitalWrite(LED_GREEN, HIGH);
        if (imuOk) {
            Serial.printf("[nrf] accel g=(%.2f, %.2f, %.2f) t=%lus\n",
                          imu.readFloatAccelX(), imu.readFloatAccelY(), imu.readFloatAccelZ(),
                          (unsigned long)(millis() / 1000));
        } else {
            Serial.printf("[nrf] alive t=%lus (no IMU)\n", (unsigned long)(millis() / 1000));
        }
    }
    delay(5);
}
