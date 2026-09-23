#pragma once
// GuitionDisplay — hardware seam for the Guition JC3248W535 (ESP32-S3-N16R8: 16 MB flash, 8 MB OPI
// PSRAM, native USB): a 3.5" 320x480 IPS panel on an AXS15231B controller over QSPI, plus an
// AXS15231B capacitive touch controller over I2C. Compiled only when USE_LCD=1 (the esp32-guition
// envs). Same seam contract as LcdDisplay/CydDisplay — begin / setBrightness / blit / readTap /
// touchAlive / blitArea / readTouchState — main.cpp swaps the type by build flag (LCD_DRIVER_GUITION).
//
// NOTE ON THE NAME: "JC3248W535" is off the owner's AliExpress order — NOT read from the PCB, and
// not self-reported by any ESP32 board (303A:1001 is Espressif's generic S3 USB ID); the variant
// suffix is unknown. What this file actually depends on is the pin map + AXS15231B controller
// below, both confirmed on the hardware. See findings/guition-board.md ("Provenance").
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

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_axs15231b.h"

#include "GuitionInitCmds.h"  // this board's AXS15231B init table (NOT the driver's generic default)
#include "LcdCanvas.h"

#ifndef GUITION_RGB565_SWAP
// The panel wants big-endian RGB565 (the vendor BSP builds LVGL with LV_COLOR_16_SWAP=1); LVGL here
// renders little-endian, so swap while copying each strip. Flip to 0 only if colours come out mangled.
#define GUITION_RGB565_SWAP 1
#endif
#ifndef GUITION_FRAME_STATS
// Per-frame timing to serial (TE wait, transfer us vs the panel's ~13 ms refresh, frame count).
// Bring-up only - on a live UI this would log every 2 s.
#define GUITION_FRAME_STATS 0
#endif
#ifndef GUITION_TE_SYNC
// Start each frame on the panel's tear-effect (TE, GPIO38) falling edge, as the vendor BSP does. The
// AXS15231B QSPI panel garbles detailed content written across its refresh (solid colours hide it);
// syncing the write start to TE is what the known-good esp_lcd BSP does with this exact driver path.
#define GUITION_TE_SYNC 1
#endif
#ifndef GUITION_PANEL_SELFTEST
// Bring-up aid for a NEW panel/board (off by default: it costs ~4.5 s of boot). Build with
// -DGUITION_PANEL_SELFTEST=1 to show RED/GREEN/BLUE/WHITE bars (top->bottom, 1.5 s) then a solid
// white screen (3 s) before LVGL starts. One look tells you a lot:
//   bars R,G,B,W = pixels land, orientation + colour order + byte order all correct
//   bars B,G,R,W = RGB element order swapped (use LCD_RGB_ELEMENT_ORDER_BGR)
//   dark B,R,G,W = byte order wrong (flip GUITION_RGB565_SWAP)
//   solid white  = a once-written static frame: anything visible there is the panel's own analog
//                  behaviour, not our data, addressing or write timing.
#define GUITION_PANEL_SELFTEST 0
#endif

namespace sb20proxy {

class GuitionDisplay {
public:
    // QSPI display pins
    static constexpr int PIN_QSPI_CS = 45, PIN_QSPI_SCK = 47;
    static constexpr int PIN_QSPI_D0 = 21, PIN_QSPI_D1 = 48, PIN_QSPI_D2 = 40, PIN_QSPI_D3 = 39;
    static constexpr int PIN_BL = 1;
    static constexpr int PIN_TE = 38;  // panel tear-effect output (vendor BSP: falling edge, pull-up)
    // Touch pins (AXS15231B capacitive, I2C)
    static constexpr int PIN_SDA = 4, PIN_SCL = 8, PIN_TP_RST = 12, PIN_TP_INT = 11;
    static constexpr uint8_t TOUCH_ADDR = 0x3B;

    void begin() {
        // --- QSPI bus (SPI2) — sized for one strip per DMA transfer (see pushFrame_) ---
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
        // Colour transfers are queued ASYNCHRONOUSLY; this callback (ISR) signals each one finished, so
        // pushFrame_ never queues the next strip while the previous is still on the wire (vendor BSP).
        colorDone_ = xSemaphoreCreateBinary();
        if (colorDone_) xSemaphoreGive(colorDone_);
        io_cfg.on_color_trans_done = &GuitionDisplay::onColorDone_;
        io_cfg.user_ctx = this;
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io_);

