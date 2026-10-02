# OpenBikeControl (OBC) — protocol reference + our transmitter

**Status (2026-10-02, #367 desk half):** the pure core covers the whole of protocol version 1 and is
host-tested (`pio test -e native`, suite `test/test_obc` plus the older `test_obc_*` in `test_proxy`):
every standard button id, multi-action bindings, DeviceStatus, and the app-to-device Haptic and App
Information decoders. On the **ESP32** both transports are wired and compile on every CI env: BLE carries
all three OBC characteristics and sends DeviceStatus on subscribe; **mDNS/TCP (`ObcNet`) now starts** when
OBC is enabled and WiFi is up, with the spec's TXT record. **Nothing new has run on air yet** (no hardware
this week): the on-air half of #367 — the OBC UUID next to the crank spoof (the full proxy's second
advertising set, #291), the spec's reference apps against every board, qz and MyWhoosh — is still open.
The **nRF52840** is BLE-only (no WiFi) and has not been brought to parity: it still carries only the
Button-State characteristic (its parity review is a later item). Everything new is **off by default**:
the BLE additions only run when the OBC service is on (`obcEnabled` / `obcDevmode` / `obcSinkShifter`),
the TCP transport only when `obcEnabled` and devmode or the shifter sink are on.

This is the canonical doc for the OBC subsystem: what to emit so an OBC-speaking app accepts our SB20
buttons. Sibling to [`shifter-ble-protocol.md`](shifter-ble-protocol.md) (the read side) — this is the OBC
re-presentation. Issues: [#247](https://github.com/cauldnz/SB20-power-proxy/issues/247) (original),
[#367](https://github.com/cauldnz/SB20-power-proxy/issues/367) (complete the spec).

## What OBC is (and why it's clean to implement)

[OpenBikeControl](https://github.com/OpenBikeControl/openbikecontrol-protocol) is an **open, MIT-licensed**
protocol for wireless input devices (buttons/shifters) to control trainer apps (MyWhoosh, Rouvy, iCTrainer,
qz via #4504, …). **MIT means we implement directly from the spec — no clean-room concern** (unlike qz's
GPL producer #4504, which we do NOT copy). Spec repo carries `PROTOCOL.md`, `BLE.md`, `MDNS.md`, and MIT
reference Python examples (a ready-made bench consumer — see §Bench).

## Two transports, one message format

Both transports carry the **identical binary message** (message-type prefix byte + payload):

- **BLE (`BLE.md`)** — a GATT service; the notification value IS the message. **The nRF52840's only path
  (no WiFi), and available on the ESP32 too.**
  - Service `d273f680-d548-419d-b9d1-fa0472345229`
  - Button-State char (Read/**Notify**) `d273f681-…` · Haptic (Write/WWR) `d273f682-…` · AppInfo (Write/WWR) `d273f683-…`
  - Devices MUST advertise the service UUID and MUST NOT rely on manufacturer data (Apple).
- **mDNS / TCP (`MDNS.md`)** — advertise `_openbikecontrol._tcp.local.` with a TXT record (`version`,
  `id`, `name`, `service-uuids`, `manufacturer`, `model`), stream messages over TCP. **ESP32 (WiFi) only.**
  qz's producer (#4504) uses **port 21587**, our default; the port is discovered via the SRV record.
  `MDNS.md` defines **TCP only** — #247's "TCP + UDP" predates that; there is no UDP transport.

No length prefix: a BLE notify/write is one message; on TCP each message frames itself (Haptic is 4
bytes, AppInfo carries its own lengths; `ObcStreamParser` in `ObcApp.h` reassembles split/glued reads).

## Message format (from `PROTOCOL.md` / `BLE.md` / `MDNS.md`)

| Msg | Direction | Bytes |
|---|---|---|
| **ButtonState** `0x01` | device→app | `[0x01, id, state, id, state, …]` |
| **DeviceStatus** `0x02` | device→app | `[0x02, battery(0-100 or 0xFF), connected(0/1)]` |
| **Haptic** `0x03` | app→device | `[0x03, pattern, duration(10 ms units), intensity]` |
| **AppInfo** `0x04` | app→device | `[0x04, version, idLen, id…, verLen, ver…, count, ids…]` (count 0 = all) |

- **state:** `0x00` released · `0x01` pressed · `0x02-0xFF` analog.
- Notify only on **change**; combine simultaneous changes into one message; max ~20 bytes (1 + 9 pairs).
- **Every physical press is its own press/release** — never coalesced, no cooldown (MUST for `0x30`/`0x31`,
  so apps can scale the erg step by press rate).
- Golden vectors (spec examples, mirrored in our host tests): `[0x01,0x01,0x01]` Shift-Up pressed ·
  `[0x01,0x01,0x01,0x30,0x01,0x14,0x01]` multi-action · `[0x02,0xFF,0x01]` no battery, connected ·
  `[0x03,0x02,0x14,0x80]` double pulse 200 ms · AppInfo `zwift` / `1.52.0` / `[0x01,0x02,0x10,0x14]`.

### Standard button IDs (all of them are in `Obc.h` / `obcButtonCatalog`)

Gear `0x01` Shift Up · `0x02` Shift Down · `0x03` Gear Set · `0x04` Chainring Set · `0x05` Cassette Set
(the last three are analog positions: not bindable to a momentary press) · Navigation `0x10-0x13`
Up/Down/Left/Right · `0x14` Select · `0x15` Back · `0x16` Menu · `0x17` Home · `0x18/0x19` Steer L/R ·
`0x1A` Brake · Social `0x20` Emote · `0x21` Push to Talk · `0x24` Screenshot · Training `0x30` Increase
Difficulty (our `erg_up`) · `0x31` Decrease · `0x32` Skip Interval · `0x33` Pause · `0x34` Resume · `0x35` Lap ·
`0x36` Previous Interval · `0x37` U-Turn · `0x38` Change Mode · `0x39` Take a break · `0x3A` Join another
rider · `0x3B` Change route · `0x3C` Cruise Control · View `0x40` Camera View · `0x44` HUD · `0x45` Map ·
`0x46` Spectate (`0x41-0x43` are unassigned in the spec) · Power-ups `0x50-0x52`. Ranges: `0x80-0x9F`
app-specific, `0xA0-0xFF` manufacturer-specific.

## Spec coverage (matches the code at #367's desk PR)

✅ = in the code, host-tested where pure · 🔧 = compiled, not yet run on air · ❌ = not done.

### Protocol content (pure core, shared by every board)

| Spec item | State | Where |
|---|---|---|
| ButtonState encode, multi-action in one message | ✅ | `Obc.h` `encodeButtonState`, `emitObcClicks` |
| Every standard button id (incl. 0x04/05, 0x17, 0x1A, 0x21, 0x24, 0x36/37, 0x39-0x3C, 0x40-0x46, 0x50-0x52) | ✅ | `Obc.h` constants + `obcButtonCatalog` |
| DeviceStatus encode | ✅ | `encodeDeviceStatus` |
| Haptic decode (spec examples) | ✅ | `decodeHaptic` |
| AppInfo decode (spec example; truncation; count 0 = all) | ✅ | `decodeAppInfo`, `ObcAppInfo::supports` |
| TCP framing of app→device messages | ✅ | `ObcApp.h` `ObcStreamParser` |
| Latest AppInfo per link, cleared on that link's disconnect | ✅ | `ObcAppState` |
| Multiple actions per button (configurable), training controls 0x30-0x3C bindable | ✅ | `Sb20ButtonMap.h` (slot = `token+token…`), `ObcShifterSource` |
| Never coalesce presses | ✅ | `ObcShifterSource` (no clock), `ObcOutbox` (FIFO, refuses rather than merges); `test_rapid_presses_are_never_coalesced` |
| Tap vs hold (#289) | ❌ | every press is a momentary click today |
| Bindings follow the app's AppInfo button list | ❌ | AppInfo is stored and logged, not yet used to pick actions |

### BLE transport

| Spec item | ESP32 (NimBLE) | nRF52840 (Bluefruit) |
|---|---|---|
| OBC service + Button-State Read/Notify | 🔧 (on air since session 13) | 🔧 (on air since session 13) |
| Haptic `d273f682` Write/WWR, accepted + logged | 🔧 | ❌ |
| App Information `d273f683` Write/WWR, stored per link | 🔧 | ❌ |
| DeviceStatus `[0x02,0xFF,0x01]` on subscribe | 🔧 (to the subscribing link) | ❌ |
| Advertise the OBC service UUID — devmode | 🔧 scan response | ❌ |
| Advertise the OBC service UUID — next to the crank spoof / shifter sink | ❌ needs the second advertising set (#291, `sb20-full-proxy.md` §3d) | ❌ |
| Per-board name | 🔧 devmode advert `OBC-SB20-NNNN` | ❌ |
| Device Information Service `0x180A` | ⚠️ present (the crank's, or ours in corrector mode) with manufacturer, model, firmware and serial; no Hardware Revision `0x2A27`, which BLE.md lists | not checked |
| No manufacturer data in the advert | 🔧 (none is set) | 🔧 |
| 7.5 ms connection interval, latency < 50 ms | ❌ unmeasured | ❌ |

BLE.md lists only `0x01`/`0x03`/`0x04` for BLE; DeviceStatus on subscribe follows #367 (and MDNS.md).
The spec's reference BLE consumer ignores any notification whose first byte is not `0x01`.

### mDNS/TCP transport (ESP32 only; the nRF has no WiFi)

| Spec item | State |
|---|---|
| `ObcNet` started from `main` when `obcEnabled` AND a press source is on (`obcDevmode` or `obcSinkShifter`) AND the station link is up (never in the portal). The source condition is deliberate: `obcEnabled` is not shown over HTTP and no route clears it (devmode/on sets it, devmode/off leaves it), so the source toggles are the visible off-switches until `/app` has per-transport ones | 🔧 |
| Service `_openbikecontrol._tcp`, instance = the board's OBC name, port `obcPort` (21587) | 🔧 |
| TXT `version=1`, `id=<base MAC hex>`, `name=OBC-SB20-NNNN`, `service-uuids=d273f680-…`, `manufacturer=SB20Proxy`, `model=SB20 Proxy` | ✅ builder (`obcTxtRecord`) · 🔧 published |
| Several consumers at once | 🔧 up to 3 |
| Same ButtonState bytes as BLE, same press | 🔧 one `obcEmit` feeds both (BLE inline, TCP via a loop-drained queue) |
| DeviceStatus on connect and every 30 s | 🔧 |
| Haptic / AppInfo read off the socket | 🔧 through `ObcStreamParser` → `ObcAppState` |
| Ride-mode WiFi-off (`/wifi/off`) policy for mDNS consumers | ❌ not decided |
| Per-transport on/off switch in `/app` | ❌ follow-up PR (with the web work) |

## Identity: one name and one id per board, both transports

`FleetIdentity.h`: `defaultObcName(mac)` = **`OBC-SB20-NNNN`**, NNNN the same MAC digits as the derived
crank name (`Stages 9NNNN`), and `obcDeviceId(mac)` = the base MAC as 12 hex digits (the TXT `id`). It
replaces the fixed `OBC-SB20`, so two devmode boards no longer collide. The `OBC-` prefix stays for our qz
fork's name matcher; the upstream-bound listener and the spec's apps match on the service UUID. In spoof
mode the BLE advert must stay the crank's name (the SB20 pairs to it), so the OBC name appears on BLE only
in devmode until the second advertising set exists (#291).

## Configuring bindings (multi-action)

Each of the 6 SB20 buttons binds to a **slot**: one or more tokens joined by `+`, e.g.
`shift_up+erg_up` (shift in a shifting app, raise erg in an erg app, one press). Tokens: the indexed
dropdown list (`none`, `shift_up`, `shift_down`, `erg_up`, `erg_down`, `lap`, `menu`, `pause`, `bias_up`,
`bias_down` — the u8 index the BLE Buttons char and today's SPA use) plus every clickable token in
`obcButtonCatalog` (`take_break`, `cruise_control`, `skip_interval`, …). Up to 9 actions per button (one
message's worth); the OBC ones go out as ONE combined press and ONE combined release; `bias_*` nudge our
own erg target. Stored as before in the NVS line (`s0,…,s5`) — a single-token slot is the old format,
so every saved binding loads unchanged (`test_old_saved_bindings_load_unchanged`).

Over HTTP, `GET /obc/buttons.json` now returns `{"enabled","actions":[i0..i5],"tokens":["s0",..]}`;
`POST` accepts `"tokens"` (wins) or the old `"actions"` indices. **Caveat until the `/app` follow-up:**
today's SPA reads and writes only `actions`, so saving from `/app` replaces a multi-action or
catalog-only slot with its first indexed action. Set those over the API for now:

```
curl -X POST http://sb20proxy.local/obc/buttons.json \
  -d '{"enabled":true,"tokens":["shift_up+erg_up","shift_down+erg_down","lap","shift_up+erg_up","shift_down+erg_down","take_break"]}'
```

## Bench testing (no qz / MyWhoosh needed)

The spec's **MIT Python examples** are the consumer/producer to validate our output:
`examples/python/ble_trainer_app.py` (scans for the service UUID, sends AppInfo, subscribes, sends
haptic on each press), `mdns_trainer_app.py` / `tcp_trainer_app.py` (network), `protocol_parser.py`
(the canonical decoder), `mock_device_*.py` (reference producers to diff our bytes against). Note the
reference TCP reader frames ButtonState with one `read(128)`, so two of our messages arriving in one TCP
segment would confuse it (not a real app); we write one message per `write()` with Nagle off.

### Devmode HTTP endpoint — the firmware-only test source (no shifter hardware)

A `-live` ESP32 build carries an **OBC Devmode**: it advertises the board as its OBC controller
(`OBC-SB20-NNNN`, with the OBC service UUID in the scan response) instead of the Stages crank, so an OBC
listener (qz's `obclistener`, or the Python examples) discovers and connects to it, and virtual button
presses are driven over HTTP — no shifter, no bike. Devmode also turns `obcEnabled` on, so the same
presses go out over mDNS/TCP.

```
curl -X POST http://sb20proxy.local/obc/devmode/on   # persists + reboots as OBC-SB20-NNNN
curl 'http://sb20proxy.local/obc/press?id=0x30'      # ERG Up   → both transports
curl 'http://sb20proxy.local/obc/press?id=0x01'      # Shift Up
curl  http://sb20proxy.local/obc                     # status + id cheat-sheet
curl -X POST http://sb20proxy.local/obc/devmode/off  # back to the normal crank identity
```

(`/obc`'s help text still says `OBC-SB20`; the routes live in `WebRoutes.h` and are updated with the
`/app` follow-up.) `/log` shows `[obc]` lines: subscribes, TCP consumers, AppInfo, haptic.

### Sink the SB20's own shifter buttons → re-broadcast as OBC (the bike add-on)

The real button source (vs Devmode's virtual presses): read the **SB20's own handlebar buttons** and
re-broadcast them as OBC. The SB20 is a BLE **peripheral** exposing a vendor button characteristic
(`0c46be60`, service `0c46be5f` — see `shifter-ble-protocol.md`), so the box opens a **separate central
role** to the SB20 (it is already the SB20's power-meter *peripheral*) and subscribes to it.

Pure spine (both firmwares): `ObcShifterSource` composes `ShifterDebounce` (decode `0c46be60` + collapse
the ~10–20×/press held-frame stream to one event) with the configurable `Sb20ButtonMap` above.
Host-tested with the real session-3 golden vectors (`test_obc_shifter_source_click_and_debounce`,
`test_multi_action_binding_emits_one_combined_click`, `test_rapid_presses_are_never_coalesced`). Default
binding: paddles → Shift Up/Down, Left 3rd → Lap, Right 3rd → Menu.

- **ESP32** (runtime + web-configurable): `RuntimeConfig.obcSinkShifter` (NVS) → `BleShifterClient` joins
  the shared scan hub, matches the SB20 by name (`"Stages Bike"`), subscribes, and feeds
  `ObcShifterSource` → `obcEmit` (BLE + TCP) and/or a local erg-bias nudge. Enable:
  `curl -X POST http://sb20proxy.local/obc/shifter/on`; bind at `/app` or `/obc/buttons.json`.
- **nRF** (build flag, `-D OBC_SINK_SHIFTER=1`; bindings over the Bridge GATT Buttons char as indices).

Both are **bench-gated** for the on-air step, and qz on a real SB20 additionally needs the full proxy
(#291): qz's connection stops the SB20 advertising, which deadlocked session 13's G2.
