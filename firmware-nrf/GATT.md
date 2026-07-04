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
| Config | `…-0002-…` | read, write | correction + identity + radio routing |
| RecCtl | `…-0003-…` | write, notify | recording start/stop/erase/download + state |
| RecData | `…-0004-…` | notify | chunked IMU download stream |

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
| 1 | u8 | routing: b0 srcIsAnt · b1 outIsAnt (BLE when clear; ANT bits reject-write until S340 present) |
| 2 | u16 | correction scale ×1000 |
| 4 | i16 | correction offset ×10 (W) |
| 6 | u8[19] | source name filter (NUL-padded; empty = any CPS) |
| 25 | u8[19] | broadcast identity name (NUL-padded) |

Writes persist to internal flash (LittleFS) and apply live (no reboot — the nRF re-configures the
radio roles in place; a rejected write notifies the old value back via Status).

## RecCtl

Write (2 bytes): `[ver, cmd]` — cmd: `0` stop · `1` start · `2` erase · `3` download · `4+rate`:
`[ver, 4, rateHz]` set sample rate (13/26/52/104 Hz; default 52).

Notify (12 bytes): `[ver, state(0 idle·1 recording·2 downloading), rateHz, reserved,
sampleCount u32, capacity u32]` — emitted on every state change and @1 Hz while recording.

## RecData (download stream)

After `download`: a header frame then data frames, 180-byte max payloads (fits DLE MTU 185):

- Header: `[ver, 0xFF, rateHz, reserved, sampleCount u32, startMs u32]`
- Data: `[ver, seq u16lo, seq u16hi, samples…]` — each sample 12 bytes:
  `ax ay az gx gy gz` as i16 (accel LSB = 0.488 mg @±16 g; gyro LSB = 70 mdps @±2000 dps),
  i.e. 14 samples per frame. Ends with `[ver, 0xFE, crc32 u32]`.

## Memory budget (why recording is bounded)

nRF52840 has 256 KB RAM; SoftDevice + FreeRTOS + BLE stacks leave ~150 KB. The ring budget is
**120 KB** → 10 240 samples → **~3.3 min @52 Hz** (6.5 @26 Hz, 13 @13 Hz). Start/stop over BLE is
therefore the workflow (arm it at the start line); rate 13 Hz covers a full track session.
