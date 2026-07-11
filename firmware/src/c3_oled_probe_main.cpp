// c3_oled_probe_main.cpp — throwaway I2C pin scanner to find a C3 board's 0.96" OLED wiring.
//
// AliExpress "ESP32-C3 + 0.96 OLED" boards vary in which GPIOs the SSD1306/SH1106 sits on. This
// probe sweeps the common (SDA,SCL) candidate pairs, does a full I2C address scan on each, and
// flags 0x3C/0x3D (the OLED). Run it to learn the pins, then wire a real board variant (U3).
//
// Guarded by -DC3_OLED_PROBE so the file is INERT in every normal env (it compiles to nothing when
// the flag is absent — no setup()/loop() — so it can't clash with main.cpp). The `c3-oled-probe`
// env in platformio.ini sets the flag and excludes main.cpp. Flash with:
//   python code/scripts/flash_c3.py --env c3-oled-probe --port COMxx
#ifdef C3_OLED_PROBE
#include <Arduino.h>
#include <Wire.h>

// Common SDA,SCL pairs seen on C3 OLED boards (0.42" peff74 uses 5/6; many 0.96" use 8/9 or 5/6;
// some route to other low GPIOs). Both orders tried since silkscreens/listings disagree.
static const int8_t PAIRS[][2] = {
    {5, 6},  {6, 5},  {8, 9},  {9, 8},  {4, 5},  {5, 4},  {7, 6},  {6, 7},
    {18, 19},{19, 18},{0, 1},  {1, 0},  {2, 3},  {3, 2},  {20, 21},{21, 20}, {10, 8},
};

static void scanPair(int sda, int scl) {
    Wire.end();
    delay(5);
    if (!Wire.begin(sda, scl)) {
        Serial.printf("  SDA=%2d SCL=%2d -> Wire.begin failed\n", sda, scl);
        return;
    }
    delay(5);
    int found = 0;
    for (uint8_t a = 1; a < 127; ++a) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            const bool oled = (a == 0x3C || a == 0x3D);
            Serial.printf("  SDA=%2d SCL=%2d -> 0x%02X%s\n", sda, scl, a,
                          oled ? "   <-- SSD1306/SH1106 OLED" : "");
            ++found;
        }
    }
    if (!found) Serial.printf("  SDA=%2d SCL=%2d -> (nothing)\n", sda, scl);
}

void setup() {
    Serial.begin(115200);
    delay(3000);  // let the USB-CDC host attach before the first burst
    Serial.println("\n=== C3 0.96\" OLED I2C pin scan ===");
    Serial.println("(a device at 0x3C or 0x3D on some pair = the OLED's SDA/SCL)");
    for (auto& p : PAIRS) scanPair(p[0], p[1]);
    Serial.println("=== scan done ===");
}

void loop() {
    delay(8000);
    Serial.println("(idle — tap RESET to rescan)");
}
#endif  // C3_OLED_PROBE
