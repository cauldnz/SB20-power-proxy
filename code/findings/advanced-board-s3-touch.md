# "Advanced" board idea — Waveshare ESP32-S3-Touch-LCD-1.47

**Status: ✅ BOOTS + ADVERTISES (2026-07-02, evening) — bring-up UNBLOCKED via the pioarduino platform.**
The full head-unit UI + firmware seam are built and host-tested; the board now **boots the firmware
and advertises the Stages crank on air** after switching the S3 off the stock Arduino-2.0.x/IDF-4.4
core (whose 2nd-stage bootloader crash-looped on this module) onto **pioarduino 55.03.39
(Arduino 3.3.9 / IDF 5.5.4)**. See §"Bring-up RESOLVED" below; the older §"Bring-up status" is kept as
the diagnosis trail. Prior: exploratory planning (2026-06-27) — an **optional "advanced" hardware
tier** alongside the shippable [ESP32-C3 0.42" OLED beta
board](../../06-prior-art-and-references.md) ([[esp32-c3-oled-beta-board]] memory).

## Bring-up RESOLVED (2026-07-02 pm) — pioarduino (Arduino 3.x / IDF 5.5) boots it

**Root cause:** the stock `platform = espressif32@~6.7.0` (Arduino core 2.0.17 / **IDF 4.4**) ships a
2nd-stage bootloader too old for this S3 module. The ROM handed off to it (`entry 0x403c98d0`) and it
**reset before printing its own banner** — `rst:0x3 RTC_SW_SYS_RST` (DIO) / `rst:0x7 TG0WDT_SYS_RST`
(QIO), across every flash-mode/freq combo (DIO/QIO × 80/40 MHz) and on a minimal base sketch. Not a
flash-tuning problem — a **core/bootloader-version** problem. (This corrects the earlier "hangs before
setup()" reading: a boot log captured by holding serial open across a physical RESET showed the crash
is in the **2nd-stage bootloader**, before the app is even loaded.)

**Fix:** new PlatformIO envs on the **pioarduino** platform —
`platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip`
(Arduino 3.3.9 / IDF 5.5.4, modern S3 bootloader). Envs: **`esp32s3-pio-min`** (minimal DIAG probe)
and **`esp32s3-pio`** / `-live` / `-live-bench` / `-ota` (full LCD UI). First boot log (esp32s3-pio-min):
`[sb20proxy] BLE crank proxy starting` → `[diag] alive 0..7 heap=286584` → `NimBLE-init done` →
`spoofing as 'Stages 62144'`; **0 boot-loops**; BLE scan sees **`A4:CB:8F:DA:E9:CD` = "Stages 62144"**
(RSSI −46, CPS + Stages services).

**Four migration gotchas (all fixed in-repo):**
1. **Toolchain install fails under Git-Bash/MSYS** ("MSys/Mingw is not supported" — leaves an empty,
   binary-less `toolchain-xtensa-esp-elf`). **Run all pioarduino `pio` commands from native PowerShell**
   via the penv: `& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m platformio run -e … `.
2. **Arduino 3.x WiFi split** → `fatal error: Network.h: No such file` under the global `deep+` LDF
   (espressif/arduino-esp32#9782 — the `+` variants evaluate preprocessor conditionals and mis-resolve
   the bundled framework libs). Fix: **`lib_ldf_mode = deep`** (no `+`) on the S3 pio envs.
3. Plain `deep` won't auto-discover the flat-layout, manifest-less **`lib/monocypher`** → added a
   minimal `lib/monocypher/library.json` and an explicit `symlink://lib/monocypher` in `lib_deps`.
4. `WiFiClient` no longer transitively included → added `#include <WiFiClient.h>` to `src/net/OtaPull.cpp`;
   the LEDC backlight in `src/disp/LcdDisplay.h` ported to the 3.x pin-based API behind
   `#if ESP_ARDUINO_VERSION_MAJOR >= 3` (`ledcAttach`/`ledcWrite(pin,…)`).

**Flash the S3 (native USB-JTAG, COM16):** the USB-JTAG drops the stub at 460800 (same as the C3), so
`pio -t upload` (which uses 460800) hangs at connect. **Flash the merged image at a safe baud instead:**
`python -m esptool --chip esp32s3 --port COM16 --baud 115200 --before default_reset --after hard_reset
write_flash --flash_size 16MB 0x0 .pio/build/esp32s3-pio/firmware.factory.bin`. Boot log via
`<scratchpad>/capture_s3_boot.py` (resets + holds serial open, since auto-reset is unreliable). Or just
**`python code/scripts/flash_s3.py --env esp32s3-pio[-live] --verify-ble "Stages 62144"`** (picks a good
esptool, flashes, confirms the advert).

