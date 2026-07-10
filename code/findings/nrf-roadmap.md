# nRF52840 device — completeness roadmap (ANT+ · spoof · generic boards)

**Goal (owner, 2026-07-08):** invest in the nRF device to make it *complete* — lean into its two
differentiators the ESP32-C3 can't match: **ANT+** and the small no-WiFi form factor — and support the
incoming **generic nRF boards** (Seeed XIAO now → **Adafruit-Feather clones as the primary target**).

**Decisions taken:** (1) **port the SB20 crank spoof to the nRF** (as a mode, not a replacement); (2)
Seeed now, Feather clones primary; (3) do **P1–P5**. Constraint while the owner is away skiing (≤1 wk):
**no hardware on the home dev box** → everything here is built host-tested / compile-first; the on-air
and brick-risk steps are staged as run-sheets below. Owner **has the thisisant login** — S340 + key
coming.

## Where the nRF stands (code-verified 2026-07-08, three Explore agents)

**Complete on BLE** (all bench/serial-verified): source read → correct → re-broadcast · on-device
calibration · FTMS erg + workouts + shifter bias · IMU recording+download · OBC shifter-sink + Buttons
char (#250) · full Bridge GATT (9 chars) · LittleFS persistence · the shared web SPA drives it all · a
Garmin Connect IQ in-ride remote (builds Edge540/Epix2; record proven, erg/shifter on-device pending).

**The gaps this roadmap closes:**
- **No SB20 crank-spoof** — the nRF is *corrector-only* (honest identity, plain CPS frames), so as-is it
  **can't drive a real SB20**. The ESP32's `BleCrankPeripheral` (0x2F framing, `CP_FEATURE_STAGES`, the
  **442 company-id + captured mfgData**, the Stages proprietary service, the dual-crank pairing rule) is
  not ported.
- **ANT+ is stubbed + hard-rejected** — `configWriteCb` rejects the ANT routing bits
  (`main.cpp` ~639) "until S340 present"; zero ANT radio/codec code. Needs the licensed **S340
  SoftDevice + ANT+ network key** (both a free but login-gated thisisant.com download) and a from-scratch
  ANT channel layer. No `IRadioSource`/`IRadioSink` seam exists yet (the docs' "seam ready" is aspirational).
- **No board abstraction** — hardcoded to the XIAO via external-variant pin macros. The **RGB LED**
  (`LED_RED/GREEN/BLUE`) is the one *compile* blocker on any other board; the **IMU** already fails safe
  at runtime (`g_imuOk`) but still links (flash cost).
- Minor: cadence hardcoded −1 in the Status notify; IMU capacity doc drift (8192 samples in code vs 10240
  in GATT.md).

## Owner-action gates (login-only, out-of-repo — do when convenient)
1. **S340 / ANT** — thisisant.com adopter → download **S340 SoftDevice** + the **8-byte ANT+ network
   key** → drop into `firmware-nrf/vendor/softdevice/` (see its README). Unblocks all ANT *radio* work.
   The firmware is wired so the ANT seam **compiles in automatically when `ant_network_key.h` appears**.
2. **Garmin CIQ SDK** — Garmin developer login + a local signing key, only if iterating the Connect IQ
   app. Independent of ANT.

## Phases

### P1 — C++ ANT+ Bike Power page codec ✅ DONE (2026-07-08, commit 676e140)
`firmware-nrf/lib/bridge/AntBikePower.h` — pure, host-tested (nRF native 27/27). Encoders (power-only
0x10, crank-torque 0x12, common 0x50/0x51/0x52, calibration 0x01) + a decoder for the source read.
Mirrors the Phase-0-validated `code/src/sb20proxy/ant/pages.py` byte-for-byte, incl. the captured
`01 AC FF FF FF FF 87 03` offset-903 calibration vector. Needs neither S340 nor hardware. **Unblocks P4.**

### Spoof the SB20 — TWO paths (owner, 2026-07-10: do both)
The SB20's crank↔bike link is **internally ANT+** (`sb20-hardware-reference.md`; session 7 measured
"SB20 = Stages crank 1:1" on ANT), while the app pairs the crank over **BLE**. So there are two ways for
the nRF to impersonate the Stages crank — a config-selectable `spoof|corrector` mode gates them (default
corrector, the honest identity, stays right for the track-bike product):

- **BLE Stages spoof** ✅ DONE (2026-07-10; host-tested + both envs compile; on-SB20 = R3). A runtime
  `spoof|corrector` mode (`ConfigPacket.spoof`, flags bit 3, default corrector) ported from the ESP32
  `BleCrankPeripheral`: spoof emits the **Stages 0x2F** frame (`encodeStagesCpsMeasurement`; crank rev
  passes through from the source, accumulated torque integrated per rev from corrected power like the ESP
  `publishPower`), presents the Stages identity at boot (DIS SPM2, `CP_FEATURE_STAGES` 0x0008030B,
  sensor-location 0, name `Stages 62144`, the Stages proprietary service in the scan response + a battery
  service), and answers the app's 0x10 enhanced-offset with the **442 company-id + captured `mfgData`**
  (BLE zero offset 0). Framing switches live; the identity is boot-time (reboot to re-advertise; serial
  `SPOOF1`/`SPOOF0` toggles). nRF native **28/28**. **Next: R3** — pair to a real SB20, confirm power +
  the calibrate/zero handshake (the session 8–9 criteria). **Follow-ups:** a web-SPA mode toggle (the
  Bridge Config char already carries the bit); consider aligning `firmware-nrf` to gnu++17 (it builds at
  gnu++11 — see decisions.md 2026-07-10 for the ODR gotcha this forced).

- **ANT+ Stages spoof** 🔧 (codec DONE in P1; the ANT radio is P4, S340-gated). The nRF-native path: an
  ANT+ **master** on the Stages crank's channel params (device # 62144, device-type **0x0B** Bike Power,
  tx-type 5, period 8182, rf 57 — from `code/…/ant/master.py`) broadcasting Bike Power pages **as the
  Stages crank** (power-only 0x10 + crank-torque 0x12 + common 0x50/0x51/0x52, and answering the
  calibration/zero request with the offset-903 response) so the SB20 takes it over its **own internal
  ANT+ receiver** — potentially the *most faithful* spoof. **P1's `AntBikePower.h` already provides every
  encoder this needs** (incl. `encodeCalibrationResponse`); it becomes a **mode of the P4 ANT master**.
  Open question (on-air, R2/R3): does the SB20 accept a *spoofed* ANT+ crank on its internal link? The
  Python `ant/master.py` + `openant_master.py` already target this identity — the desk codec is proven,
  only the SB20's acceptance is untested. **Uniquely the nRF** (the ESP32-C3 has no ANT).

