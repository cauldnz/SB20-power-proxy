# "Advanced" board idea — Waveshare ESP32-S3-Touch-LCD-1.47

**Status: exploratory planning (2026-06-27).** Owner has one arriving. Idea: an **optional "advanced"
hardware tier** alongside the shippable [ESP32-C3 0.42" OLED beta board](../../06-prior-art-and-references.md)
([[esp32-c3-oled-beta-board]] memory). Ideas only — not committed work; grounded in the
[capability inventory](../../PROJECT-MAP.md) so each reuses what we already have.

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
