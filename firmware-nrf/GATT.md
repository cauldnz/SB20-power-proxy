# The Bridge GATT contract (Web Bluetooth + Connect IQ surface)

The XIAO Sense has no WiFi, so this custom GATT service IS the control/telemetry interface —
consumed by the Web Bluetooth app and the Garmin Connect IQ app. This file is the **contract**;
the pure pack/unpack lives in `lib/bridge/Proto.h` (host-testable) and the JS/Monkey C mirrors
must match it byte for byte.

All multi-byte fields are **little-endian**. First byte of every payload is a format version
(`PROTO_VER = 1`); consumers must ignore payloads with a newer major version.

## Service

- **Bridge service UUID**: `53423230-0000-4bd9-a4ae-1b4e2c633a1d`
  (`0x53423230` = ASCII "SB20"; the `0000` short id selects the service, `0001`… the
  characteristics below share the base.)

| Characteristic | UUID (`5342323e-XXXX-…`) | Props | Purpose |
|---|---|---|---|
| Status | `…-0001-…` | notify, read | live telemetry @2 Hz |
| Config | `…-0002-…` | read, write | correction (scale/offset) + single-sided + identity + radio routing |
| RecCtl | `…-0003-…` | write, notify | recording start/stop/erase/download + state |
| RecData | `…-0004-…` | notify | chunked IMU download stream |
| Curve | `…-0005-…` | read, write | piecewise power→factor correction curve (wins over scale/offset) |
| Calibrate | `…-0006-…` | write, notify | on-device DUT→reference calibration control + state |
| ScanList | `…-0007-…` | read, notify | nearby meters/trainers for the source picker |
| Workout | `…-0008-…` | write, notify | FTMS erg: pick a trainer, load a preset, run it + shifter bias |
| Buttons | `…-0009-…` | read, write | SB20-shifter → action binding + sink enable (P5) |

Full UUIDs: replace `XXXX` in `53423230-XXXX-4bd9-a4ae-1b4e2c633a1d`.

## Status (notify @2 Hz, 20 bytes)

| off | type | field |
|---|---|---|
| 0 | u8 | proto version (1) |
| 1 | u8 | flags: b0 srcConnected · b1 outAdvertising · b2 recording · b3 srcIsAnt · b4 outIsAnt |
| 2 | i16 | src power (W, raw from meter; -1 none) |
| 4 | i16 | out power (W, corrected, as broadcast; -1 none) |
| 6 | i16 | cadence (rpm; -1 none) |
| 8 | i8 | balance (left %, -1 none) |
| 9 | u8 | battery (%; 0xFF unknown) |
| 10 | u16 | correction scale ×1000 (1000 = 1.0) |
| 12 | i16 | correction offset ×10 (W) |
| 14 | u32 | recording sample count |
| 18 | u16 | uptime (s, wraps) |

## Config (read/write, 44 bytes)

| off | type | field |
|---|---|---|
| 0 | u8 | proto version (1) |
| 1 | u8 | flags: b0 srcIsAnt · b1 outIsAnt (ANT bits reject-write until S340 present) · **b2 single-sided ×2** |
| 2 | u16 | correction scale ×1000 |
| 4 | i16 | correction offset ×10 (W) |
| 6 | u8[19] | source name filter (NUL-padded; empty = any CPS) |
| 25 | u8[19] | broadcast identity name (NUL-padded) |

Writes persist to internal flash (LittleFS) and apply live (no reboot — the nRF re-configures the
radio roles in place; a rejected write notifies the old value back via Status). **single-sided ×2**
doubles the source power *before* correction (an R-only crank reports half of total).

## Curve (read/write, variable)

A piecewise power→factor correction curve. When present it **overrides** the Config scale/offset
(Correction.h: the curve wins). Written by the calibration wizard, or manually.

Payload: `[ver, nPoints, {power u16 W, factor u16 milli}...]` — 2 + 4·nPoints bytes, max 8 points.
Factors are ×1000 (1250 = 1.25×), clamped 0.25–4.0. An **empty** write (`[ver, 0]`) clears the
curve, reverting to scale/offset. Persisted to LittleFS (`/curve.bin`). Read returns the active
curve in the same format.

## RecCtl

Write (2 bytes): `[ver, cmd]` — cmd: `0` stop · `1` start · `2` erase · `3` download · `4+rate`:
`[ver, 4, rateHz]` set sample rate (13/26/52/104 Hz; default 52).

Notify (12 bytes): `[ver, state(0 idle·1 recording·2 downloading), rateHz, reserved,
sampleCount u32, capacity u32]` — emitted on every state change and @1 Hz while recording.

## RecData (download stream)

After `download`: a header frame then data frames, sized to the smallest subscriber MTU. Every
frame carries an **explicit type byte** at offset 1 (a seq low-byte of 0xFE once masqueraded as
the trailer and truncated downloads — hence the tag):

- Header: `[ver, 0xFF, rateHz, reserved, sampleCount u32, startMs u32]`
- Data: `[ver, 0xFD, seq u16, count u8, reserved, samples…]` — each sample 12 bytes:
  `ax ay az gx gy gz` as i16 (accel LSB = 0.488 mg @±16 g; gyro LSB = 70 mdps @±2000 dps),
  up to 14 samples/frame at MTU 247.
