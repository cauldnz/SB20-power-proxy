# Guition JC3248W535 — 3.5″ 320×480 QSPI head-unit port

The **Guition JC3248W535**: an ESP32-S3-N16R8 board (dual-core LX7, **8 MB OPI PSRAM**, 16 MB flash,
native USB) with a **3.5″ 320×480 IPS** panel on an **AXS15231B** controller over **QSPI**, plus an
**AXS15231B capacitive touch** controller over I²C. It's the largest head-unit we've targeted (vs the
CYD's 240×320 and the S3-Touch's 172×320), and — unlike the no-PSRAM CYD — it has ample RAM, so none of
the CYD's LVGL heap-fragmentation drama applies.

Board on the dev machine: **COM14**, USB `303A:1001` (native), base MAC `28:84:85:49:4D:20`
(SSID `Setup-4D21`). Registered in [`BOARDS.md`](../../BOARDS.md).

> ### ⚠️ Provenance of the name "JC3248W535" — reported, not verified
>
> **The model string is the one thing here that has not been confirmed against the hardware.** It
> comes from the **AliExpress order listing** the owner bought the board from (confirmed 2026-09-23).
> It has **not** been read off the PCB silkscreen.
>
> It also cannot be: **no ESP32 board can self-report a vendor model.** There is no register, eFuse
> or USB descriptor that says "Guition JC3248W535" — the USB ID `303A:1001` is Espressif's generic
> native-USB ID, shared with every other S3. Nothing readable over USB or HTTP will ever settle it.
>
> Seller listings are also loose with **variant suffixes**: Guition ships more than one JC3248W535
> (one of the sources below is a repo named `JC3248W535EN`), and if variants share the panel and pin
> map, nothing measured here would distinguish them. **Treat the suffix as unknown.**
>
> **To settle it:** read the PCB silkscreen — by eye, or photograph it with the bench camera
> (`skills/bench-camera`, see BOARDS.md). Then replace this box with what the board actually says.
>
> **What this does and doesn't put at risk.** The firmware depends on the *measured* pin map and
> panel controller below, not on the name — so a wrong string breaks nothing that runs. It matters
> for (a) **re-ordering** (someone buying "another one of these" could get a different variant) and
> (b) **trusting third-party BSPs**: the pin maps below were taken from writeups for a board we
> believe is this model. They are all independently confirmed on our hardware now (see the table),
> so that risk has already been retired — but it is the reason the confirmation mattered.

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

### Measured on *our* board (2026-09-23, `esptool` + the running firmware)

These are read from the silicon, not from a datasheet or a listing — the only claims here that owe
nothing to the model name:

| Property | Value | How |
|---|---|---|
| SoC | **ESP32-S3, revision v0.2**, 40 MHz crystal, WiFi+BLE | `esptool flash_id` |
| Flash | **16 MB**, manufacturer `0x68` (Boya), device `0x4018`; eFuse: **quad** line | `esptool flash_id` |
| PSRAM | **~8 MB octal** — 7.37 MiB largest free block | `/stats` `largest_block` on the running board |
| ⇒ module | **ESP32-S3-N16R8** | implied by the two rows above |
| Base MAC | `28:84:85:49:4D:20` | `esptool` |
| Panel controller | **AXS15231B**, 320×480 | the vendor AXS15231B init table + the pin map below render a correct image; a different controller would not |
| Touch | capacitive, I²C `0x3B` | taps route through to LVGL (`TAP`/`STATE` bench console) |

### Pin map

Originally taken from the community BSPs below (atomic14's JC3248W535 writeup, the F1ATB setup
guide, moononournation/Arduino_GFX — see Sources) — and **since confirmed on our hardware**: the
display renders correctly and touch works with exactly these pins, which is a strong fingerprint.

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

## Bring-up on hardware: blank screen → clean image (2026-09-16)

The first flashed build booted clean (`[lcd] AXS15231B 320x480 QSPI up`, LVGL alive, no errors) but the
panel was **blank with the backlight on**. Each fix below was grounded in evidence, not guessed:

1. **The driver's default init table blacks the screen.** Symptom: boot noise (uninitialised panel RAM),
   then black. `esp_lcd_axs15231b`'s built-in `vendor_specific_init_default` targets a *different*
   AXS15231B panel (different timing values) and **ends with `0x22` ALLPOFF** ("all pixels off"), with
   nothing restoring normal display. **Fix:** pass this board's own table via `vendor_config.init_cmds` —
   `firmware/src/disp/GuitionInitCmds.h`, generated from the vendor BSP and cross-checked byte-for-byte
   against Arduino_GFX 1.6.0 (29/29 register writes identical except `D0` bytes 10–11). It ends
   `0x13 NORON → 0x11 SLPOUT → 0x2C`, then `begin()` sends DISPON.
