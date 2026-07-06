# OpenBikeControl (OBC) — protocol reference + our transmitter

**Status:** **M1 built + host-tested** (the pure `firmware/lib/proxy/Obc.h` ButtonState codec, green under
`pio test -e native`). Transports (BLE / mDNS-TCP) are the next milestones. This is the canonical doc for
the OBC subsystem: what to emit so an OBC-speaking app accepts our SB20 buttons. Sibling to
[`shifter-ble-protocol.md`](shifter-ble-protocol.md) (the read side) — this is the OBC re-presentation.
Issue: [`cauldnz/SB20-power-proxy#247`](https://github.com/cauldnz/SB20-power-proxy/issues/247).

## What OBC is (and why it's clean to implement)

[OpenBikeControl](https://github.com/OpenBikeControl/openbikecontrol-protocol) is an **open, MIT-licensed**
protocol for wireless input devices (buttons/shifters) to control trainer apps (MyWhoosh, Rouvy, iCTrainer,
qz via #4504, …). **MIT means we implement directly from the spec — no clean-room concern** (unlike qz's
GPL producer #4504, which we do NOT copy). Spec repo carries `PROTOCOL.md`, `BLE.md`, `MDNS.md`, and MIT
reference Python examples (a ready-made bench consumer — see §Bench).

## Two transports, one message format

Both transports carry the **identical binary message** (message-type prefix byte + payload). This is the
key enabler for our two boards:

- **BLE (`BLE.md`)** — a GATT service; the notification value IS the message. **The nRF52840's only path
  (no WiFi), and available on the ESP32 too.**
  - Service `d273f680-d548-419d-b9d1-fa0472345229`
  - Button-State char (Read/**Notify**) `d273f681-…` · Haptic (Write) `d273f682-…` · AppInfo (Write) `d273f683-…`
- **mDNS / TCP-UDP (`MDNS.md`)** — advertise `_openbikecontrol._tcp.local.`, stream messages over TCP/UDP.
  **ESP32 (WiFi) only.** qz's producer (#4504) uses **port 21587**; the port is discovered via the mDNS SRV
  record, so any port works if advertised.

No length prefix — BLE notify and UDP datagrams are self-framing; TCP is one-message-per-write.

## Message format (from `PROTOCOL.md` / `BLE.md` / `MDNS.md`)

| Msg | Bytes |
|---|---|
| **ButtonState** `0x01` (device→app) | `[0x01, id, state, id, state, …]` |
| **DeviceStatus** `0x02` (device→app) | `[0x02, battery(0-100 or 0xFF), connected(0/1)]` |
| **Haptic** `0x03` (app→device) | `[0x03, pattern, duration, intensity]` |
| **AppInfo** `0x04` (app→device) | — |

- **state:** `0x00` released · `0x01` pressed · `0x02-0xFF` analog (0x02 min … 0xFF max).
- Notify only on **change**; combine simultaneous changes into one message; max ~20 bytes (1 + 9 pairs).
- Golden vectors (spec examples, mirrored in our host tests): `[0x01,0x01,0x01]` = Shift-Up pressed;
  `[0x01,0x01,0x01,0x02,0x00]` = 0x01 pressed + 0x02 released; `[0x01,0x10,0x80]` = nav-up analog 50%;
  `[0x02,0x55,0x01]` = 85% battery, connected.

### Standard button IDs (subset we use)

`0x01` Shift Up · `0x02` Shift Down · `0x03` Gear Set(analog) · `0x10-0x13` Nav Up/Down/Left/Right ·
`0x14` Select · `0x15` Back · `0x16` Menu · `0x18/0x19` Steer L/R · `0x20` Emote · `0x30` ERG Up
(increase difficulty) · `0x31` ERG Down · `0x32` Skip · `0x33` Pause · `0x34` Resume · `0x35` Lap ·
`0x38` Change Mode. Ranges: `0x80-0x9F` app-specific, `0xA0-0xFF` manufacturer-specific.

### Multiple actions per button (the feature that makes this easy)

A single physical press MAY emit several action IDs at once, so it works across apps without mode-switching.
e.g. an SB20 paddle → `[0x01, 0x01,0x01, 0x30,0x01]` = **Shift Up AND ERG Up** (the shifting app shifts,
the erg app bumps power). This is how the reference `bikecontrol` app maps buttons; it's our default too.

## Our transmitter — plan (real-data-first, pure-core-first)

- **M1 — pure codec `Obc.h`** ✅ **done, host-tested** (`firmware/test/test_proxy/test_main.cpp`,
  `test_obc_*`). `encodeButtonState(actions,n,out,cap)` / `encodeButtonPress` / `encodeDeviceStatus`;
  message-type + button-ID constants; BLE UUIDs; mDNS service + default port. No Arduino/BLE.
- **M2 — SB20→OBC default mapping** (pure, host-testable): each `Shifter.h` button → a set of OBC action
  IDs (default: paddles → Shift + ERG up/down; 3rd → Lap / Select), configurable per #247.
- **M3 — BLE transport seam** — a GATT OBC service + Button-State notify char. **nRF (Bluefruit) + ESP
  (NimBLE).** `firmware-nrf/src/main.cpp` (after the bridge service) and
  `firmware/src/ble/BleCrankPeripheral.cpp` (alongside CPS).
- **M4 — ESP mDNS/TCP-UDP transport seam** — `WiFiServer` + `ESPmDNS.addService` in
  `firmware/src/net/WifiLink.*` (no mDNS today). ESP only.
- **M5 — wiring + config** — `ShifterDebounce → M2 map → Obc.h → enabled transports`; NVS toggle +
  per-button map in `RuntimeConfig` / `ConfigStore` (ESP) and `Proto.h` ConfigPacket (nRF). Compose with
  the CPS spoof; watch C3 coex/heap.
- **M6 — docs + issue close** — this doc + `#247`.

## Bench testing (no qz / MyWhoosh needed)

The spec's **MIT Python examples** are the consumer/producer to validate our output:
`examples/python/ble_trainer_app.py` (subscribes to a BLE OBC device),
`tcp_trainer_app.py` / `mdns_trainer_app.py` (network), `protocol_parser.py` (the canonical decoder),
`mock_device_*.py` (reference producers to diff our bytes against). Golden-vector parity first, then a live
subscribe against a flashed board.
