# Guition JC3248W535 — 3.5″ 320×480 QSPI head-unit port

The **Guition JC3248W535**: an ESP32-S3-N16R8 board (dual-core LX7, **8 MB OPI PSRAM**, 16 MB flash,
native USB) with a **3.5″ 320×480 IPS** panel on an **AXS15231B** controller over **QSPI**, plus an
**AXS15231B capacitive touch** controller over I²C. It's the largest head-unit we've targeted (vs the
CYD's 240×320 and the S3-Touch's 172×320), and — unlike the no-PSRAM CYD — it has ample RAM, so none of
the CYD's LVGL heap-fragmentation drama applies.

Board on the dev machine: **COM14**, USB `303A:1001` (native), base MAC `28:84:85:49:4D:20`
(SSID `Setup-4D21`). Registered in [`BOARDS.md`](../../BOARDS.md).

## Why this port is different from the CYD/S3

The CYD (ILI9341/ST7789) and S3-Touch (JD9853) panels are **standard SPI**, driven by **hand-rolled
register code** in their `disp/*Display.h` seams. The JC3248W535 panel is **QSPI** (4 data lines), which
those seams don't speak. So `GuitionDisplay.h` drives the panel via the ESP-IDF **`esp_lcd`** API plus
the **AXS15231B panel driver vendored under `firmware/lib/esp_lcd_axs15231b/`** (which wraps the QSPI
command framing + the vendor init table). `esp_lcd` ships inside the arduino-esp32 core, so — unlike a
third-party Arduino display library — there's **no version pincer against the core** (see the "driver
choice" gotcha below for why Arduino_GFX was abandoned). The dependency is **scoped to the Guition env
only** (per-env `lib_deps`) — the CYD/S3/C3 builds are untouched. Everything above the display seam (the
LVGL UI, proxy core, WiFi, web UI) is inherited unchanged; the port is one seam header + one vendored
driver + one `platformio.ini` env family + two one-line `main.cpp` arms + a shared-code render-mode knob.

## The port (what it took)

- **`firmware/src/disp/GuitionDisplay.h`** — the new seam. Same 7-method contract as the other seams
  (`begin`/`setBrightness`/`blit`/`readTap`/`touchAlive`/`blitArea`/`readTouchState`). Capacitive touch →
  no resistive-cal methods (all `readRaw`/`setCal`/… calls in `main.cpp` are already `#if LCD_DRIVER_CYD`).
- **`main.cpp`** — an `#elif defined(LCD_DRIVER_GUITION)` arm in the seam-include block and in the
  `using PanelDisplay = …` typedef.
- **`firmware/lib/esp_lcd_axs15231b/`** — the Espressif AXS15231B `esp_lcd` panel driver (Apache-2.0),
  vendored from `esp-iot-solution` (like `lib/monocypher`). Trimmed to **display-only**: the upstream
  file also carried the I2C touch driver, which needs the `esp_lcd_touch` component that arduino-esp32
  doesn't bundle — so the touch code + its `esp_lcd_touch.h` include were removed (touch is hand-rolled in
  the seam). The MIPI-DSI variant `.c` is not vendored (no MIPI-DSI on the S3).
- **`platformio.ini`** — the `esp32-guition` env family (`-live`, `-live-bench`, `-ota`, `-live-ota`),
  extending `esp32s3-pio-min` for the pioarduino platform plumbing, overriding to 16 MB flash +
  `qio_opi` PSRAM, adding the vendored `esp_lcd_axs15231b` lib, and setting the geometry + driver +
  full-refresh flags. **No core-pin** — esp_lcd is in the core, so it stays on the S3's proven platform.
- **`firmware/src/ui/LvglUi.cpp`** — a `LCD_LVGL_FULL_REFRESH` knob (guarded; CYD/S3/native unchanged):
  when set, LVGL uses `LV_DISPLAY_RENDER_MODE_FULL` with a full-frame **PSRAM** buffer instead of the
  partial banded buffer.

First USB flash: `python code/scripts/flash_s3.py --env esp32-guition-live --port COM14`.

## Verified hardware facts (pin map + protocol)

From the board's community BSP — atomic14's JC3248W535 writeup, the F1ATB setup guide, and
moononournation/Arduino_GFX (see Sources):

| Interface | Pins |
|---|---|
| Display QSPI | CS=45, SCK=47, D0=21, D1=48, D2=40, D3=39; controller AXS15231B, 320×480 IPS |
| Backlight | GPIO **1** (LEDC PWM) |
| Touch (AXS15231B I²C @ `0x3B`, 400 kHz) | SDA=**4**, SCL=**8**, INT=11, RST=12 |
| Touch read | write `B5 AB A5 5A 00 00 00 08`, read 8 bytes: `[1]`=points, X=`[2..3]`, Y=`[4..5]` (high nibble = flags) |

## Gotchas (the port traps)

- **⚠️ GPIO8 is the touch SCL** — but `Config.h`'s `SB20_STATUS_LED_PIN` defaults to **8**. Driving it as
  the status-LED heartbeat would fight the touch I²C bus. The env overrides `-DSB20_STATUS_LED_PIN=2`
  (the board has no user LED; the heartbeat is parked on a free GPIO). **Any new board must check this.**
- **LVGL full-refresh only.** The AXS15231B QSPI panel renders correctly **only** in
  `LV_DISPLAY_RENDER_MODE_FULL` — partial/windowed flushes are broken on this silicon (some batches also
  ignore the `0x2A`/`0x2B` window-address commands). The env sets `-DLCD_LVGL_FULL_REFRESH=1`; the 8 MB
  PSRAM makes the 300 KB full-frame buffer free.
