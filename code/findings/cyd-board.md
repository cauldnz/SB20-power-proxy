# CYD board — AliExpress ESP32-2432S028R "Cheap Yellow Display" (third head-unit target)

**Status: ✅ PORTED + TWIN-TESTED (2026-07-03).** The full bike computer (BLE meter central →
correction → Stages-crank rebroadcast + WiFi + the 5-screen touch UI) runs on this board; the
digital-twin loop is proven end-to-end (fake 200 W CPS meter → CYD reads/rebroadcasts →
`crank_reader` decodes 200 W / 85 rpm in byte-faithful `0x2F` framing).

## The board
AliExpress item `1005007524304778` ("AOKIN ESP32 Touchscreen 2.8inch"): the community-famous
**ESP32-2432S028R** — classic **ESP32-D0WD-V3** (WROOM-32, dual-core LX6, 4 MB flash, **no PSRAM**),
2.8" **240×320 TFT** on HSPI, **XPT2046 resistive touch** (bit-banged), microSD, RGB LED, LDR,
CH340 UART. The owner's unit is the **two-port ("CYD2USB") variant** — Micro-USB *and* USB-C; **only
the Micro-USB enumerates** (CH340, **COM17**, MAC `8c:94:df:93:cc:8c`); the USB-C appears dead for
data. Panel probes as **ST7789-family** (`0xD3` RDID4 reads all-zero → not ILI9341) and needs
**INVON** — both known CYD2USB traits. Canonical community resource: witnessmenow's
ESP32-Cheap-Yellow-Display repo (PINS.md).

Pin map (verified by `firmware/src/cyd_probe_main.cpp`, the serial-driven hardware probe:
`ID`/`BARS`/`INV`/`MAD`/`TOUCH`/`LED` commands):
TFT MISO=12 MOSI=13 SCLK=14 CS=15 DC=2 BL=21 (no reset pin — tied to EN) · touch XPT2046
CLK=25 MOSI=32 CS=33 IRQ=36 MISO=39 · RGB LED R=4 G=16 B=17 (active low) · LDR=34 · speaker=26 ·
SD on VSPI (CS=5).

## The port (envs `esp32cyd`, `esp32cyd-live`, `esp32cyd-live-bench`)
- Platform **espressif32@~6.7.0** (same as the C3; classic ESP32 is its best-proven chip),
  `board = esp32dev`, `min_spiffs.csv` (4 MB), UART serial — **no `ARDUINO_USB_*` flags** (classic
  ESP32 has no native USB; the CDC flag would even break the build via `setTxTimeoutMs`).
- **Seam:** `firmware/src/disp/CydDisplay.h` (generic MIPI-DCS init driving both ST7789/ILI9341 fits,
  `CYD_INVERT` flag, band-aware blit via `SPI.writePixels`, bit-banged XPT2046 with pressure gating +
  median-of-3 + community calibration in `CYD_TP_*` flags, LEDC backlight, RGB status LED helper).
  `main.cpp` selects the seam with `-DLCD_DRIVER_CYD=1`.
- **The same pure UI, wider:** `-DLCD_PANEL_W=240` — `LcdCanvas`/`LcdUi` lay out from `LCD_W/LCD_H`,
  so the S3's 172-wide UI serves the 240-wide panel unchanged (S3 default untouched).
- **No PSRAM → banded rendering:** 240×320×2 = 153 KB can't sit beside WiFi+BLE in classic-ESP32
  DRAM. `-DLCD_BANDS=4` gives the canvas one 38 KB horizontal band; the render loop sweeps
  setBand→render→blit down the frame. The pure renderer is band-agnostic (writes outside the band
  clip) — **pixel-identical to full-frame by host test** (`test_lcd_banded_render_matches_full`).
  The serial `SCREEN` command streams a **top-down BMP** (negative biHeight) band-by-band, so
  screenshots work without ever allocating a full frame.

