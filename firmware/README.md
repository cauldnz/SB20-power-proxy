# sb20proxy — ESP32 firmware (initial cut)

A **dual-role BLE proxy**: read a real power meter (BLE *central*), apply a correction, and
re-present it to the Stages SB20 as a **spoofed crank** (BLE *peripheral*, Cycling Power
Service). The device version of the Python proxy in `../code/`.

Layout + conventions follow `cauldnz/raedian-probe`'s firmware: the **platform-agnostic core in
`lib/proxy/`** is host-unit-tested with **no hardware**; `src/main.cpp` + `src/ble/` are the only
Arduino/NimBLE files.

> **Status: initial scaffold, verified.** The pure core — CPS codec (power **and cadence**),
> linear + **non-linear (GridTransform) correction**, the ProxyCore relay, the HTTP status model —
> is **host-tested green** (`pio test -e native`, 17/17). The full firmware **compiles clean for
> the ESP32-C3** (NimBLE 2.2.0): the default BLE build at 37% flash, and the WiFi+OTA+HTTP build
> (`esp32c3-ota`) at 50% of a 1.9 MB OTA slot. The SB20-specific bits — matching the real Stages
> CPS flags, the exact calibration handshake, bonding — are gated on **Session G**
> (`../11-ble-and-esp32-path.md`). Erg won't work on the bike until that's captured.

## Architecture (mirrors the Python proxy: source → transform → target)

```
 BleMeterClient ──▶ ProxyCore(Correction) ──▶ BleCrankPeripheral ──BLE──▶ SB20
  (IPowerSource)       (the pure relay)         (ICrankOutput)
       ▲                                              ▲
   MockMeter  (host tests / bench)            MockCrank  (host tests)
```

- `lib/proxy/` — **platform-agnostic, host-tested**: `Cps.h` (power **+ cadence** codec +
  `CrankCadence`), `Correction.h` (linear + the non-linear `CorrectionCurve`/GridTransform),
  `ProxyCore.h` (the relay), `Status.h` (the HTTP status model), the `IPowerSource`/`ICrankOutput`
  interfaces, `MockMeter`/`MockCrank`, `Config.h`. The interface+mock pattern is lifted from your
  `raedian-probe` `IChargerControl`.
- `src/ble/BleCrankPeripheral.*` — `ICrankOutput` over NimBLE: advertise CPS, notify power **+
  cadence** (Crank Revolution Data), answer the zero-reset control point with the captured offset.
- `src/ble/BleMeterClient.*` — `IPowerSource` over NimBLE: scan → connect → subscribe → decode power.
- `src/net/WifiLink.*` — WiFi + OTA + HTTP status (only when `USE_WIFI`), mirroring the
  `raedian-probe` boot-guard / `/update` / `/` failsafe idiom.
- `src/main.cpp` — wires them (or `MockMeter` when `USE_MOCK_METER`).

## Build / test / flash

```bash
pio test -e native                          # host tests of the pure core — no hardware (17/17)
pio run  -e esp32c3-supermini -t upload     # your ESP32 Super Mini (BLE only, no creds needed)
pio device monitor                          # watch the [proxy] log

# WiFi observability + wireless flashing (copy wifi_secret.example.h -> wifi_secret.h first):
pio run  -e esp32c3-ota -t upload                       # first time, over USB
pio run  -e esp32c3-ota -t upload --upload-port <ip>    # thereafter, over the air
curl http://<ip>/                                       # live status JSON
```

Envs: `esp32c3-supermini` (the Super Minis you have, BLE only) · `esp32c3-ota` (adds
WiFi+OTA+HTTP) · `esp32s3-waveshare` (refine the board id + wire the touch display when it arrives).

## Try it now (no SB20)

`USE_MOCK_METER` is on by default: flash a board and it advertises as **"Stages 62144"** pumping a
ramping 100–300 W. Pair a phone CPS app or a Garmin and watch the watts — the BLE version of the
ANT+ witness test we just did on the Fenix.

## Next

1. **Session G** (the gate) — confirm BLE-crank erg works on the SB20 and capture the bike↔crank
   exchange (bonding + the calibration write); use `../code/scripts/06_capture_ble.py` and the
   `raedian-probe` recon toolkit (`scan`/`enumerate`/`listen`/`probe-write`).
2. Match the **full** Stages CPS flags (`0x2F`: pedal balance + accumulated torque on top of the
   crank-revolution/cadence we already emit) and finalise the control-point handshake against the
   captured exchange.
3. `BleMeterClient` against a real Assioma; populate `CorrectionCurve` from a calibration ride (the
   XCadey case); on-device setup UI (OLED on the C3, touch on the S3); NVS-persisted config.

## Provenance / license

New, clean-room C++ (MIT). Architecture from this project's `11-ble-and-esp32-path.md` and the
`raedian-probe` firmware conventions (lib/core + native tests + NimBLE 2.x). No GPL code (in
particular, no qz/qdomyos-zwift source).