### P2 — Generic-board seam + Feather env  ✅ DONE (2026-07-10, commit 01869f9)
`firmware-nrf/src/board.h` (`boardLed`/`boardLedBegin` + `BOARD_HAS_RGB_LED`/`BOARD_HAS_IMU`) routes the
LED off the XIAO-only RGB macros; new `[env:feather-nrf52840]` (`-DBOARD_FEATHER`, Adafruit bootloader
family). **Both envs compile** (xiao-sense no-regression + feather RAM 46%/Flash 23%). The only compile
blocker off the XIAO was the RGB LED; the IMU driver links + fails safe. **Minor follow-up (deferred):**
`#if BOARD_HAS_IMU`-gate the IMU include/object to reclaim its flash on Feather — not needed to build.
<details><summary>original plan detail</summary>
1. New `firmware-nrf/src/board.h`: `boardLed(r,g,b)` + `boardLedBegin()` mapping — XIAO drives the RGB
   triad, single-LED boards map to `LED_BUILTIN` (lit if any channel on). Route `setLed()`/pinMode/status
   colours (`main.cpp` ~106,~918,~1329) through it. Cap flags `BOARD_HAS_RGB_LED` / `BOARD_HAS_IMU`.
2. Gate the IMU behind `#if BOARD_HAS_IMU` (mirror the `OBC_SINK_SHIFTER` flag pattern): the include,
   `imu` object, setup, `imuSelfTest`, and the capture loop; move the Seeed `lib_deps` under the xiao env.
