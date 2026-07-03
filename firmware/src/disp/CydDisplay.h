#pragma once
// CydDisplay — the hardware seam for the AliExpress ESP32-2432S028R "Cheap Yellow Display"
// (classic ESP32-WROOM-32): 2.8" 240x320 TFT on HSPI + XPT2046 resistive touch (bit-banged on
// its own pins) + LEDC backlight. Same five-method interface as LcdDisplay (the S3 seam):
// begin / setBrightness / blit / readTap / touchAlive — main.cpp swaps the type by build flag.
//
// Panel: the 2-USB variant probes as ST7789-family (0xD3 reads all-zero => not ILI9341;
// probe 2026-07-03) and wants INVERSION ON. Init is generic MIPI-DCS, which drives both
// controllers; CYD_INVERT=0 un-inverts if a batch differs. blit() is band-aware: it windows
// the canvas's bandY0/bandH slice, so the no-PSRAM banded render path streams per band.
//
// Touch: XPT2046 (resistive) — pressure-gated, median-of-3 sampled, mapped to portrait panel
// coords with the community calibration for this board; tune the CYD_TP_* build flags if a
// unit's film differs. Edge-triggered like the S3 seam (fires once per press).
#if defined(USE_LCD) && USE_LCD

#include <Arduino.h>
#include <SPI.h>

#include "LcdCanvas.h"
#include "TouchCal.h"

#ifndef CYD_INVERT
#define CYD_INVERT 0        // owner's unit: INVOFF (the "CYD2USB wants INVON" folklore was wrong here)
#endif
#ifndef CYD_MADCTL
#define CYD_MADCTL 0x00     // portrait, RGB, no mirror (0x48 = MX|BGR showed mirrored+wrong colors)
#endif
// XPT2046 raw ranges (community calibration for the ESP32-2432S028R touch film).
#ifndef CYD_TP_XMIN
#define CYD_TP_XMIN 200
#endif
#ifndef CYD_TP_XMAX
#define CYD_TP_XMAX 3900
#endif
#ifndef CYD_TP_YMIN
#define CYD_TP_YMIN 240
#endif
#ifndef CYD_TP_YMAX
#define CYD_TP_YMAX 3900
#endif

namespace sb20proxy {

class CydDisplay {
public:
    // ESP32-2432S028R pin map (community PINS.md; verified by the cyd-probe firmware).
    static constexpr int PIN_MISO = 12, PIN_MOSI = 13, PIN_SCLK = 14, PIN_CS = 15;
    static constexpr int PIN_DC = 2, PIN_BL = 21;
    static constexpr int TP_CLK = 25, TP_MOSI = 32, TP_CS = 33, TP_IRQ = 36, TP_MISO = 39;
    static constexpr int LED_R = 4, LED_G = 16, LED_B = 17;  // on-board RGB LED, active LOW
    static constexpr int kBlChannel = 1;

    void begin() {
        pinMode(PIN_CS, OUTPUT);
        pinMode(PIN_DC, OUTPUT);
        digitalWrite(PIN_CS, HIGH);
        spi_.begin(PIN_SCLK, PIN_MISO, PIN_MOSI, -1);
        // --- panel: generic MIPI-DCS init (drives both the ST7789 + ILI9341 fits) ---
        spi_.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
        cmd_(0x01); delay(150);                  // SWRESET (the CYD ties TFT reset to EN)
        cmd_(0x11); delay(120);                  // SLPOUT
        cmd_(0x3A); dat_(0x55);                  // COLMOD: 16bpp
        cmd_(0x36); dat_(CYD_MADCTL);            // MADCTL (orientation + color order; photo-tuned)
        cmd_(CYD_INVERT ? 0x21 : 0x20);          // INVON/INVOFF (photo-tuned per unit)
        cmd_(0x29); delay(20);                   // DISPON
        spi_.endTransaction();
        // --- backlight: LEDC PWM (GPIO21) ---
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcAttach(PIN_BL, 5000, 10);
#else
        ledcSetup(kBlChannel, 5000, 10);
        ledcAttachPin(PIN_BL, kBlChannel);
#endif
        setBrightness(100);
        // --- RGB status LED off (active low) ---
        for (int p : {LED_R, LED_G, LED_B}) { pinMode(p, OUTPUT); digitalWrite(p, HIGH); }
        // --- touch: bit-banged XPT2046 ---
        pinMode(TP_CS, OUTPUT); pinMode(TP_CLK, OUTPUT); pinMode(TP_MOSI, OUTPUT);
        pinMode(TP_MISO, INPUT); pinMode(TP_IRQ, INPUT);
        digitalWrite(TP_CS, HIGH); digitalWrite(TP_CLK, LOW);
        // sanity: an untouched film reads ~0 pressure; all-ones means the wiring is wrong
        touchAlive_ = tpRead_(0xB1) < 4000;
    }