**Bench-screenshot caveat:** the serial `SCREEN` console reliably dumps the framebuffer (a clean BMP is
**exactly 165174 B**) on the **mock** build, but on the **-live** build the BLE central's constant
scanning contends with the USB-CDC and corrupts the ~220 KB transfer (oversized BMP → visual "tearing"
that is NOT a render bug — the STATE JSON and the panel itself are fine). For clean bench screenshots use
the mock env; on the live build, trust the physical panel.

## Bring-up status (2026-07-02) — the blocker + exactly what was tried [SUPERSEDED by the section above]

**What's DONE (software, all verified):**
- **PlatformIO env** `esp32s3-touch` (+ `-live`, `-live-bench`, `-ota`) — compiles + flashes clean
  (~1.16 MB image). Pin map from the board BSP: LCD **JD9853** 172×320 on SPI2 (MOSI 39 / SCLK 38 /
  CS 21 / DC 45 / RST 40 / BL 46, INVON, col-offset 34); touch **AXS5106** on I²C (SDA 42 / SCL 41,
  RST 48 / INT 47, addr 0x63).
- **The whole head-unit UI as pure, host-tested code:** `lib/proxy/LcdCanvas.h` (RGB565 + 8×8 font +
  BMP encode), `LcdFont.h` (generated from the public-domain font8x8), `LcdUi.h` — the **5 locked
  screens** (Ride / Workout / Setup / More / Calibrate) + tap hit-testing → typed `UiAction`s.
  **7 host tests** (`test_lcd_*`), and **all 5 screens rendered to PNG and visually confirmed**
  (design/render or the scratchpad ui_*.png).
- **Hardware seam** `src/disp/LcdDisplay.h` (JD9853 init table + SPI blit + AXS5106 read), `main.cpp`
  `lcdTask` (render on core 1 + touch) + a **USB-serial bench console** (`SCREEN` / `TAP x y` /
  `STATE`) and `code/scripts/bench_s3.py` to drive it headless.

**The blocker:** the Arduino image **hangs before `setup()` runs** on this board. Reproduced on the
**minimal base proxy** (`esp32s3-min`: no LCD, no WiFi — near-identical to the working C3 firmware)
→ so it is a **board / Arduino-core issue, not our UI code**. Symptoms: no BLE crank advert (base MAC
`a4:cb:8f:da:e9:cc`), no app serial, and **NVS never written** (empty `sb20perf/reboots` +
`bootstage`) = setup()'s first lines never run.

**Ruled out** (each rebuilt + reflashed + checked for the crank advert):
static-init framebuffer (now lazy-allocated), PSRAM (`BOARD_HAS_PSRAM` removed — S3R8 octal PSRAM
isn't mapped during C++ static init), flash mode (`dio` and `qio`), the LCD code entirely (minimal
build), and USB-CDC-on-boot (`CDC_ON_BOOT=0`). None boot.

**Why it's hard to crack blind:** the board's **native-USB serial is flaky** (a documented project
gotcha — we read the C3 over HTTP for this reason), and even the ROM/USB-Serial-JTAG boot log never
reached the host, so there's **no panic backtrace** to read.

**NEXT STEP (needs a human at the bench):**
1. **Get the panic.** Solder/clip a 3.3 V USB-UART to **GPIO43 (U0TXD)** + GND and monitor at
   115200 — the ROM + panic backtrace come out UART0 regardless of the USB weirdness. That one line
   almost certainly names the fault.
2. **Or switch toolchain.** The known-good reference for THIS board
   (`miguelgarcia/waveshare-ESP32-S3-Touch-LCD-1.47-espidf-platform-template`) is **ESP-IDF**, and
   uses `qio_opi` + `flash_mode=qio` + `psram_type=opi`. Our `platform = espressif32@~6.7.0`
   (Arduino core 2.0.x / IDF 4.4) may be too old for this S3 module — try the **pioarduino** platform
   (Arduino 3.x / IDF 5.x) for the S3 env, or an ESP-IDF build.
3. To re-run the diagnostic: `pio run -e esp32s3-min -t upload --upload-port COM16`, then
   `python <scratchpad>/readboot.py` reads how far `setup()` got from NVS `bootstage`.

