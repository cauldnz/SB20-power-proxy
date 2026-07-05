# Seeed XIAO nRF52840 Sense — the BLE(/ANT) bridge board

**Status:** bring-up in progress (2026-07-04 overnight). The fourth head-unit-family board, and the
first with ANT capability — target use: the track-bike repeater-with-correction, plus IMU capture.

## Board facts

- **Seeed Studio XIAO nRF52840 Sense** — nRF52840 (1MB flash / 256KB RAM), BLE 5 + ANT-capable
  radio, **LSM6DS3TR-C** 6-axis IMU (internal I2C at 0x6A, power gated by
  `PIN_LSM6DS3TR_C_POWER`), PDM mic, RGB LED (active low), no WiFi.
- Enumerates as **VID 2886 PID 8065** (application CDC); desk port **COM18**
  (serial `CA387478E20D0342`). UF2 bootloader (double-tap RST) + adafruit-nrfutil CDC DFU.
- SWD recovery pads are on the underside (needs a J-Link/DAP probe) — treat bootloader/SoftDevice
  surgery as brick-risk unless staged carefully (see §ANT).

## Toolchain (PlatformIO)

- Stock PlatformIO has **no XIAO nRF52840 board defs**. Use **maxgerhardt/platform-nordicnrf52
  `#develop`** (the `master` branch lacks the XIAO boards — cost a debug loop) with board
  **`xiaoblesense_adafruit`** = the Adafruit-fork core: Bluefruit dual-role BLE (like the ESP32
  proxies), FreeRTOS, S140 SoftDevice.
- GOTCHA: if a stock `nordicnrf52` platform is already installed, PlatformIO silently uses it
  instead of the git-URL fork (same name) → "Unknown board ID". Delete
  `~/.platformio/platforms/nordicnrf52` first.
- Project: `firmware-nrf/` (env `xiao-sense`).

## ANT: the licensed path (S340)

ANT (and concurrent ANT+BLE) needs Nordic's **S340 SoftDevice** + an **ANT+ network key**, both
licensed via a free thisisant.com adopter account and **not redistributable** — they live in
`firmware-nrf/vendor/softdevice/` (gitignored; see its README for the exact download steps).