2. **A boot colour-bar self-test** (`GUITION_PANEL_SELFTEST`: red/green/blue/white bands, 1.5 s) then showed
   correct **R/G/B/W** → init, QSPI link, colour order, byte order (`GUITION_RGB565_SWAP=1`, matching the
   vendor's `LV_COLOR_16_SWAP`), orientation and row continuation all correct. **Lesson: solid bars hide
   horizontal/within-row faults** — the UI still came out mangled.
3. **Async strip race.** esp_lcd queues colour transfers asynchronously, so reusing one DMA strip buffer
   lets the next strip overwrite one still on the wire. **Fix (mirrors the vendor `lv_port.c`):** two
   internal-DMA strip buffers used alternately + an `on_color_trans_done` ISR/semaphore so each strip is
   queued only after the previous finished.
4. **Proving LVGL wasn't the problem.** The firmware's serial `SCREEN` command dumps every flush as
   `<AREA x1 y1 x2 y2 base64-RGB565>`; reassembled, it showed **one full-frame flush `(0,0,319,479)`** and a
   **pixel-perfect** render. So FULL mode was active and the corruption was between the buffer and the glass.
5. **Tear-effect (TE) sync removed the corruption band.** Symptom: a full-height light band with sharp
   edges exactly at the white QR card's columns (x 75–245). The vendor esp_lcd BSP — same driver, same
   per-strip framing — differs from ours only by starting each frame on the panel's **TE falling edge
   (GPIO38)**; a community thread on this panel reports "no image, partial image, garbled image" without
   tear avoidance. **Fix:** `GUITION_TE_SYNC` — falling-edge ISR, drain any stale edge, wait (≤40 ms) for
   a fresh one before the first strip. Boot log proves the wiring: `[lcd] guition TE edges in 100 ms: 6`
   (~60 Hz, matching the vendor's `Tvdl=13 ms`/`Tvdh=3 ms`). Result: band gone, image correctly placed.

### Residual: faint alternate-row striping on bright content (open)

After the fixes above the image is correct, but **bright areas (the white QR card, white text) show a faint
horizontal line texture** — alternate rows differing slightly in brightness, visible by eye. Established:

- **It isn't our pixels.** The serial `SCREEN` dump, reassembled, shows every white-margin row *byte-identical*
  pure white and the background a single colour — so the variation is introduced panel-side.
- **It isn't repeated redraws.** Instrumentation shows `frames 1` on a static screen: one frame is pushed
  and nothing re-pushes, so this isn't accumulated tearing.
- **⭐ It survives a once-written solid white frame.** With `-DGUITION_PANEL_SELFTEST=1` the board holds a
  solid white screen (written once, never redrawn). Captured on camera, it covers the panel completely —
  no stale content — **and still shows the line texture**. Since no SPI activity happens at all after that
  write, this also rules out pixel clock, write cadence and TE timing as causes *without* a test build.
  Combined with our init registers being byte-identical to two independently-working sources, the
  remaining explanation is the panel's own analog drive (VCOM / line inversion). Note atomic14 warns some
  batches of this board are suspected **non-genuine AXS15231B silicon**.
- **Frame writes span ~2 refreshes.** Measured `TE wait ~2.3 ms, xfer ~25 ms` against the panel's ~13 ms
  `Tvdl`. The byte-swap was moved out of the post-TE window to shorten this.
- **Two candidate fixes tried and refuted (don't repeat):**
  - `D0` bytes 10–11 swapped to the Arduino_GFX order (`C2 42` instead of the BSP's `42 C2`): **no change**.
    Reverted to the BSP order.
  - Prepending `DISPOFF`+`SLPIN` so the analog registers are programmed while the panel sleeps (as
    Arduino_GFX does): **panel stays black**. Reverted. Note Arduino_GFX **1.6.1 added that preamble** and
    1.6.1 is the release reported broken on this panel (#803) — the preamble may well *be* that regression.

Everything else about the panel's drive registers is byte-identical to two independent working sources, so
the remaining lead is analog (VCOM / line-inversion) or the write still crossing the refresh. It is cosmetic
and least visible on the dark ride UI.

## Phase-1 validation: DONE (2026-09-23)

Validated end-to-end against a **simulated meter over the air** — an ESP32-C3 running the
`esp32c3-oled` mock build advertising CPS, with the Guition reading it, correcting it and
re-broadcasting as the Stages crank. Full narrative + numbers in `decisions.md` (2026-09-23).

- ✅ **Colour order, byte order, orientation** — verified by the boot colour bars (R/G/B/W top→bottom)
  and the Wi-Fi QR screen: `GUITION_RGB565_SWAP=1`, `LCD_RGB_ELEMENT_ORDER_RGB`, rotation 0, no mirror.
- ✅ **Provisioning** — real captive-portal flow (`Setup-4D20` → SSID/pass → NVS → reboot → DHCP),
  not a compiled-in SSID.
- ✅ **IN leg** — `source=connected` 15/15 samples, power *changing* across 14 distinct values,
  cadence 85, `forwarded` climbing.
- ✅ **OUT leg** — a host BLE central sees `Stages 62144`, decoding watts/cadence/balance with
  byte-faithful `0x2F` framing. The spoof identity is not C3-specific.
- ✅ **UI + touch** — `SCREEN` framebuffer dump matches the expected ride screen; synthetic `TAP`s
  walk Ride → Setup → More → Ride through the real digitiser → LVGL → `navTo` chain.
- ✅ **OTA** — push OTA succeeded first attempt at RSSI −82 dBm, reboot verified, NVS preserved.
- ✅ **mDNS hostname** — now `sb20proxy-guition.local`. The per-board switch keyed off
  `CONFIG_IDF_TARGET_ESP32S3` and the Guition is *also* an S3, so it had been colliding with the
  Waveshare board on `sb20proxy-s3.local`. Name boards by board, not by chip.
- ✅ **Heap under load** — `STRIP_ROWS` cut 40 → 20 after the two internal-DMA strip buffers
  (25.6 KB each) left a 2,700-byte floor under web + BLE load. Now 25,620 B on the same test.

### Still open

- **Touch orientation** — taps land correctly on the nav bar, so no gross X/Y mirror is needed; a
  precise corner-accuracy check across the full panel has not been done.
- **Touch INT/RST pins disagree across sources.** The seam drives **RST=GPIO12** (pulse) / reads
  **INT=GPIO11** per the F1ATB guide, but the vendor BSP has touch RST/INT = **-1** (not connected) and the
  LVGL v9 test repo (byte-me404/JC3248W535_lvgl_test) uses **INT=GPIO3**, no RST. Touch works either way
  (polled I2C), but GPIO11/12 may belong to something else (e.g. the SD slot) — stop driving them once
  confirmed.
- **Not yet ridden.** Everything above used a *simulated* meter; no SB20 and no real pedals.

### Flashing this board

`code/scripts/flash_s3.py` now preserves NVS by default (it writes the code regions and skips every
data partition), so a reflash keeps WiFi credentials, meter/crank identity and calibration. Use
`--erase-nvs` when a clean slate is actually wanted. Before 2026-09-23 it wrote the merged
`firmware.factory.bin` at `0x0`, which is `0xFF`-padded across the NVS window — so every flash
silently blanked provisioning and dropped the board back into its setup portal.

## Sources

- Espressif **esp_lcd_axs15231b** component (the vendored driver) — https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd/esp_lcd_axs15231b · registry: https://components.espressif.com/components/espressif/esp_lcd_axs15231b
- atomic14 — *Guition JC3248W535 (3.5″ ESP32-S3): specs, gotchas & where to buy* — https://www.atomic14.com/esp32/boards/guition-jc3248w535/
- F1ATB — *ESP32-S3 3.5 inch Capacitive Touch IPS Display – Setup* (pin map + touch protocol) — https://f1atb.fr/home-automation/esp32-s3/esp32-s3-3-5-inch-capacitive-touch-ips-display-setup/
- NorthernMan54/JC3248W535EN — a working esp_lcd BSP for this exact board — https://github.com/NorthernMan54/JC3248W535EN
- Arduino_GFX version-pincer (why we didn't use it): #803 (AXS15231B init regressed 1.6.1+) https://github.com/moononournation/Arduino_GFX/issues/803 · #772 (won't compile on core 3.3.6+) https://github.com/moononournation/Arduino_GFX/issues/772
