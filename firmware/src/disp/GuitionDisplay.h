#pragma once
// GuitionDisplay — hardware seam for the Guition JC3248W535 (ESP32-S3-N16R8: 16 MB flash, 8 MB OPI
// PSRAM, native USB): a 3.5" 320x480 IPS panel on an AXS15231B controller over QSPI, plus an
// AXS15231B capacitive touch controller over I2C. Compiled only when USE_LCD=1 (the esp32-guition
// envs). Same seam contract as LcdDisplay/CydDisplay — begin / setBrightness / blit / readTap /
// touchAlive / blitArea / readTouchState — main.cpp swaps the type by build flag (LCD_DRIVER_GUITION).
//
// The panel bus is QSPI, which the hand-rolled SPI seams don't speak. We drive it with the ESP-IDF
// **esp_lcd** panel API + the AXS15231B panel driver vendored under lib/esp_lcd_axs15231b (which wraps
// the QSPI command framing + the vendor init table). esp_lcd ships inside the arduino-esp32 core, so —
// unlike Arduino_GFX — there is no third-party version pincer against the core (guition-board.md).
// Touch is hand-rolled over Wire (the upstream component's I2C touch driver needs the esp_lcd_touch
// component, which arduino-esp32 doesn't bundle).
//
// Pin map + touch protocol verified against the board's community BSP (atomic14 JC3248W535 writeup,
// the F1ATB setup guide, Espressif esp_lcd_axs15231b) — see code/findings/guition-board.md:
//   QSPI display : CS=45 SCK=47 D0=21 D1=48 D2=40 D3=39 ; backlight = GPIO1 (LEDC PWM)
//   Touch (I2C @0x3B, 400 kHz) : SDA=4 SCL=8 INT=11 RST=12 ; read cmd B5 AB A5 5A 00 00 00 08 ..,
//                                head byte[1]=points, X=bytes[2..3], Y=bytes[4..5] (high nibble=flags)
// Gotchas baked in below: the AXS15231B QSPI path only addresses full frames from row 0, so LVGL runs
// in full-refresh mode (-DLCD_LVGL_FULL_REFRESH=1); esp_lcd sends pixel bytes verbatim, so LVGL's
// little-endian RGB565 is byte-swapped here (GUITION_RGB565_SWAP); and this component's disp_on_off
// callback is inverted (pass false to turn the panel ON — matches the vendor BSP).
#if defined(USE_LCD) && USE_LCD

#include <Arduino.h>
#include <Wire.h>

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_axs15231b.h"

#include "LcdCanvas.h"

#ifndef GUITION_RGB565_SWAP
#define GUITION_RGB565_SWAP 1  // LVGL renders little-endian RGB565; the panel wants byte-swapped. Flip to 0 if hues inv.
#endif

namespace sb20proxy {

class GuitionDisplay {
public:
    // QSPI display pins
    static constexpr int PIN_QSPI_CS = 45, PIN_QSPI_SCK = 47;
    static constexpr int PIN_QSPI_D0 = 21, PIN_QSPI_D1 = 48, PIN_QSPI_D2 = 40, PIN_QSPI_D3 = 39;
    static constexpr int PIN_BL = 1;
    // Touch pins (AXS15231B capacitive, I2C)
    static constexpr int PIN_SDA = 4, PIN_SCL = 8, PIN_TP_RST = 12, PIN_TP_INT = 11;
    static constexpr uint8_t TOUCH_ADDR = 0x3B;

    void begin() {
        // --- QSPI bus (SPI2) — one full frame per transfer ---
        spi_bus_config_t bus = {};
        bus.sclk_io_num = PIN_QSPI_SCK;
        bus.data0_io_num = PIN_QSPI_D0;
        bus.data1_io_num = PIN_QSPI_D1;
        bus.data2_io_num = PIN_QSPI_D2;
        bus.data3_io_num = PIN_QSPI_D3;
        bus.max_transfer_sz = (int)((size_t)LCD_W * STRIP_ROWS * 2 + 64);  // one strip per DMA transfer
        spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);

        // --- panel IO (QSPI): cmd on 32 bits, no DC pin, quad mode (mirrors AXS15231B_PANEL_IO_QSPI_CONFIG) ---
        esp_lcd_panel_io_spi_config_t io_cfg = {};
        io_cfg.cs_gpio_num = PIN_QSPI_CS;
        io_cfg.dc_gpio_num = -1;
        io_cfg.spi_mode = 3;
        io_cfg.pclk_hz = 40 * 1000 * 1000;
        io_cfg.trans_queue_depth = 10;
        io_cfg.lcd_cmd_bits = 32;
        io_cfg.lcd_param_bits = 8;
        io_cfg.flags.quad_mode = 1;
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io_);

        // --- panel: AXS15231B over QSPI, vendor default init table ---
        axs15231b_vendor_config_t vendor = {};
        vendor.flags.use_qspi_interface = 1;
        esp_lcd_panel_dev_config_t panel_cfg = {};
        panel_cfg.reset_gpio_num = -1;   // no dedicated panel RST pin on this board (SWRESET in init)
        panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_cfg.bits_per_pixel = 16;
        panel_cfg.vendor_config = &vendor;
        if (esp_lcd_new_panel_axs15231b(io_, &panel_cfg, &panel_) != ESP_OK) panel_ = nullptr;
        if (panel_) {
            esp_lcd_panel_reset(panel_);
            esp_lcd_panel_init(panel_);
            // NOTE: this component's disp_on_off is inverted — false turns the panel ON (vendor BSP).
            esp_lcd_panel_disp_on_off(panel_, false);
        }

        // --- backlight: LEDC PWM ---
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcAttach(PIN_BL, 5000, 10);
#else
        ledcSetup(kBlChannel, 5000, 10);
        ledcAttachPin(PIN_BL, kBlChannel);
#endif
        setBrightness(100);