    void setBrightness(uint8_t pct) {
        if (pct > 100) pct = 100;
        const uint32_t duty = (uint32_t)pct * 1023 / 100;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcWrite(PIN_BL, duty);
#else
        ledcWrite(kBlChannel, duty);
#endif
    }

    // Push the canvas's band (or the whole frame when bandH==LCD_H) to the panel.
    void blit(const LcdCanvas& c) {
        spi_.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
        setWindow_(0, c.bandY0, LCD_W, c.bandH);
        digitalWrite(PIN_DC, HIGH);
        digitalWrite(PIN_CS, LOW);
        const uint8_t* p = (const uint8_t*)c.px.data();
        size_t left = (size_t)LCD_W * c.bandH * 2;
        while (left) {
            size_t chunk = left > 8192 ? 8192 : left;
            spi_.writePixels(p, chunk);  // swaps to the panel's big-endian 565
            p += chunk;
            left -= chunk;
        }
        digitalWrite(PIN_CS, HIGH);
        spi_.endTransaction();
    }

    // Poll one touch point; true once per NEW press (edge-triggered, like the S3 seam).
    bool readTap(int& outX, int& outY) {
        uint16_t rx, ry, z;
        bool down = readRaw(rx, ry, z);
        bool newTap = down && !wasDown_;
        wasDown_ = down;
        if (!newTap) return false;
        rawToScreen(rx, ry, outX, outY);
        return true;
    }

    // One pressure-gated raw sample (median-of-3 when pressed). True while the film is pressed.
    // Public so the calibration ritual (TouchCal.h + main.cpp) can collect raw points.
    bool readRaw(uint16_t& rx, uint16_t& ry, uint16_t& z) {
        z = tpRead_(0xB1);
        if (z <= 200 || z >= 4000) { tpRead_(0x90); return false; }
        rx = tpMedian3_(0xD1);
        ry = tpMedian3_(0x91);
        tpRead_(0x90);  // power down between frames
        return true;
    }

    // Map a raw sample to panel coords: the NVS-loaded least-squares fit when present, else the
    // compiled community defaults (CYD_TP_* — X runs opposite the panel on this film).
    void rawToScreen(uint16_t rx, uint16_t ry, int& outX, int& outY) const {
        if (cal_.valid) { touchCalApply(cal_, (float)rx, (float)ry, outX, outY); return; }
        int x = (int)((int32_t)(CYD_TP_XMAX - rx) * (LCD_W - 1) / (CYD_TP_XMAX - CYD_TP_XMIN));
        int y = (int)((int32_t)(ry - CYD_TP_YMIN) * (LCD_H - 1) / (CYD_TP_YMAX - CYD_TP_YMIN));
        if (x < 0) x = 0;
        if (x >= LCD_W) x = LCD_W - 1;
        if (y < 0) y = 0;
        if (y >= LCD_H) y = LCD_H - 1;
        outX = x;
        outY = y;
    }