- **⚠️ Driver choice: why NOT Arduino_GFX (a version pincer) → esp_lcd.** The first attempt used
  Arduino_GFX and hit an unresolvable three-way version trap, which is *why* this board uses esp_lcd —
  don't reintroduce Arduino_GFX without checking all three:
  - Arduino_GFX **1.6.1+ regressed** the AXS15231B init (blank/garbled panel) — Arduino_GFX#803 — so only
    **1.6.0** works for this panel.
  - But 1.6.0's standard-SPI databus files (always compiled, even though we'd only use its QSPI path) call
    the **pre-3.3.6** `spiFrequencyToClockDiv()` signature, and arduino-esp32 **core 3.3.6+ changed it**
    (Arduino_GFX#772 / arduino-esp32#12328) → 1.6.0 won't compile on the S3's core 3.3.9.
  - Pinning the platform to core **3.3.5** (pioarduino `55.03.35`) fixed the compile, but that older
    platform release's Python-dependency install **fails deterministically** in this environment
    (`Failed to install Python dependencies into penv`) — a hard wall.
  - **Resolution:** drop Arduino_GFX entirely and drive the panel with **`esp_lcd`** (in the core, no
    version drift) + the vendored `esp_lcd_axs15231b` driver. Stays on the S3's proven `55.03.39`
    platform, no core-pin. LovyanGFX was considered but its AXS15231B QSPI support is develop-branch-only
    and fiddly.
- **This component's `disp_on_off` is inverted.** `esp_lcd_new_panel_axs15231b` maps the legacy
  `disp_off(panel, off)` callback onto the `disp_on_off` slot, so `esp_lcd_panel_disp_on_off(panel,
  false)` turns the panel **ON** (matches the vendor BSP). `begin()` calls it with `false`.
- **QSPI addresses full frames only.** In the driver's QSPI path `draw_bitmap` sends `CASET` but **not**
  `RASET`, using `RAMWR` when `y_start==0` and `RAMWRC` (continue) otherwise — so only a full frame
  starting at row 0 is addressable. That's the concrete reason partial mode is broken and why
  `LCD_LVGL_FULL_REFRESH` is required.
- **Pixel byte order.** `esp_lcd_panel_draw_bitmap` sends pixel bytes verbatim; LVGL renders RGB565
  little-endian while the panel wants big-endian, so the seam byte-swaps into a PSRAM scratch
  (`GUITION_RGB565_SWAP`, default on). Flip to 0 if hues come out inverted.
- **No hardware 90° rotation.** The panel is native **portrait 320×480** (rotation 0) — which is exactly
  what the portrait UI wants, so no software rotation is needed. Don't reach for `setRotation(1/3)`.
- **pioarduino platform.** Same as the S3: build on the pioarduino (IDF 5.5) platform; the stock
  espressif32 core's older bootloader is a known S3 boot-loop risk. Native-USB flashing needs esptool
  ≥ 4.11 (`flash_s3.py`/`flash_c3.py` auto-pick a good one).
- **Flash mode.** Set `memory_type = qio_opi` (N16R8 = QIO flash + OPI/octal PSRAM). If a unit boot-loops
  at the 2nd-stage bootloader, try `dio_opi` (the fallback the S3-Touch needed).

## Open items / to verify on hardware (Phase-1 validation)

- **Colour byte order** — the seam byte-swaps by default (`GUITION_RGB565_SWAP=1`). If hues come out
  inverted (red/blue swapped) on hardware, rebuild with `-DGUITION_RGB565_SWAP=0`.
- **Touch orientation** — the AXS15231B reports native portrait coords; confirm no X/Y mirror is needed
  (the S3 needed `x = (LCD_W-1) - rawX`). Tune in `GuitionDisplay::readTouchState` after seeing a tap land.
- **Panel orientation / colour order** — if the image is mirrored or wrong-coloured, adjust via
  `esp_lcd_panel_mirror` / `esp_lcd_panel_swap_xy` after init, or flip `rgb_ele_order` to BGR.
- **mDNS hostname** — the C3 already claims `sb20proxy.local`; give the Guition its own per-board
  hostname (like `-cyd`/`-s3`) so it doesn't collide on the LAN.

## Sources

- Espressif **esp_lcd_axs15231b** component (the vendored driver) — https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd/esp_lcd_axs15231b · registry: https://components.espressif.com/components/espressif/esp_lcd_axs15231b
- atomic14 — *Guition JC3248W535 (3.5″ ESP32-S3): specs, gotchas & where to buy* — https://www.atomic14.com/esp32/boards/guition-jc3248w535/
- F1ATB — *ESP32-S3 3.5 inch Capacitive Touch IPS Display – Setup* (pin map + touch protocol) — https://f1atb.fr/home-automation/esp32-s3/esp32-s3-3-5-inch-capacitive-touch-ips-display-setup/
- NorthernMan54/JC3248W535EN — a working esp_lcd BSP for this exact board — https://github.com/NorthernMan54/JC3248W535EN
- Arduino_GFX version-pincer (why we didn't use it): #803 (AXS15231B init regressed 1.6.1+) https://github.com/moononournation/Arduino_GFX/issues/803 · #772 (won't compile on core 3.3.6+) https://github.com/moononournation/Arduino_GFX/issues/772