## Gotchas (each cost a debug loop — don't repeat)
1. **GPIO 8 is a FLASH pin on classic ESP32.** `Config::STATUS_LED_PIN = 8` (the C3's LED) wedges
   the chip the instant `pinMode(8, OUTPUT)` runs → `TG1WDT_SYS_RESET` boot loop right after the
   "spoofing as" banner. GPIOs **6–11** are the SPI-flash bus on WROOM-32. Fixed: the pin is now
   `-DSB20_STATUS_LED_PIN` (CYD uses **4** = RGB-red, also active-low).
2. **A >15 s serial stream trips our own loop-stall watchdog.** The 300 KB `SCREEN` dump blocks
   `loop()` ~30 s at 115200 → `g_loopBeat` freezes → the Phase-B watchdog restarts the board
   mid-stream. Fixed: the emit path feeds `g_loopBeat` (like OTA does) and batches 512-byte
   `Serial.write`s (per-byte writes throttle to ~3 KB/s on the UART lock).
3. **Build from native PowerShell** — the classic-ESP32 toolchain's first install corrupted under
   Git-Bash/MSYS (cc1plus "out of memory" / CreateProcess failures), same class of problem as the
   pioarduino installer. The standing rule: `pio` runs via `…\.platformio\penv\Scripts\python.exe`
   from PowerShell.
4. **CH340 auto-reset:** opening the COM port pulses EN unless DTR/RTS are cleared **before**
   `open()` (pyserial: set `s.dtr = s.rts = False` on the unopened object). Flashing itself is
   painless (no USB-JTAG stub bug — 460800 works).
5. **This unit's XPT2046 idle Z2 reads MID-SCALE (~2045), not the datasheet ~4095.** The composite
   pressure `Z1+4095-Z2` therefore reads ~2050 UNTOUCHED = a permanent phantom press that swallows
   every tap (LVGL pinned in pressed state). And a Z1>200 gate drops weak right-edge presses (Z1
   scales with raw X; this film's X is inverted). The working gate: **Z1 alone with a low floor**
   (`CYD_TP_ZMIN`, default 40 — idle Z1 is a clean 0). Probe any unit first: serial **`RAWZ`**
   prints one `{down,rx,ry,z,z1,z2}` sample (2026-07-03).
6. **LVGL's builtin fixed pool is a trap on this board** — 48 KB exhausted with the full widget
   tree (NULL glyph-buf write → StoreProhibited; asserts compiled out), and 72 KB of .bss doesn't
   link once the live build's NimBLE central is in (dram0 overflow). LVGL runs **malloc-backed**
   (`LV_STDLIB_CLIB` in `include/lv_conf.h`); live-bench idles ~35 KB free heap.

## Twin-test record (2026-07-03)
`esp32cyd-live-bench` + `fake_meter.py --watts 200 --steady`: CYD scanned/connected/subscribed
(`subs=1`), STATE showed `power:200, cad:85`; `crank_reader.py` against the CYD's crank
(`8C:94:DF:93:CC:8E`, "Stages 62144", −42 dBm) decoded **200 W / 85 rpm / L50-R50** from
`2f00c800…` frames. Tap-nav (Ride→Setup→More→Workout) + preset load (`wk_target:138`) verified over
the serial console; workout survived a reflash (NVS). Screenshot: `design/render/cyd-ride.png`.

## Touch calibration (the tap-the-crosshair ritual)
Resistive films vary unit to unit, so the CYD build has an **old-school 4-point calibration screen**
(`lib/proxy/TouchCal.h` — pure per-axis least-squares fit + the crosshair renderer, host-tested incl.
an inverted axis): it **auto-runs on first boot** (no stored cal), saves the fit to NVS
(`sb20touch`), and `CydDisplay::readTap` applies it (compiled `CYD_TP_*` defaults until then).
Serial: **`CALTOUCH`** re-runs the ritual · **`CALCLEAR`** wipes + re-runs · **`CALINFO`** prints the
active fit · **`RAWTAP <rx> <ry>`** injects a synthetic raw press — the whole ritual is
**headlessly twin-testable** (verified 2026-07-03: 4 synthetic corner presses through a known
mapping recovered sx=−0.06897/sy=0.09091 exactly, persisted across reboot, CALCLEAR resets).

## Open / next
- Colors on the physical panel (INVON assumed per CYD2USB folklore) — one glance + `INV 0/1` on the
  probe settles it if wrong.
- The RGB LED could mirror StatusLed states (red=searching, green=meter linked, blue=portal).
- Role: with 3 head-unit boards (C3-OLED = shipping beta, S3-Touch = premium, CYD = budget/big-screen),
  the CYD is the cheapest touch option (~AU$23) — a candidate tester/demo unit.