**Where the code is:** `firmware/lib/proxy/{LcdCanvas,LcdUi,LcdFont}.h`, `firmware/src/disp/LcdDisplay.h`,
the `#if USE_LCD` blocks in `firmware/src/main.cpp`, `code/scripts/bench_s3.py`. The pure layer is
CI-green; nothing here blocks the C3 path.

---

Idea (original 2026-06-27 planning below): not committed as a product — grounded in the
[capability inventory](../../PROJECT-MAP.md) so each feature reuses what we already have.

## The board (verified spec — waveshare.com / wiki)
**ESP32-S3R8:** dual-core Xtensa LX7 @ 240 MHz · **8 MB PSRAM · 16 MB flash** · **1.47" capacitive-touch
IPS LCD, 172×320, 262K colour, ST7789** · Wi-Fi + BLE 5 · **native full-speed USB** · **microSD (TF) slot** ·
runs **LVGL**.

## 1. Why it matters — its strengths hit our exact pain points

| | C3-OLED (beta default) | S3-Touch-LCD-1.47 (advanced) | What it fixes for us |
|---|---|---|---|
| Cores | 1 | **2** | Headroom / insurance, **not** a fix for a live problem. The C3's early loop-stall was **root-caused to the OLED render blocking the loop and already fixed** (moved off the hot loop → loop-max 96→12 ms, stalls 161→0, a 5-min loaded soak ran stall-free + no reboots — [perf-results](perf-results.md)). Dual-core just gives more margin (and is the easier coex case); "Ride mode: WiFi off" is belt-and-braces, not proven-necessary. |
| RAM | ~400 KB | 512 KB + **8 MB PSRAM** | Headroom for an LVGL UI, OTA image buffering, on-device capture/log, on-device fit. |
| Flash | 4 MB | **16 MB** | Room for a big UI asset set + dual OTA slots + a default-build library. |
| Display | 72×40 mono OLED | **172×320 colour touch** | A real on-device UI → **no phone needed** for setup/ride/calibration. |
| Storage | none | **microSD** | On-device capture logging — the collaboration loop's data, on a card. |
| Flashing | USB-JTAG (wedges — [[esp32-c3-flashing]]) | **native USB-CDC** | No USB-JTAG "no serial data" hangs; clean flashing/recovery. |
| Cost / size | tiny, ~US$3 | bigger, pricier | ⇒ genuinely **optional/advanced**, not the default unit. |

## 2. What it *uniquely* unlocks (the headline two)

1. **Standalone touch head-unit — no phone.** Do the entire flow on the 172×320 touch screen: scan → tap
   your meter → pick crank identity / ×2 → live ride display → calibrate → "send a report." The proxy stops
   being a hidden box you configure by phone and becomes a **self-contained device + display**. *This is the
   real differentiator.*
2. **On-device capture to microSD.** Log raw CPS frames + ride power/cadence to the card → the beta data
   loop becomes "it's a file on the SD," far richer/longer than the `/diag` ring, and we get **on-device
   ride logging** that feeds our SQLite/analysis pipeline ([sqlite-analysis-layer](sqlite-analysis-layer.md)).
3. **Native USB + 8 MB PSRAM / 16 MB flash** — clean flashing/recovery (no C3 USB-JTAG wedge) and headroom
   for LVGL + OTA buffering + on-device fit.

*(Dual-core is a nice-to-have margin, not a headline — the C3's loop-stall was already root-caused and
fixed, and ran stall-free in a loaded soak; see §1 + [perf-results](perf-results.md).)*

## 3. Feature ideas (each reuses an existing capability — just a new render target)

- **Live head-unit dashboard** — colour power chart + both streams + L/R balance bar + erg target; the
  `WebApp.h` dashboard content rendered native via LVGL (same `ProxyStatus`).
- **Touch source picker + diagnostics** — the `/setup` BLE-scan tap-list, a raw-frame viewer, and the
  `qa_board` acceptance card **on-screen** (great for our bench/QA and power-user testers).
- **Touch calibration wizard** — the meter-to-meter corrector (`CalibrationPage`) as touch screens with the
  live coverage grid + fitted-curve preview. No laptop ([meter-to-meter-proxy](meter-to-meter-proxy.md)).
- **Erg / workout console** — the MCP workout / Ride Director segment + target + remaining on-screen; touch
  to skip / extend / nudge target ([mcp-workout-server](mcp-workout-server.md), [ride-director](ride-director.md)).
- **On-screen "Send a report"** — the consent-first `/report` flow, but the SD card makes it a real file to
  copy off (or upload once the OTA backend lands).

## 4. Positioning — a two-tier product