        // --- panel: AXS15231B over QSPI, THIS BOARD's init table ---
        // Never the driver's built-in default: it's for a different AXS15231B panel and ends with 0x22
        // (all pixels off) — boot noise, then a black screen with the backlight on (GuitionInitCmds.h).
        axs15231b_vendor_config_t vendor = {};
        vendor.init_cmds = guition::kInitCmds;
        vendor.init_cmds_size = guition::kInitCmdsCount;
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
#if GUITION_TE_SYNC
        // --- tear-effect sync: falling-edge ISR signals the start of each panel refresh ---
        teSem_ = xSemaphoreCreateBinary();
        pinMode(PIN_TE, INPUT_PULLUP);
        attachInterruptArg(PIN_TE, &GuitionDisplay::onTe_, this, FALLING);
        {   // diagnostic: prove TE is wired + toggling (~6 edges per 100 ms at ~62 Hz)
            const uint32_t e0 = teEdges_;
            delay(100);
            Serial.printf("[lcd] guition TE edges in 100 ms: %u%s\n", (unsigned)(teEdges_ - e0),
                          (teEdges_ - e0) ? "" : " (no TE - frames will not be synced)");
        }
#endif
#if GUITION_PANEL_SELFTEST
        selfTestBars_();
#endif

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
    // Rows per DMA transfer. These buffers MUST be internal DMA memory (PSRAM can't feed the SPI DMA -
    // that was the original blank-panel bug), and there are two of them, so they come straight out of the
    // scarce internal heap: at 40 rows they cost 2 x 25.6 KB and min_free_heap fell to 2.7 KB under web
    // load with BLE streaming - far too close to OOM for a ride device. 20 rows halves it to 2 x 12.8 KB.
    // The cost is more (smaller) transfers per frame, which the panel doesn't mind.
    static constexpr int STRIP_ROWS = 20;
    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    uint16_t* strips_[2] = {nullptr, nullptr};  // two INTERNAL-DMA strip buffers, used alternately
    int stripNext_ = 0;                         // the buffer NOT held by an in-flight transfer
    SemaphoreHandle_t colorDone_ = nullptr;     // given by the ISR when a queued strip finishes
    SemaphoreHandle_t teSem_ = nullptr;         // given on each TE falling edge
    volatile uint32_t teEdges_ = 0;             // TE edge counter (boot diagnostic)
    uint32_t frames_ = 0;                       // frames pushed (diagnostic)
    uint16_t* swap_ = nullptr;                  // PSRAM byte-swap scratch (one frame), lazily sized
    size_t swapCap_ = 0;                        // capacity of swap_ in pixels
    bool wasDown_ = false;
    bool touchAlive_ = false;

    static IRAM_ATTR void onTe_(void* ctx) {
        auto* self = static_cast<GuitionDisplay*>(ctx);
        if (!self) return;
        self->teEdges_++;
        if (self->teSem_) {
            BaseType_t woken = pdFALSE;
            xSemaphoreGiveFromISR(self->teSem_, &woken);
            if (woken == pdTRUE) portYIELD_FROM_ISR();
        }
    }

    // Wait for a FRESH TE falling edge before starting a frame: drop any stale edge, then block for
    // the next one (<= ~16 ms at 62 Hz). The 40 ms timeout keeps frames flowing if TE isn't wired.
    void waitTe_() {
#if GUITION_TE_SYNC
        if (!teSem_) return;
        xSemaphoreTake(teSem_, 0);
        xSemaphoreTake(teSem_, pdMS_TO_TICKS(40));
#endif
    }

    // esp_lcd colour-transfer-complete callback (runs in ISR context; IRAM so it's safe while the flash
    // cache is off during an NVS write).
    static IRAM_ATTR bool onColorDone_(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* ctx) {
        BaseType_t woken = pdFALSE;
        auto* self = static_cast<GuitionDisplay*>(ctx);
        if (self && self->colorDone_) xSemaphoreGiveFromISR(self->colorDone_, &woken);
        return woken == pdTRUE;
    }

