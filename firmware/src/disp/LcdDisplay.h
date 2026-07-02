#pragma once
// Waveshare ESP32-S3-Touch-LCD-1.47 hardware seam: JD9853 172x320 LCD over SPI2 + AXS5106
// capacitive touch over I2C. Compiled only when USE_LCD=1 (the esp32s3-touch envs). The pure
// UI (lib/proxy/LcdCanvas.h + LcdUi.h) renders into a host-testable framebuffer; this class
// only (a) initialises the panel, (b) blasts the finished buffer over SPI, (c) reads touches.
//
// Pin map + init sequence: the board's BSP (Waveshare demo / strnad's ESP32-S3-Touch-LCD-1.47
// template, esp_lcd_jd9853 + esp_lcd_touch_axs5106) — register-level facts re-implemented for
// the Arduino core; verified against the live panel via GET /screen.bmp.
#if defined(USE_LCD) && USE_LCD

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include "LcdCanvas.h"

namespace sb20proxy {

class LcdDisplay {
public:
    // LCD pins (SPI2/FSPI)
    static constexpr int PIN_SCLK = 38, PIN_MOSI = 39, PIN_CS = 21, PIN_DC = 45, PIN_RST = 40;
    static constexpr int PIN_BL = 46;   // backlight, LEDC PWM
    // Touch pins (I2C) — AXS5106 at 0x63
    static constexpr int PIN_SDA = 42, PIN_SCL = 41, PIN_TP_RST = 48, PIN_TP_INT = 47;
    static constexpr uint8_t TOUCH_ADDR = 0x63;
    // The JD9853's RAM window for this 172-wide panel starts at column 34 (vendor init).
    static constexpr int COL_OFFSET = 34, ROW_OFFSET = 0;

    void begin() {
        // --- panel ---
        pinMode(PIN_CS, OUTPUT);
        pinMode(PIN_DC, OUTPUT);
        pinMode(PIN_RST, OUTPUT);
        digitalWrite(PIN_CS, HIGH);
        digitalWrite(PIN_RST, HIGH);
        delay(10);
        digitalWrite(PIN_RST, LOW);
        delay(10);
        digitalWrite(PIN_RST, HIGH);
        delay(120);
        SPI.begin(PIN_SCLK, /*miso=*/-1, PIN_MOSI, PIN_CS);
        sendInit_();
        // --- backlight: LEDC PWM (5 kHz / 10-bit, per the BSP) ---
        ledcSetup(kBlChannel, 5000, 10);
        ledcAttachPin(PIN_BL, kBlChannel);
        setBrightness(100);
        // --- touch: reset pulse, then plain register reads ---
        pinMode(PIN_TP_RST, OUTPUT);
        pinMode(PIN_TP_INT, INPUT);
        digitalWrite(PIN_TP_RST, LOW);
        delay(10);
        digitalWrite(PIN_TP_RST, HIGH);
        delay(10);
        Wire.begin(PIN_SDA, PIN_SCL, 400000);
        Wire.setTimeOut(50);
        touchAlive_ = probeTouch_();
    }

    void setBrightness(uint8_t pct) {
        if (pct > 100) pct = 100;
        ledcWrite(kBlChannel, (uint32_t)pct * 1023 / 100);
    }

    // Push the whole canvas to the panel (~22 ms at 40 MHz). Call from a dedicated task.
    void blit(const LcdCanvas& c) {
        setWindow_(0, 0, LCD_W, LCD_H);
        SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
        digitalWrite(PIN_DC, HIGH);
        digitalWrite(PIN_CS, LOW);
        const uint8_t* p = (const uint8_t*)c.px.data();
        size_t left = c.px.size() * 2;
        while (left) {
            size_t chunk = left > 8192 ? 8192 : left;
            SPI.writePixels(p, chunk);  // swaps to the panel's big-endian 565
            p += chunk;
            left -= chunk;
        }
        digitalWrite(PIN_CS, HIGH);
        SPI.endTransaction();
    }

    // Poll one touch point. Returns true on a NEW press (edge-triggered by pressure of use:
    // the AXS5106 reports points only while touched; we edge-detect so a hold isn't repeat-fire).
    // Coordinates are panel-space (x mirrored per the BSP's rotation-0 config).
    bool readTap(int& outX, int& outY) {
        if (!touchAlive_) return false;
        uint8_t d[14] = {0};
        if (!readRegs_(0x01, d, sizeof(d))) return false;
        uint8_t points = d[1] & 0x0F;
        bool down = points > 0;
        bool newTap = down && !wasDown_;
        wasDown_ = down;
        if (!newTap) return false;
        int rawX = ((d[2] & 0x0F) << 8) | d[3];
        int rawY = ((d[4] & 0x0F) << 8) | d[5];
        int x = (LCD_W - 1) - rawX;  // BSP rotation-0: mirror_x
        int y = rawY;
        if (x < 0) x = 0;
        if (x >= LCD_W) x = LCD_W - 1;
        if (y < 0) y = 0;
        if (y >= LCD_H) y = LCD_H - 1;
        outX = x;
        outY = y;
        return true;
    }

