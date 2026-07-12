#pragma once
// SSD1306 OLED hardware seam for the ESP32-C3 OLED boards. Two panels are supported via build flags:
//   - default: the 0.42" 72x40 (peff74 board) — the exact recipe raedian-probe proved stable.
//   - -DOLED_128X64=1: the 0.96" 128x64 (AliExpress C3+OLED board) — bigger fonts, more spacing.
// I2C pins default to SCL=6 / SDA=5 (both boards seen so far) and are overridable with
// -DOLED_SCL_PIN / -DOLED_SDA_PIN (confirm with the c3-oled-probe env if a board differs).
// Compiled only when USE_OLED=1. The pure row layout lives in lib/proxy/OledScreen.h (host-tested);
// this file is the hardware seam (U8g2 + Wire), exercised only on the bench like the BLE radio.
#if defined(USE_OLED) && USE_OLED

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

#include "OledScreen.h"

#ifndef OLED_SCL_PIN
#define OLED_SCL_PIN 6
#endif
#ifndef OLED_SDA_PIN
#define OLED_SDA_PIN 5
#endif

namespace sb20proxy {

class OledDisplay {
public:
    // Both U8g2 panel types take the same ctor args (rotation, reset, clock=SCL, data=SDA).
    OledDisplay()
        : oled_(U8G2_R0, /*reset=*/U8X8_PIN_NONE, /*clock=SCL=*/OLED_SCL_PIN, /*data=SDA=*/OLED_SDA_PIN) {}

    // Init the panel. NO I2C presence-probe on purpose: raedian-probe found the 0x3C probe NACKs
    // on the 0.42" board's marginal onboard bus (false negative -> blank screen), so we init + draw
    // unconditionally (its commit 20e6f83). 50 kHz because that bus locks up higher; Wire.setTimeOut
    // bounds any single transaction so a glitch can never hang the loop. (The 0.96" board tolerates
    // faster, but 50 kHz is the safe common floor — a ~1 Hz status refresh doesn't need more.)
    void begin() {
        oled_.setBusClock(50000);
        oled_.begin();          // inits Wire on the constructor pins
        Wire.setTimeOut(50);    // never hang on a bus glitch
        oled_.setContrast(255);
        oled_.setFontMode(1);
    }

    // Draw 4 pre-formatted rows (from the pure formatOledLines). Runs on a dedicated OLED task
    // (render-on-change) since the full-buffer I2C send blocks the calling task.
    void drawLines(const std::array<std::string, 4>& lines) {
        oled_.clearBuffer();
#if defined(OLED_128X64) && OLED_128X64
        // 0.96" 128x64: bigger fonts spread across the taller panel.
        oled_.setFont(u8g2_font_8x13B_tf);             // title row (bold)
        oled_.drawStr(0, 12, lines[0].c_str());
        oled_.setFont(u8g2_font_7x13_tf);              // detail rows
        oled_.drawStr(0, 31, lines[1].c_str());
        oled_.drawStr(0, 47, lines[2].c_str());
        oled_.drawStr(0, 63, lines[3].c_str());
#else
        // 0.42" 72x40: compact 4 rows in 40 px.
        oled_.setFont(u8g2_font_6x10_tf);              // title row
        oled_.drawStr(0, 9, lines[0].c_str());
        oled_.setFont(u8g2_font_5x7_tf);               // detail rows
        oled_.drawStr(0, 20, lines[1].c_str());
        oled_.drawStr(0, 29, lines[2].c_str());
        oled_.drawStr(0, 38, lines[3].c_str());
#endif
        oled_.sendBuffer();
    }

private:
#if defined(OLED_128X64) && OLED_128X64
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled_;
#else
    U8G2_SSD1306_72X40_ER_F_HW_I2C oled_;
#endif
};

}  // namespace sb20proxy
#endif  // USE_OLED