    bool allocStrips_() {
        for (auto& b : strips_) {
            if (!b) b = (uint16_t*)heap_caps_malloc((size_t)LCD_W * STRIP_ROWS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        }
        return strips_[0] && strips_[1];
    }

    // Queue one filled strip: wait for the previous strip's transfer to finish, draw, then hand the
    // OTHER buffer to the next fill. The 500 ms timeout is a deadlock guard (a failed draw raises no
    // callback) — normal strips complete in a few ms.
    void sendStrip_(int xs, int y, int xe, int rows) {
        if (colorDone_) xSemaphoreTake(colorDone_, pdMS_TO_TICKS(500));
        esp_lcd_panel_draw_bitmap(panel_, xs, y, xe, y + rows, strips_[stripNext_]);
        stripNext_ ^= 1;
    }

    // Push a region to the panel in row-strips, mirroring the vendor BSP's flush:
    //   * DMA: the LVGL frame buffer lives in PSRAM; one 300 KB transfer makes spi_master try to allocate
    //     a 300 KB *internal* DMA bounce buffer and fail ("setup_dma_priv_buffer: Failed to allocate priv
    //     TX buffer"). ~26 KB internal-DMA strips sidestep that.
    //   * Async: colour transfers are queued, so a strip buffer can still be on the wire when draw_bitmap
    //     returns. Two buffers used alternately + waiting on the transfer-done callback means we never
    //     overwrite a buffer mid-transfer (a single reused buffer would scramble the image).
    //   * Byte order: esp_lcd sends bytes verbatim; LVGL renders little-endian RGB565, the panel wants
    //     big-endian — swap while copying (the caller's buffer stays untouched, so /screen.bmp is right).
    // In QSPI the driver sends no RASET: draw_bitmap emits RAMWR for the row-0 strip and RAMWRC
    // (continue) for the rest, so sequential full-width strips from ys=0 land correctly — exactly what
    // LVGL full-refresh mode delivers (one (0,0,W-1,H-1) flush per frame).
    void pushFrame_(int xs, int ys, int xe, int ye, const uint16_t* px) {
        if (!panel_ || xe <= xs || ye <= ys || xe - xs > LCD_W || !allocStrips_()) return;
        const int w = xe - xs;
        const size_t total = (size_t)w * (ye - ys);
        const uint16_t* src0 = px;
#if GUITION_RGB565_SWAP
        // Byte-swap the WHOLE frame up front, before the TE wait, so the timed window after TE is only
        // memcpy + SPI. Doing it per strip inside the window stretched the write well past the panel's
        // ~13 ms refresh. Swapping into a PSRAM scratch also leaves the caller's buffer untouched, so
        // GET /screen.bmp and the serial SCREEN dump still see true little-endian RGB565.
        if (swapCap_ < total) {
            if (swap_) heap_caps_free(swap_);
            swap_ = (uint16_t*)heap_caps_malloc(total * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            swapCap_ = swap_ ? total : 0;
        }
        if (swap_) {
            for (size_t i = 0; i < total; ++i) swap_[i] = (uint16_t)((px[i] >> 8) | (px[i] << 8));
            src0 = swap_;
        }
#endif
        const uint32_t tTe = micros();
        waitTe_();  // start the frame at a panel refresh boundary
        const uint32_t tStart = micros();
        for (int y = ys; y < ye; y += STRIP_ROWS) {
            const int rows = (y + STRIP_ROWS <= ye) ? STRIP_ROWS : (ye - y);
            const uint16_t* src = src0 + (size_t)(y - ys) * w;
            const size_t n = (size_t)w * rows;
            uint16_t* dst = strips_[stripNext_];
#if GUITION_RGB565_SWAP
            if (swap_) memcpy(dst, src, n * 2);
            else for (size_t i = 0; i < n; ++i) dst[i] = (uint16_t)((src[i] >> 8) | (src[i] << 8));
#else
            memcpy(dst, src, n * 2);
#endif
            sendStrip_(xs, y, xe, rows);
        }
        // Wait out the last strip so the timing below is the true on-the-wire cost (and the frame is
        // fully sent before LVGL is told the flush is done), then hand the token back for the next frame.
        if (colorDone_ && xSemaphoreTake(colorDone_, pdMS_TO_TICKS(500)) == pdTRUE) xSemaphoreGive(colorDone_);
        const uint32_t tEnd = micros();
        // How our write compares with the panel's ~13 ms refresh window (vendor Tvdl) tells us whether a
        // frame spans multiple refreshes; frames/s shows whether the UI is redrawing when it shouldn't.
        frames_++;
#if GUITION_FRAME_STATS
        static uint32_t lastLog = 0;
        if (millis() - lastLog >= 2000) {
            lastLog = millis();
            Serial.printf("[lcd] guition frame: TE wait %lu us, xfer %lu us (Tvdl 13000), frames %lu\n",
                          (unsigned long)(tStart - tTe), (unsigned long)(tEnd - tStart),
                          (unsigned long)frames_);
        }
#else
        (void)tTe; (void)tStart; (void)tEnd;
#endif
    }

#if GUITION_PANEL_SELFTEST
    // RED / GREEN / BLUE / WHITE horizontal bands, top to bottom, held ~1.5 s (see the macro above).
    void selfTestBars_() {
        if (!panel_ || !allocStrips_()) return;
        static constexpr uint16_t kBands[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};  // RGB565 R, G, B, W
        waitTe_();
        for (int y = 0; y < LCD_H; y += STRIP_ROWS) {
            const int rows = (y + STRIP_ROWS <= LCD_H) ? STRIP_ROWS : (LCD_H - y);
            uint16_t c = kBands[(y * 4) / LCD_H];
#if GUITION_RGB565_SWAP
            c = (uint16_t)((c >> 8) | (c << 8));
#endif
            uint16_t* dst = strips_[stripNext_];
            for (size_t i = 0, n = (size_t)LCD_W * rows; i < n; ++i) dst[i] = c;
            sendStrip_(0, y, LCD_W, rows);
        }
        delay(1500);
        // Then a STATIC solid-white frame, written once and held. Bright + uniform + no redraws, so any
        // line/banding texture visible here is the panel's own analog drive, not our data or write cadence.
        fillScreen_(0xFFFF);
        delay(3000);
    }

    void fillScreen_(uint16_t rgb565) {
        if (!panel_ || !allocStrips_()) return;
#if GUITION_RGB565_SWAP
        const uint16_t c = (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
#else
        const uint16_t c = rgb565;
#endif
        waitTe_();
        for (int y = 0; y < LCD_H; y += STRIP_ROWS) {
            const int rows = (y + STRIP_ROWS <= LCD_H) ? STRIP_ROWS : (LCD_H - y);
            uint16_t* dst = strips_[stripNext_];
            for (size_t i = 0, n = (size_t)LCD_W * rows; i < n; ++i) dst[i] = c;
            sendStrip_(0, y, LCD_W, rows);
        }
    }
#endif

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
