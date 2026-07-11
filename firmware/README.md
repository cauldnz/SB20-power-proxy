# sb20proxy — ESP32 firmware (initial cut)

A **dual-role BLE proxy**: read a real power meter (BLE *central*), apply a correction, and
re-present it to the Stages SB20 as a **spoofed crank** (BLE *peripheral*, Cycling Power
Service). The device version of the Python proxy in `../code/`.

Layout + conventions follow `cauldnz/raedian-probe`'s firmware: the **platform-agnostic core in
`lib/proxy/`** is host-unit-tested with **no hardware**; `src/main.cpp` + `src/ble/` are the only
Arduino/NimBLE files.

> **Status: initial scaffold, verified.** The pure core — CPS codec (power **and cadence**),
> linear + **non-linear (GridTransform) correction**, the ProxyCore relay, the HTTP status model —
> is **host-tested green** (`pio test -e native`, 45/45). The full firmware **compiles clean for
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
- `src/net/WifiLink.*` — WiFi + OTA + HTTP status + **captive-portal setup** + a **diagnostic
  `/log` endpoint** (only when `USE_WIFI`), mirroring the `raedian-probe` boot-guard / `/update`
  / `/` failsafe idiom (incl. its serve-logs-over-HTTP trick, since C3 serial is flaky).
- `src/net/DebugLog.*` + `lib/proxy/LogBuffer.h` — a recent-log ring (host-tested) mirrored from
  Serial and served at `GET /log`; toggleable (`/log/on` · `/log/off`, persisted in NVS).
- `src/net/WifiCreds.*` — NVS storage for the provisioned WiFi credentials (+ the /log toggle).
- `src/net/ProvisioningDisplay.h` — injectable setup-UX seam (Serial default; an OLED/QR
  module drops in here later).
- `lib/proxy/Provisioning.h` — the **pure** half of provisioning (setup-page render, form
  parse, credential validation), host-tested alongside the rest of `lib/proxy`.
- `src/main.cpp` — wires them (or `MockMeter` when `USE_MOCK_METER`).

## Build / test / flash

```bash
pio test -e native                          # host tests of the pure core — no hardware (45/45)
pio run  -e esp32c3-supermini -t upload     # your ESP32 Super Mini (BLE only, no creds needed)
pio device monitor                          # watch the [proxy] log

# WiFi observability + wireless flashing — NO wifi_secret.h needed (set the network at runtime):
pio run  -e esp32c3-wifi -t upload                      # first time, over USB
#   on boot the device raises the WPA2 AP 'Setup-XXXX' (per-device, XXXX = last 2 MAC bytes; OLED boards show an 8-digit PIN on screen;
#   screenless boards use the default password 'sb20setup' — Config::SETUP_AP_DEFAULT_PASSWORD).
#   Join it, open http://192.168.4.1/, pick your network -> it saves to NVS and reboots onto WiFi.
pio run  -e esp32c3-ota -t upload --upload-port <ip>    # thereafter, over the air
curl http://<ip>/                                       # live status JSON
curl http://<ip>/log                                    # recent log lines (serial-over-HTTP)
curl http://<ip>/log/off                                # disable the log endpoint (persisted)
curl -X POST http://<ip>/forget                         # wipe creds -> reboots into setup (POST: CSRF-guarded)
```

The C3's native-USB serial is unreliable, so a **diagnostic `/log` endpoint** mirrors the log
to RAM and serves it over HTTP — available in both the setup portal and normal operation. It's
**on by default** (and linked from the setup page) and can be toggled at `/log/on` · `/log/off`;
the choice persists in NVS. Secrets (the WiFi password) are never logged.

WiFi setup is via a **captive portal**: there are no credentials to compile in. If the stored
network later can't be joined (moved router, changed password) the device automatically falls
back into the portal so it can be re-provisioned without a USB reflash. `wifi_secret.h` is
optional and only *seeds* the first boot (see `wifi_secret.example.h`).

Envs: `esp32c3-supermini` (the Super Minis you have, BLE only) · `esp32c3-wifi` (WiFi build,
first USB flash) · `esp32c3-ota` (same binary, flashed over the air) · `esp32s3-waveshare`
(refine the board id + wire the touch display when it arrives).

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