**Prior art (all read-to-understand; reimplement where licenses demand):**
- [orrmany/SDAntplus](https://github.com/orrmany/SDAntplus) — ANT+ profiles piggy-backed on
  Bluefruit52 for CONCURRENT BLE+ANT against S340. License: "(Modified) MIT" — read the full text
  before vendoring anything; the sd_ant_* wrapper layer is thin enough to write clean regardless.
- [cujomalainey/ant-arduino](https://github.com/cujomalainey/ant-arduino) — framework-neutral ANT
  driver, best for serial ANT radios; SoftDevice mode exists.
- Integration recipe (blogarak.wordpress.com, S340 + Adafruit Feather nRF52840 — the same core we
  use): custom board json (`"softdevice": {"sd_flags":"-DS340","sd_name":"s340",
  "sd_version":"6.1.1","sd_fwid":"0x00B9"}`), custom linker script (app FLASH origin **0x31000**
  vs S140's 0x26000; RAM origin 0x20006000), S340 API headers into the core's
  `cores/nRF5/nordic/softdevice/s340_nrf52_6.1.1_API/include/`, and a **bootloader rebuilt against
  S340** (the Adafruit bootloader checks the SD fwid; see Adafruit_nRF52_Bootloader issues #73 and
  #146).
- **Brick-risk staging:** prove the SoftDevice swap on the **nRF52840 dongle first** (COM13 —
  Nordic Open Bootloader, button-forced DFU = recoverable), which doubles as the ANT signal source
  for twin tests. Only then repeat on the Sense (UF2 bootloader replacement over CDC DFU; SWD pads
  are the only recovery if it goes wrong).

## Architecture (mirrors the ESP32 proxies)

- Same pure core reused by reference: `firmware/lib/proxy/` (`Cps.h` codec, `Correction.h`).
- Radio seam: `IRadioSource` / `IRadioSink` so the bridge composes any of BLE-in/ANT-in ×
  BLE-out/ANT-out once S340 lands. BLE-in → BLE-out ships first (Bluefruit central + peripheral).
- No WiFi ⇒ the control/telemetry surface is a **custom GATT service** (contract in
  `firmware-nrf/GATT.md`) consumed by the **shared web SPA** ([`web/`](../../web/README.md)) and the
  Garmin Connect IQ app (Edge 540 / Epix Gen 2).
- **The web UI is shared with the ESP32** (2026-07-05): `web/index.html` is ONE self-contained SPA
  behind a `Transport` seam — `BleTransport` (Web Bluetooth, served from GitHub Pages for the nRF)
  and `HttpTransport` (the ESP32 serves the *same file* at `GET /app` + a JSON API). A UI change
  lands on both; the palette is generated from `design/tokens.json` (also the LVGL RGB565). See the
  "Shared web UI" section of `PROJECT-MAP.md`. **Calibration profiles are portable** across the nRF
  (Curve GATT char), the ESP32 (`/curve`), and the desk tooling — same `CorrectionCurve`, one profile
  JSON (`decisions.md` 2026-07-05).

Sources: [SDAntplus](https://github.com/orrmany/SDAntplus) ·
[ant-arduino](https://github.com/cujomalainey/ant-arduino) ·
[S340+PlatformIO recipe](https://blogarak.wordpress.com/2020/03/29/platformio-ide-integration-for-the-nrf52840-feather-express-with-s340/) ·
[Adafruit bootloader S340 issue](https://github.com/adafruit/Adafruit_nRF52_Bootloader/issues/73)

## Can we do ANT+ WITHOUT the S340 SoftDevice? (clean-room research, 2026-07-05)

**Question (owner):** while thisisant activation is pending (≤1 business day), can we skip the
licensed S340 entirely and hand-roll ANT+ (concurrent with BLE) in clean-room firmware?

**Answer: technically the radio can, legally the protocol docs allow it — but for THIS project
it's the wrong tool, and it can't get us off the adopter hook anyway. Three findings:**

1. **The radio hardware can generate ANT frames bare-metal — "the SoftDevice unlocks the radio"
   is a myth.** The nRF52840 `RADIO` peripheral is a generic 1 Mbps GFSK transceiver; register-
   level packet TX/RX with a configurable access address + CRC is routine (Nordic's own ESB /
   Gazell and the NordicSnippets bare-metal examples all do exactly this). ANT's on-air framing
   (1 Mbps GFSK, ANT+ device profiles on RF channel 57 = 2457 MHz) is within that capability. The
   S340 provides the ANT **MAC/TDMA timing engine + message protocol + BLE coexistence
   arbitration** — a *software stack*, not radio access.

2. **The ANT+ network key is an ON-AIR gate, not a software check — so bare-metal doesn't dodge
   the license.** thisisant is explicit: an ANT device "will not hear transmissions occurring on
   other networks" — the network key materially segregates traffic on air. A perfect bare-metal
   ANT stack therefore *cannot* talk to a real ANT+ power meter or Garmin head unit without the
   **ANT+ managed network key**, which comes only through the (free) adopter agreement the owner
   just applied for. Brute-forcing / sniffing the key to avoid the agreement would be a licensing
   violation AND pointless (it arrives free in ≤1 day). Note: the **public network key** (a real
   value — NOT all-zeros, which is invalid) allows private ANT experimentation, but by definition
   can't interoperate with the ANT+ ecosystem — useless for a repeater whose whole job is talking
   to real ANT+ gear. The protocol docs themselves (ANT Message Protocol & Usage; the ANT+
   Bicycle Power device profile D00001086) are PUBLIC, no NDA — so clean-room *implementation* is
   legal; only the key + the ANT+ trademark are gated.

3. **Concurrent ANT+BLE is the genuinely hard part — and it's exactly what S340 sells.** The
   bridge needs two radios live at once (ANT-in + BLE-out to the web app / head unit). Bare-metal,
   that means writing a hard-real-time radio time-slice arbiter between two independent-timing
   protocols — reimplementing the core value of the S340, with brick/timing risk, to replace
   something we get free.

**The one bare-metal option that IS worth keeping in the back pocket:** a **single-protocol** ANT
mode — ANT-in → correct → ANT-out, NO concurrent BLE — becomes tractable precisely because it
drops the arbitration. It can even use the *licensed* ANT+ key loaded as DATA (an 8-byte array in
the gitignored key header, not the SoftDevice binary) — clean-room-legal once we're an adopter. A
viable fallback ONLY if S340 proves problematic on the XIAO's UF2 bootloader.

**Recommendation: wait for the S340** (free, ≤1 business day, gives concurrent ANT+BLE = the
bridge's actual need). Tonight stays BLE + Connect IQ; the drop-zone watch stays armed. Sources:
[Network Keys / Managed Network](https://www.thisisant.com/developer/resources/tech-bulletin/network-keys-and-the-ant-managed-network) ·
[public key FAQ](https://www.thisisant.com/developer/resources/tech-faq/how-do-i-get-the-public-network-key) ·
[nRF52 ANT (SoftDevice) ](https://www.thisisant.com/developer/components/nrf52832) ·
[NordicSnippets bare-metal radio](https://github.com/andenore/NordicSnippets) ·
[ANT+ Bicycle Power profile D00001086](https://www.thisisant.com).

## IMU recording — VERIFIED live-sampling (2026-07-05)

Proven in two layers:
1. **Record→download→CRC chain** (earlier): 766 samples @52 Hz captured while relaying, downloaded
   over BLE byte-perfect (CRC32 match). The plumbing is sound.
2. **Sensor→buffer liveness** (serial `IMUTEST` self-test, runs the PRODUCTION capture path
   `imu.readRaw* → g_cap.add`, bypassing the flaky desktop BLE stack): 260 samples, **gravity |a|
   = 1.013 g** (real gravity, correct ±16 g scale), axis means **(-0.03, 0.23, 0.99) g** — gravity
   correctly resolved onto +Z with the board flat on the desk (physically right, not fabricated),
   **ax noise 2.5 LSB** (real per-sample ADC noise; a frozen buffer reads 0), **0/260 consecutive
   duplicate samples** (every read hits the ADC). Repeatable across runs. VERDICT: PASS.

The one thing static analysis can't show is a *motion* spike — needs the board physically tapped
(owner away at test time). Serial `IMUTEST` is a permanent diagnostic (send it over USB CDC at
115200); the on-air path stays the production interface. NOTE: a dev reflash wipes the LittleFS
config, so a freshly-flashed board has no source filter and will thrash trying to grab any CPS
advertiser on air (harmless; set a filter via the web app / Config write to settle it).

## ESP32 capability ports (P1–P4, 2026-07-05)

The owner asked which ESP32 features to port; picked all four batches. Status:
- **P1** (#221): correction curve · single-sided ×2 · zero-offset forwarding · RGB status LED.
  All verified on real relayed data (fake_meter → bridge).
- **P2** (#222): **on-device calibration** — the pure `CalibrationSession` (shared) fits a
  power→factor curve from a paired reference-meter ride (2nd Bluefruit central). Fit math proven
  on-device (serial `CALTEST`: DUT 10% low → ×1.111 curve, residual 0.00 W). Two-meter LIVE test
  gated on the owner's real XCadey+Assioma (not the concurrent session's boards).
- **P3**: **BLE OTA** (Adafruit `BLEDfu` — update over Bluetooth, no USB) + **scan-based source
  picker** (pure `SourceCandidate`, exposed via the ScanList characteristic). Picker proven on the
  bench: 4 on-air devices discovered + correctly classified (CPS / FTMS-trainer / Stages-crank),
  strongest-first. Web app shows a tap-to-select list.
- **P4**: **FTMS erg + structured workouts + shifter bias.** A **3rd Bluefruit central** links to
  an FTMS trainer (`0x1826`) and drives its target power via the control point (`0x2AD9`:
  RequestControl→Start→SetTargetPower) from the pure `WorkoutRuntime` (shared with the ESP32). The
  **shifter** feature — the XIAO has no user button — is a signed **target bias** (±W, clamped ±200)
  the web app / Garmin drive via the Workout characteristic (`0x0008`, cmd 8); a physical BLE shifter
  would drive the same cmd as a 4th central via the pure `Shifter` decode. Bench-verified over serial
  (`WKTEST`): erg encoders byte-correct (RequestControl `00`, Start `07`, SetTargetPower(250) `05 FA
  00`), a preset parses + runs (`4×8 Threshold` → 9 segments, 3180 s, first target 138 W), and the
  bias folds into the target (+10,+10,−30 = −10 W → effective 128 W). Web app has a Workout & erg
  card (trainer picker from the ScanList FTMS entries, preset selector, start/pause/stop, live target
  + elapsed, ±10 W shifter buttons). **Live erg-drive against a real trainer is gated** — the only
  FTMS trainer on air (`SB20-FTMS-Server`) belongs to the concurrent session; the encoders + workout
  runtime are proven, only the on-air control loop against a free trainer remains.

**nRF verification pattern (two layers):**
1. **Pure wire-format core → CI.** `firmware-nrf/lib/bridge/` (`Proto.h` + `ImuCapture.h`) is
   board-free, so it has a host Unity suite (`pio test -e native` from `firmware-nrf/`, run in CI
   alongside the ESP32 native tests). 21 golden-vector tests pin every GATT layout documented in
   `firmware-nrf/GATT.md` — Status/Config/Curve/RecCtl/RecState/CalState/**WkState**/ScanList + the
   RecData framing (incl. the seq-254-vs-0xFE-trailer regression) + `ImuCapture` fill/auto-stop/crc.
   These are the byte-for-byte authority the web app (JS) and Connect IQ (Monkey C) mirror, so the
   vectors catch drift in the firmware **or** either mirror.
2. **On-board glue → serial bench.** The desktop Windows GATT cache goes stale on every reflash (it
   caches per device-address and auto-reconnects), so on-board behaviour (radio wiring, live erg,
   calibration collection) is verified over the USB serial console (`IMUTEST`, `CALTEST`, `SCANLIST`,
   `WKTEST`, `SHOW`, `SINGLE1/0`, `CURVE`, `ZERO`) rather than fighting the cache — the reliable path
   for this board.

Also: the Adafruit `Arduino.h` defines `abs/round/min/max` as macros that break `std::` inside the
shared pure headers; `#undef` them before those includes.
