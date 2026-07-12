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
#include <U8g2lib.h>

// Common SDA,SCL pairs seen on C3 OLED boards (0.42" peff74 uses 5/6; many 0.96" use 8/9 or 5/6;
// some route to other low GPIOs). Both orders tried since silkscreens/listings disagree.
static const int8_t PAIRS[][2] = {
    {5, 6},  {6, 5},  {8, 9},  {9, 8},  {4, 5},  {5, 4},  {7, 6},  {6, 7},
    {18, 19},{19, 18},{0, 1},  {1, 0},  {2, 3},  {3, 2},  {20, 21},{21, 20}, {10, 8},
};

// Some C3 boards (this 0.96") don't deliver Serial to the host over USB-Serial-JTAG, so ALSO report
// the answer ON the OLED itself: the first pair that has a device at 0x3C/0x3D gets a 128x64 SSD1306
// inited on it, showing the pins. If it lights up, read SDA/SCL off the screen.
static int found_sda = -1, found_scl = -1;
static uint8_t found_addr = 0;

static bool scanPair(int sda, int scl) {
    Wire.end();
    delay(5);
    if (!Wire.begin(sda, scl)) {
        Serial.printf("  SDA=%2d SCL=%2d -> Wire.begin failed\n", sda, scl);
        return false;
    }
    delay(5);
    bool oledHere = false;
    for (uint8_t a = 1; a < 127; ++a) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            const bool oled = (a == 0x3C || a == 0x3D);
            Serial.printf("  SDA=%2d SCL=%2d -> 0x%02X%s\n", sda, scl, a,
                          oled ? "   <-- SSD1306/SH1106 OLED" : "");
            if (oled && found_sda < 0) { found_sda = sda; found_scl = scl; found_addr = a; oledHere = true; }
        }
    }
    if (found_sda < 0 && !oledHere) Serial.printf("  SDA=%2d SCL=%2d -> (nothing)\n", sda, scl);
    return oledHere;
}

void setup() {
    Serial.begin(115200);
    delay(3000);  // let the USB-CDC host attach before the first burst
    Serial.println("\n=== C3 0.96\" OLED I2C pin scan ===");
    Serial.println("(a device at 0x3C or 0x3D on some pair = the OLED's SDA/SCL)");
    for (auto& p : PAIRS) { if (scanPair(p[0], p[1])) break; }
    Serial.println("=== scan done ===");

    if (found_sda >= 0) {
        Serial.printf(">>> OLED at 0x%02X on SDA=%d SCL=%d — showing on the panel\n",
                      found_addr, found_sda, found_scl);
        // U8g2 HW-I2C ctor: (rotation, reset, clock=SCL, data=SDA), pins are runtime.
        static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(
            U8G2_R0, U8X8_PIN_NONE, (uint8_t)found_scl, (uint8_t)found_sda);
        oled.setBusClock(50000);
        oled.begin();
        oled.setContrast(255);
        oled.clearBuffer();
        oled.setFont(u8g2_font_8x13B_tf);
        oled.drawStr(0, 12, "OLED FOUND");
        char b[24];
        oled.setFont(u8g2_font_7x13_tf);
        snprintf(b, sizeof(b), "addr 0x%02X", found_addr); oled.drawStr(0, 31, b);
        snprintf(b, sizeof(b), "SDA=%d", found_sda);       oled.drawStr(0, 47, b);
        snprintf(b, sizeof(b), "SCL=%d", found_scl);       oled.drawStr(0, 63, b);
        oled.sendBuffer();
    }
}

void loop() {
    delay(8000);
    if (found_sda >= 0)
        Serial.printf("(OLED @ 0x%02X SDA=%d SCL=%d — on the panel)\n", found_addr, found_sda, found_scl);
    else
        Serial.println("(no OLED found at 0x3C/0x3D on any candidate pair — tap RESET to rescan)");
}
#endif  // C3_OLED_PROBE