3. New `[env:feather-nrf52840]` (`board = adafruit_feather_nrf52840` — same Adafruit bootloader family, so
   UF2 + buttonless DFU keep working; `-DBOARD_FEATHER`, no IMU). Compile **both** envs green.
Effort: ~1 hr for a Feather; the pure core needs zero changes. (Nordic dongle / bare module = +½ day of
bootloader/upload plumbing — see R1.)
</details>

### P3 — S340 SoftDevice swap  🔧 HARDWARE (brick-risk; run-sheet R1) — gated on the S340 download
Custom board json (`softdevice s340 6.1.1 fwid 0x00B9`), custom linker (app FLASH origin **0x31000** vs
S140's 0x26000), S340 API headers, a **bootloader rebuilt against S340**. **Stage on the recoverable
Nordic dongle first** (Open Bootloader, button-forced DFU), then the Sense (SWD pads are the only
recovery). Prove BLE still works post-swap before touching ANT.

### P4 — Radio seam + ANT channels  🔧 (seam/scaffolding now; ANT radio gated on S340)
Introduce `IRadioSource`/`IRadioSink`, refactor the BLE central/peripheral behind it (compile-verify,
BLE-only). Add — guarded so it **compiles in when `ant_network_key.h` is present** — an **ANT slave**
channel (read a power meter, mirror `sources/ant_power.py`) and an **ANT master `BIDIRECTIONAL_TRANSMIT`**
channel (broadcast power, mirror `ant/openant_master.py`: dev# 62144, type 0x0B, tx-type 5, period 8182,
rf 57), both using **P1's `AntBikePower.h`** codec. Replace the `configWriteCb` ANT-reject (~639) with
real role switching. On-air twin-test dongle→Sense → run-sheet R2.

### P5 — Third-party electronic shifters over ANT → OBC  🔧 (needs ANT + a capture; the product payoff)
Read SRAM AXS Controls / Shimano Di2 D-Fly (ANT-only → *uniquely the nRF*) and map their buttons to OBC,
reusing the OBC work. Per `obc-shifter-sources.md` / issue #249. Needs P4 + a captured shifter ANT stream
(run-sheet R4) — SRAM AXS (ANT+ Controls profile) is the most reachable; Di2 is private-ANT (key recovery
+ clean-room capture).

## Hardware run-sheets (for the owner on the road / on return)
- **R1 — S340 swap, dongle-first.** Download S340+key → drop in `vendor/softdevice/` → apply the board
  json + linker + bootloader-rebuild on the **dongle** (recoverable) → confirm BLE still enumerates+works
  → repeat on the Sense. Details: `nrf52-sense.md §ANT`.
- **R2 — ANT on-air.** With S340 live: flash the P4 build, use the dongle as the ANT power source (or a
  real ANT+ meter), confirm the nRF reads it + re-broadcasts ANT+ that a Garmin sees. Watch coex.
- **R3 — spoof on the real SB20.** Flash the spoof-mode build, pair to the SB20 (both crank IDs findable),
  confirm power + the calibrate/zero-reset handshake completes (the 442 + mfgData reply) — the session
  8–9 criteria, now on the nRF.
- **R4 — shifter ANT capture.** Sniff a SRAM AXS / Di2 shifter's ANT stream (the dongle + `sniff` tooling)
  → decode the button pages → feed P5.

## Verification pattern (this project's nRF discipline)
Pure wire-format core → host Unity tests in CI (`pio test -e native` from `firmware-nrf/`). On-board glue
→ the USB serial console (`IMUTEST`/`CALTEST`/`WKTEST`/`SHOW`/…) because the desktop GATT cache goes stale
on every reflash. See `nrf52-sense.md`.