    // Live panel tweaks (serial INV/MAD commands — dial orientation/colors without reflashing)
    void setInvert(bool on) {
        spi_.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
        cmd_(on ? 0x21 : 0x20);
        spi_.endTransaction();
    }
    void setMadctl(uint8_t v) {
        spi_.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
        cmd_(0x36); dat_(v);
        spi_.endTransaction();
    }

    void setCal(const TouchCalFit& f) { cal_ = f; }
    const TouchCalFit& cal() const { return cal_; }

    // --- LVGL seam: area blit + level-triggered touch state -------------------------------
    void blitArea(int x1, int y1, int x2, int y2, const uint16_t* px) {
        spi_.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
        setWindow_(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
        digitalWrite(PIN_DC, HIGH);
        digitalWrite(PIN_CS, LOW);
        const uint8_t* p = (const uint8_t*)px;
        size_t left = (size_t)(x2 - x1 + 1) * (y2 - y1 + 1) * 2;
        while (left) {
            size_t chunk = left > 8192 ? 8192 : left;
            spi_.writePixels(p, chunk);
            p += chunk;
            left -= chunk;
        }
        digitalWrite(PIN_CS, HIGH);
        spi_.endTransaction();
    }
    bool readTouchState(int& x, int& y) {  // true WHILE pressed (LVGL indev semantics)
        uint16_t rx, ry, z;
        if (!readRaw(rx, ry, z)) return false;
        rawToScreen(rx, ry, x, y);
        return true;
    }

    bool touchAlive() const { return touchAlive_; }

    // On-board RGB status LED (active low): handy boot/link indicator.
    void statusLed(bool r, bool g, bool b) {
        digitalWrite(LED_R, r ? LOW : HIGH);
        digitalWrite(LED_G, g ? LOW : HIGH);
        digitalWrite(LED_B, b ? LOW : HIGH);
    }

private:
    SPIClass spi_{HSPI};
    bool wasDown_ = false;
    bool touchAlive_ = false;
    TouchCalFit cal_;  // invalid until loaded/fit -> the compiled defaults apply

    void cmd_(uint8_t c) {
        digitalWrite(PIN_DC, LOW);
        digitalWrite(PIN_CS, LOW);
        spi_.transfer(c);
        digitalWrite(PIN_CS, HIGH);
    }
    void dat_(uint8_t d) {
        digitalWrite(PIN_DC, HIGH);
        digitalWrite(PIN_CS, LOW);
        spi_.transfer(d);
        digitalWrite(PIN_CS, HIGH);
    }
    void setWindow_(int x, int y, int w, int h) {
        cmd_(0x2A); dat_(x >> 8); dat_(x & 0xFF); dat_((x + w - 1) >> 8); dat_((x + w - 1) & 0xFF);
        cmd_(0x2B); dat_(y >> 8); dat_(y & 0xFF); dat_((y + h - 1) >> 8); dat_((y + h - 1) & 0xFF);
        cmd_(0x2C);
    }

    // XPT2046 on its own bit-banged bus (its pins aren't on the TFT's SPI peripheral).
    uint16_t tpRead_(uint8_t c) {
        uint16_t out = 0;
        digitalWrite(TP_CS, LOW);
        for (int i = 7; i >= 0; --i) {
            digitalWrite(TP_MOSI, (c >> i) & 1);
            digitalWrite(TP_CLK, HIGH);
            digitalWrite(TP_CLK, LOW);
        }
        for (int i = 15; i >= 0; --i) {
            digitalWrite(TP_CLK, HIGH);
            out = (uint16_t)((out << 1) | digitalRead(TP_MISO));
            digitalWrite(TP_CLK, LOW);
        }
        digitalWrite(TP_CS, HIGH);
        return (out >> 4) & 0x0FFF;
    }
    uint16_t tpMedian3_(uint8_t c) {
        uint16_t a = tpRead_(c), b = tpRead_(c), d = tpRead_(c);
        if (a > b) { uint16_t t = a; a = b; b = t; }
        if (b > d) b = d;
        return a > b ? a : b;
    }
};

}  // namespace sb20proxy
#endif  // USE_LCD