        // --- touch: reset pulse, then I2C command reads ---
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
        const uint32_t duty = (uint32_t)pct * 1023 / 100;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcWrite(PIN_BL, duty);
#else
        ledcWrite(kBlChannel, duty);
#endif
    }

    // Canvas path (the Watty egg / GET /screen.bmp): push the whole framebuffer.
    void blit(const LcdCanvas& c) { pushFrame_(0, 0, LCD_W, LCD_H, c.px.data()); }

    // --- LVGL seam: area blit + level-triggered touch state -------------------------------
    // In full-refresh mode LVGL hands us the whole frame (0,0..W-1,H-1) each flush. esp_lcd's
    // draw_bitmap takes an EXCLUSIVE end coordinate, so pass x2+1/y2+1.
    void blitArea(int x1, int y1, int x2, int y2, const uint16_t* px) {
        pushFrame_(x1, y1, x2 + 1, y2 + 1, px);
    }

    bool readTouchState(int& x, int& y) {  // true WHILE pressed (LVGL indev semantics)
        if (!touchAlive_) return false;
        uint8_t buff[8] = {0};
        if (!readTouch_(buff, sizeof(buff))) return false;
        const uint8_t points = buff[1] & 0x0F;
        if (points == 0 || points > 5) return false;   // 0 = finger up; >5 = a garbage/stale frame
        int rx = ((buff[2] & 0x0F) << 8) | buff[3];
        int ry = ((buff[4] & 0x0F) << 8) | buff[5];
        if (rx >= LCD_W || ry >= LCD_H) return false;   // out-of-range = stale read, treat as no touch
        if (rx < 0) rx = 0;
        if (ry < 0) ry = 0;
        x = rx;
        y = ry;
        return true;
    }

    bool readTap(int& outX, int& outY) {
        int x, y;
        const bool down = readTouchState(x, y);
        const bool newTap = down && !wasDown_;
        wasDown_ = down;
        if (!newTap) return false;
        outX = x;
        outY = y;
        return true;
    }

    bool touchAlive() const { return touchAlive_; }

private:
    static constexpr int kBlChannel = 0;
    static constexpr int STRIP_ROWS = 40;   // rows per DMA transfer (320x40x2 = 25.6 KB, fits internal DMA)
    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    uint16_t* strip_ = nullptr;     // small INTERNAL-DMA scratch (one strip), lazily allocated
    bool wasDown_ = false;
    bool touchAlive_ = false;

    // Push a region to the panel in row-strips through a small INTERNAL-DMA scratch. Two reasons:
    //   * esp_lcd's SPI path needs a DMA-capable source. The LVGL frame buffer lives in PSRAM, and a
    //     full 300 KB transfer makes spi_master try to allocate a 300 KB *internal* DMA bounce buffer,
    //     which fails ("setup_dma_priv_buffer: Failed to allocate priv TX buffer"). A ~26 KB internal
    //     scratch per strip sidesteps that.
    //   * esp_lcd sends pixel bytes verbatim, but LVGL renders little-endian RGB565 while the panel
    //     wants big-endian — so we byte-swap while copying each strip (leaves the caller's buffer
    //     untouched, so GET /screen.bmp stays correct).
    // In QSPI the driver sends no RASET: draw_bitmap emits RAMWR for the row-0 strip and RAMWRC
    // (continue) for the rest, so sequential full-width strips starting at ys=0 land correctly — which
    // is exactly what LVGL full-refresh mode delivers (a single (0,0,W-1,H-1) flush per frame).
    void pushFrame_(int xs, int ys, int xe, int ye, const uint16_t* px) {
        if (!panel_ || xe <= xs || ye <= ys) return;
        const int w = xe - xs;
        if (!strip_) {
            strip_ = (uint16_t*)heap_caps_malloc((size_t)w * STRIP_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        }
        if (!strip_) {  // last-resort direct push (may fail on a large region, but don't crash)
            esp_lcd_panel_draw_bitmap(panel_, xs, ys, xe, ye, px);
            return;
        }
        for (int y = ys; y < ye; y += STRIP_ROWS) {
            const int rows = (y + STRIP_ROWS <= ye) ? STRIP_ROWS : (ye - y);
            const uint16_t* src = px + (size_t)(y - ys) * w;
            const size_t n = (size_t)w * rows;
#if GUITION_RGB565_SWAP
            for (size_t i = 0; i < n; ++i) strip_[i] = (uint16_t)((src[i] >> 8) | (src[i] << 8));
#else
            memcpy(strip_, src, n * 2);
#endif
            esp_lcd_panel_draw_bitmap(panel_, xs, y, xe, y + rows, strip_);
        }
    }

    // AXS15231B touch read: write the 11-byte read command, then read n bytes as a SEPARATE I2C
    // transaction (STOP between — the Arduino-3.x/IDF-5 "ng" I2C driver rejects a repeated-start).
    // Command + parse mirror the upstream esp_lcd_axs15231b touch driver.
    bool readTouch_(uint8_t* buf, size_t n) {
        static const uint8_t cmd[11] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00};
        Wire.beginTransmission(TOUCH_ADDR);
        Wire.write(cmd, sizeof(cmd));
        if (Wire.endTransmission(true) != 0) return false;
        if (Wire.requestFrom((int)TOUCH_ADDR, (int)n) != (int)n) return false;
        for (size_t i = 0; i < n; ++i) buf[i] = Wire.read();
        return true;
    }
    bool probeTouch_() {
        uint8_t buff[8] = {0};
        return readTouch_(buff, sizeof(buff));  // any ACKed read = the controller is alive
    }
};

}  // namespace sb20proxy
#endif  // USE_LCD
