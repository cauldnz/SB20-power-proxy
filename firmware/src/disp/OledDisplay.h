#pragma once
// 0.42" SSD1306 (72x40) OLED for the peff74 ESP32-C3 OLED board — the exact recipe raedian-probe
// proved stable on this hardware. Compiled only when USE_OLED=1 (the esp32c3-oled env). The pure
// row layout lives in lib/proxy/OledScreen.h (host-tested); this file is the hardware seam
// (U8g2 + Wire) and is exercised only on the bench, like the BLE radio.
#if defined(USE_OLED) && USE_OLED

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

#include "OledScreen.h"

namespace sb20proxy {

class OledDisplay {
public:
    // Native 72x40 SSD1306, HW I2C, explicit pins for THIS board: clock(SCL)=6, data(SDA)=5.
    OledDisplay() : oled_(U8G2_R0, /*reset=*/U8X8_PIN_NONE, /*clock=SCL=*/6, /*data=SDA=*/5) {}

    // Init the panel. NO I2C presence-probe on purpose: raedian-probe found the 0x3C probe NACKs
    // on this board's marginal onboard bus (false negative -> blank screen), so we init + draw
    // unconditionally (its commit 20e6f83). 50 kHz because the bus locks up higher; Wire.setTimeOut
    // bounds any single transaction so a glitch can never hang the loop.
    void begin() {
        oled_.setBusClock(50000);
        oled_.begin();          // inits Wire on the constructor pins (SCL=6, SDA=5)
        Wire.setTimeOut(50);    // never hang on a bus glitch
        oled_.setContrast(255);
        oled_.setFontMode(1);
    }

    void render(OledMode mode, const String& ip, int watts, int cadenceRpm) {
        auto lines = formatOledLines(mode, std::string(ip.c_str()), watts, cadenceRpm);
        oled_.clearBuffer();
        oled_.setFont(u8g2_font_6x10_tf);              // title row
        oled_.drawStr(0, 9, lines[0].c_str());
        oled_.setFont(u8g2_font_5x7_tf);               // detail rows
        oled_.drawStr(0, 20, lines[1].c_str());
        oled_.drawStr(0, 29, lines[2].c_str());
        oled_.drawStr(0, 38, lines[3].c_str());
        oled_.sendBuffer();
    }

private:
    U8G2_SSD1306_72X40_ER_F_HW_I2C oled_;
};

}  // namespace sb20proxy
#endif  // USE_OLED