- **C3-OLED = the shippable beta default:** tiny, cheap, hidden behind the bars, phone-setup. What we mail to ~10 testers.
- **S3-Touch = the "advanced" tier:** a self-contained touch head-unit, rock-solid (dual-core), SD logging,
  native-USB. For (a) **us** as the bench/session instrument, (b) the engaged **data-collaborator** testers,
  (c) a later premium SKU. Exactly the "optional advanced board" framing.

## 5. Architecture fit + effort (well-scoped — the core doesn't move)

Our firmware already splits a **pure, host-tested core** (`firmware/lib/proxy/`) from the **hardware seam**
(`firmware/src/{ble,net,disp}/`). Adding this board is:
- a new `platformio.ini` env (`esp32s3-touch-lcd`), **reusing the same `ProxyCore` / CPS / Config / FTMS /
  Calibration logic unchanged** (host tests untouched);
- a new **display/touch seam** under `disp/` (LVGL + ST7789 + the capacitive-touch controller) consuming the
  same `ProxyStatus` / `RuntimeConfig` the OLED + web UI already render;
- the web UI stays (the S3 still serves `/` `/setup` `/report`), so the touch UI is *additive*, not a rewrite.

**Phasing (each a small PR; bench-gated):** P1 bring-up (BLE proxy + status text on the LCD, no touch) →
P2 touch setup/source-pick → P3 touch dashboard + workout console → P4 SD capture logging. *(Pinning BLE to
its own core for extra coex margin is a cheap optional tweak, not a phase — the C3's stall is already
fixed.)* Stop at any phase — even P1 (a colour-status bench unit) is useful; the touch phases (P2+) are the
"advanced" payoff.

## 6. Risks / open questions

- **Small screen (172×320):** fine for a head-unit/status + big-tap-target flows; tight for a dense wizard —
  design few screens, large targets.
- **Effort:** LVGL touch UI is genuine firmware work — keep it **optional** so it never blocks the C3
  pre-beta path (boards arriving ~end June; the bundled bike ride).
- **BLE parity:** same NimBLE stack; the proxy core is platform-agnostic, so low risk — and S3 coex is the
  *easier* case (dual-core). Bench-verify the spoof pairs identically from the S3.
- **Power/mounting:** USB-powered on the bike is fine; confirm whether this variant has a Li-ion charger +
  battery connector if an untethered unit is wanted.
- **Don't let the shiny screen pull focus** from the value-prop (the meter/crank proxy) — this is a *delivery
  vehicle*, not a new product.

## 7. Recommendation

When it arrives, the highest-leverage low-risk first step is **P1**: bring the existing BLE proxy up on the
S3 and show status on the colour LCD — that alone makes it a usable bench/session instrument (native USB,
bigger screen) for almost no new surface. The **touch UI (P3+) is where the actual "advanced" value is** —
no-phone setup, on-screen calibration/workout, SD capture — so that's the part worth investing in if the
tier proceeds. **Don't over-rotate on the dual-core/coex angle:** the C3's loop-stall was already fixed and
soak-clean ([perf-results](perf-results.md)), so the S3 buys *margin*, not a rescue. Treat the S3 as **our
instrument + a touch-UX testbed first, a product tier second** — and keep it strictly optional so it never
pulls focus from the pre-beta path.

## 8. LVGL-era status (2026-07-05)

The S3 now runs the shared **LVGL v9 + Inter** UI (same code as the CYD; `esp32s3-pio*` envs).
Two S3-specific rules learned on the way (full detail in `decisions.md` 2026-07-04/05):

- **Never nest SPI transactions on the IDF5 core** — `setWindow_` owns its own transaction; calling
  it inside another `beginTransaction` deadlocks the non-recursive bus lock SILENTLY (the LVGL task
  froze at its first flush; no panic, console stayed alive).
- **USB-CDC output rules:** anything printed before a host attaches is dropped (`setTxTimeoutMs(0)`),
  and bulk dumps (serial `SCREEN`) need a temporary nonzero TX timeout or the CDC buffer eats them.

Verified on hardware: full touch UI, QR onboarding (owner-run), meter twin chain (fake_meter via
`/log`), 13/13 web-route sweep as `sb20proxy-s3.local`. **Open issue:** ArduinoOTA on the
pioarduino core is DEAF to espota invitations (the C3 OTAs fine with the same tooling) — the S3
needs a USB-data flash to get current; retest OTA on each new build. The board's USB port/cables
have been flaky; a powered-only USB-C charger runs it fine (WiFi-only operation).