    bool touchAlive() const { return touchAlive_; }

private:
    static constexpr int kBlChannel = 0;
    bool wasDown_ = false;
    bool touchAlive_ = false;

    void cmd_(uint8_t c) {
        digitalWrite(PIN_DC, LOW);
        digitalWrite(PIN_CS, LOW);
        SPI.write(c);
        digitalWrite(PIN_CS, HIGH);
    }
    void data_(const uint8_t* d, size_t n) {
        if (!n) return;
        digitalWrite(PIN_DC, HIGH);
        digitalWrite(PIN_CS, LOW);
        SPI.writeBytes(d, n);
        digitalWrite(PIN_CS, HIGH);
    }

    void sendInit_() {
        // The JD9853 vendor init for this panel (RGB565, TE on, window 34..205 x 0..319),
        // + MADCTL portrait/RGB + INVON (the BSP inverts) + DISPON.
        struct Cmd { uint8_t cmd; uint8_t len; uint8_t d[32]; uint16_t delayMs; };
        static const Cmd seq[] = {
            {0x11, 0, {}, 120},
            {0xDF, 2, {0x98, 0x53}, 0},
            {0xDF, 2, {0x98, 0x53}, 0},
            {0xB2, 1, {0x23}, 0},
            {0xB7, 4, {0x00, 0x47, 0x00, 0x6F}, 0},
            {0xBB, 6, {0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0}, 0},
            {0xC0, 2, {0x44, 0xA4}, 0},
            {0xC1, 1, {0x16}, 0},
            {0xC3, 8, {0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77}, 0},
            {0xC4, 12, {0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82}, 0},
            {0xC8, 32, {0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17,
                        0x12, 0x0D, 0x04, 0x00, 0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
                        0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00}, 0},
            {0xD0, 5, {0x04, 0x06, 0x6B, 0x0F, 0x00}, 0},
            {0xD7, 2, {0x00, 0x30}, 0},
            {0xE6, 1, {0x14}, 0},
            {0xDE, 1, {0x01}, 0},
            {0xB7, 5, {0x03, 0x13, 0xEF, 0x35, 0x35}, 0},
            {0xC1, 3, {0x14, 0x15, 0xC0}, 0},
            {0xC2, 2, {0x06, 0x3A}, 0},
            {0xC4, 2, {0x72, 0x12}, 0},
            {0xBE, 1, {0x00}, 0},
            {0xDE, 1, {0x02}, 0},
            {0xE5, 3, {0x00, 0x02, 0x00}, 0},
            {0xE5, 3, {0x01, 0x02, 0x00}, 0},
            {0xDE, 1, {0x00}, 0},
            {0x35, 1, {0x00}, 0},                        // TE on
            {0x3A, 1, {0x05}, 0},                        // RGB565
            {0x2A, 4, {0x00, 0x22, 0x00, 0xCD}, 0},      // cols 34..205
            {0x2B, 4, {0x00, 0x00, 0x01, 0x3F}, 0},      // rows 0..319
            {0xDE, 1, {0x02}, 0},
            {0xE5, 3, {0x00, 0x02, 0x00}, 0},
            {0xDE, 1, {0x00}, 0},
            {0x36, 1, {0x00}, 0},                        // MADCTL: portrait, RGB
            {0x21, 0, {}, 0},                            // INVON (panel is inverted)
            {0x29, 0, {}, 20},                           // display on
        };
        SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
        for (const auto& s : seq) {
            cmd_(s.cmd);
            data_(s.d, s.len);
            if (s.delayMs) delay(s.delayMs);
        }
        SPI.endTransaction();
    }

    void setWindow_(int x, int y, int w, int h) {
        const int x0 = x + COL_OFFSET, x1 = x + w - 1 + COL_OFFSET;
        const int y0 = y + ROW_OFFSET, y1 = y + h - 1 + ROW_OFFSET;
        const uint8_t ca[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
        const uint8_t ra[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
        SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
        cmd_(0x2A); data_(ca, 4);
        cmd_(0x2B); data_(ra, 4);
        cmd_(0x2C);
        SPI.endTransaction();
    }

    bool probeTouch_() {
        Wire.beginTransmission(TOUCH_ADDR);
        Wire.write((uint8_t)0x01);
        if (Wire.endTransmission(false) != 0) return false;
        return Wire.requestFrom((int)TOUCH_ADDR, 1) == 1;
    }
    bool readRegs_(uint8_t reg, uint8_t* buf, size_t n) {
        Wire.beginTransmission(TOUCH_ADDR);
        Wire.write(reg);
        if (Wire.endTransmission(false) != 0) return false;
        if (Wire.requestFrom((int)TOUCH_ADDR, (int)n) != (int)n) return false;
        for (size_t i = 0; i < n; ++i) buf[i] = Wire.read();
        return true;
    }
};

}  // namespace sb20proxy
#endif  // USE_LCD