- Trailer: `[ver, 0xFE, crc32 u32]` (CRC32 over the concatenated sample bytes).

## Calibrate (write control + notify, P2)

On-device calibration: reads the source (DUT) + a reference meter at once, fits a power→factor
correction curve (the pure `CalibrationSession`, shared with the ESP32), and on save writes it to
the Curve characteristic. Needs the reference meter within BLE range during collection (2nd central).

Write `[ver, cmd, ...]`: `1` start `[ver,1, refFilter[≤19]]` · `2` cancel · `3` save (fit if needed,
apply the curve) · `4` discard.

Notify (16 bytes): `[ver, state(0 idle·1 collecting·2 fitted), reserved, pairCount u16, minPairs u16,
residual ×10 W i16, coverage u8[6], enoughToFit u8]` — emitted @1 Hz while collecting so the wizard
shows pairs + per-bin coverage climbing.

## ScanList (read + notify, P3)

Nearby power meters / trainers the bridge has seen, for the web source picker. Read or subscribe;
pushed @≤2 Hz when new devices appear. `[ver, count, {name[19], rssi i8, flags u8}...]` — 21-byte
slots, up to 8 (strongest-first). flags: `b0` isCps · `b1` isFtms (trainer) · `b2` isStagesCrank.

## Workout + erg (write control + notify state, P4)

The bridge can be a **third central** onto an **FTMS trainer** (Fitness Machine `0x1826`) and drive
its **target power** from a structured workout — the ESP32's erg loop, ported. It runs the pure
`WorkoutRuntime` (shared with the ESP32) and speaks the FTMS Control Point (`0x2AD9`:
RequestControl→Start→SetTargetPower). Pick the trainer from a ScanList FTMS entry.

Write `[ver, cmd, ...]`: `1` set trainer `[ver,1, trainerFilter[≤19]]` (empty drops it) · `2` load
preset `[ver,2, presetIndex u8]` · `3` start · `4` pause · `5` resume · `6` stop · `7` unload ·
`8` **bias step** `[ver,8, delta i8]` — the **shifter** feature: nudges the erg target ±delta W
(clamped ±200), on top of the workout prescription. A physical BLE shifter (Zwift Click / SRAM) would
drive cmd `8` as a 4th central via the pure `Shifter` decode; the web app + Garmin drive it too, since
the XIAO has no user button.

Notify (18 bytes): `[ver, flags, targetW i16, segIndex u8, nSeg u8, segRemainS u16, ergSentW i16,
elapsedS u16, biasW i16, reserved u16]` — emitted on every command and @1 Hz while a workout is loaded.
`flags`: `b0` loaded · `b1` running · `b2` paused · `b3` ergConnected (trainer linked) · `b4`
ergControlled (trainer granted control). **`targetW` already includes `biasW`** (what the erg is asked
to hold); `biasW` is broken out so the UI can show the shifter offset. `ergSentW` is the last value
actually written to the trainer's control point.

## Buttons (read/write, 8 bytes, P5)

Sink the SB20's own handlebar buttons (a separate central onto the SB20's vendor button char `0c46be60`
— see `../code/findings/shifter-ble-protocol.md`) and re-broadcast each press as the user-configured
action: an OBC id (to a training app, via the OBC service) or a local erg-target nudge.

`[ver, enabled, act0..act5]` — `enabled` turns the sink on (the board opens the extra central); each
`actN` is an action-option **index** (0 = none) into the shared option order in
`firmware/lib/proxy/Sb20ButtonMap.h` (`sb20ActionOptions`), which the JS + firmware map index↔token.
The 6 slots are LEFT up/down/3rd then RIGHT up/down/3rd. Persisted to LittleFS (`/buttons.bin`); applies
live (toggling `enabled` starts/stops the SB20 central in place). Read returns the current binding.

## BLE OTA (firmware update over Bluetooth, P3)

The board runs the Adafruit **buttonless DFU** service (`BLEDfu`), so firmware updates over BLE —
no USB reflash. To update:

1. Build the DFU package from the compiled firmware:
   `adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application .pio/build/xiao-sense/firmware.hex app.zip`
2. Push it over BLE (needs a BLE-capable link — an nRF52 dongle in the PC, or on-device nRF tooling):
   `adafruit-nrfutil dfu ble -f -pkg app.zip -a <bridge-addr> --name "SB20 Bridge"`

The bridge reboots into the bootloader's OTA mode on the DFU trigger and back into the app when
done. USB DFU (`pio -t upload`) still works as the fallback.

## Memory budget (why recording is bounded)

nRF52840 has 256 KB RAM; SoftDevice + FreeRTOS + BLE stacks leave ~150 KB. The ring budget is
**120 KB** → 10 240 samples → **~3.3 min @52 Hz** (6.5 @26 Hz, 13 @13 Hz). Start/stop over BLE is
therefore the workflow (arm it at the start line); rate 13 Hz covers a full track session.
