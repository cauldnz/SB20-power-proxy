# Session 11 — OpenBikeControl (OBC) on the bike: transmit → shifter-sink → web-config

**Status: PLANNED (2026-07-08)** · Board: C3-OLED bike board (LAN **192.168.1.165**, COM9) · Branch:
`feat/obc-web-config` (stacked on `feat/obc-transmitter` #248; both CI-green, 9 ahead of `origin/main`,
0 behind). Observer: **`code/scripts/obc_reader.py`** (decoder self-test PASS). ~45–60 min.

> **Why now:** the whole OBC feature stack (PR #248 transmitter + Devmode + shifter-sink, PR #250 web
> config) is host-tested and compiles, but **nothing is proven on air**. This session closes that — with
> a real SB20 and a real observer — before the owner is away. Session 10 (three-board erg/UX) stays
> `🟢 READY` and **deferred** — a separate concern; this is the OBC session.

## What we're proving (3 gates, each stands alone)

1. **G1 — OBC transmit + observe** (no SB20): a virtual press (`/obc/press`) reaches a real BLE consumer.
2. **G2 — Sink the SB20's own shifter → OBC** (the headline): press a real SB20 paddle → the mapped OBC
   action comes out. **The dual-role coex + ~8 KB heap is the risk.**
3. **G3 — Web-config round-trip** (PR #250): change a button's action live, see the new action.

## Topology
```
  SB20 (Stages Bike 0105, peripheral)  ──vendor char 0c46be60──►  C3 (central)
                                                                    │  reads shifter
  C3 (also a peripheral, advertises "OBC-SB20")  ──OBC Button-State (d273f681)──►  obc_reader.py (central)
```
The C3 is **not** spoofing power here (it advertises `OBC-SB20`, not the Stages crank) — it's the OBC
add-on: central *to* the SB20 for the shifter, peripheral *for* the OBC output. The SB20 only needs to be
**powered on** (no ride, no Zwift) so the C3 can connect + the paddles fire `0c46be60`.

## Turnkey kit (all verified at the desk)
- **Observer:** `python code/scripts/obc_reader.py` — scans for `OBC-SB20`, subscribes, prints each
  decoded press. Decoder proven: `python code/scripts/obc_reader.py --self-test` → SELF-TEST PASS. Runs
  on the bike machine (native Windows + bleak).
- **Board control (curl over WiFi):** `GET /obc` (status + cheat-sheet) · `GET /obc/press?id=0xNN` ·
  `POST /obc/devmode/{on,off}` (persists+reboots) · `GET/POST /obc/buttons.json` `{enabled,actions[6]}` ·
  `GET /log` (serial-over-HTTP — the live instrument) · `GET /stats` (heap + watchdog resets).
- **Flash:** OTA (board is online) `firmware/flash.ps1` **or** USB `python code/scripts/flash_c3.py
  --env esp32c3-oled-live-ota --port COM9`. Board currently on spoof v0.1.0 — **restore it at the end**.
- Default button map (indices `1,2,5,1,2,6`): LEFT up/down/3rd → Shift Up / Shift Down / **Lap**;
  RIGHT up/down/3rd → Shift Up / Shift Down / **Menu**.

---

## Pre-flight (desk, no bike) — the gates that de-risk the ride

**P0 — Flash the OBC firmware.** From `firmware/`: OTA `./flash.ps1` (RSSI pre-flight + reboot verify),
targeting the OBC `-live-ota` build on this branch. ✅ = board reboots, `curl http://192.168.1.165/status`
answers, `curl http://192.168.1.165/obc` prints the cheat-sheet.
- ❌ fallback: USB flash on COM9 via `flash_c3.py` (needs the cable).

**P1 — Endpoint smoke (no radio peers).** `curl http://192.168.1.165/obc/buttons.json` → returns
`{"enabled":false,"actions":[1,2,5,1,2,6]}`. `POST` a change and GET it back (round-trips = the config
path works before we touch BLE). Note baseline `/stats` **heap** here — this is the number to watch.

---

## G1 — OBC transmit + observe (no SB20 needed)

1. `curl -X POST http://192.168.1.165/obc/devmode/on` → board reboots advertising **`OBC-SB20`**.
2. On the bike machine: `python code/scripts/obc_reader.py` → `found 'OBC-SB20' … connected … subscribing`.
3. `curl "http://192.168.1.165/obc/press?id=0x30"` → obc_reader prints **`BUTTON ERG Up (0x30) PRESSED`**.
   Try `id=0x01` (Shift Up), `id=0x35` (Lap).

**✅ PASS:** every `/obc/press` id shows up in obc_reader within ~1 s. **This proves the OBC BLE transmit +
decode end-to-end with zero SB20 dependency** — if G1 fails, stop here (it's a firmware/observer issue,
not the SB20).

## G2 — Sink the real SB20 shifter → OBC ⭐ (the headline + the coex risk)

Keep obc_reader connected (still `OBC-SB20`). Power on the SB20 (idle — no ride).

1. Enable the shifter sink **live** (tests the PR #250 path, no reboot). **The `Content-Type: application/json`
   header is required** — without it the ESP WebServer parses the body as form fields and the JSON is lost
   (the #239 lesson):
   `curl -X POST http://192.168.1.165/obc/buttons.json -H "Content-Type: application/json" -d '{"enabled":true,"actions":[1,2,5,1,2,6]}'`
2. Confirm the C3 grabbed the SB20: `curl http://192.168.1.165/log` shows **`[shifter] SB20 connected`**.
3. **Press each SB20 paddle by hand** and read obc_reader:
   - LEFT up / RIGHT up → `Shift Up (0x01)` · LEFT down / RIGHT down → `Shift Down (0x02)`
   - LEFT 3rd → `Lap (0x35)` · RIGHT 3rd → `Menu (0x16)`
4. **Coex watch (do this throughout G2):** `curl http://192.168.1.165/stats` — heap must stay stable and
   **watchdog resets must stay 0**. The OLED/`/log` should keep updating.

**✅ PASS:** each physical paddle produces its mapped OBC action in obc_reader, and the board stays
healthy (no reset, heap not cratering) across ~2 min of presses. **⚠️ record:** press→OBC latency, any
missed/duplicated presses, heap low-water. **❌ the finding to watch:** if the board resets or the SB20
link drops when the OBC peripheral + WiFi are also up — that's the single-core C3 coex ceiling; capture
`/stats` reset-reason + the `/log` tail (it's a real result, not a failure of the session).

## G3 — Web-config round-trip (PR #250, live)

1. Re-bind a button to a *different, observable* action — e.g. RIGHT 3rd (slot 5) → **ERG Down**:
   `curl -X POST http://192.168.1.165/obc/buttons.json -H "Content-Type: application/json" -d '{"enabled":true,"actions":[1,2,5,1,2,4]}'`
   (`actions` are action-option **indices**: 0 none · 1 shift_up · 2 shift_down · 3 erg_up · **4 erg_down**
   · 5 lap · 6 menu · 7 pause · 8 bias_up · 9 bias_down).
2. `curl http://192.168.1.165/obc/buttons.json` → reflects the new `actions`.
3. Press **RIGHT 3rd** → obc_reader now shows **`ERG Down (0x31)`** instead of `Menu`.

**✅ PASS:** GET reflects the write, and the press produces the newly-bound action — the config path
(SPA `/obc/buttons.json` → NVS → live shifter) works. (Bonus: do the same from the **web app** at
`http://192.168.1.165/app` → "SB20 handlebar buttons" card, to exercise the real UI, not just curl.)

---

## Stretch (only if G1–G3 are green and there's appetite)
- **qz as the consumer** (the real end-to-end): a Linux-desktop qz built with the OBC listener
  (fork PR #1) connects to `OBC-SB20` and a paddle press changes the qz target power / gears. **Needs
  the Linux qz binary built + a BIKE connected in qz** — heavier setup; do only if it's already staged.
- **nRF** (XIAO Sense): the same shifter-sink over the Bridge GATT `Buttons` char (0009), configured from
  the web app over Web Bluetooth. No WiFi/`/log` on the nRF → harder to observe; its own session later.

## Cleanup (do not skip)
- `curl -X POST http://192.168.1.165/obc/devmode/off` and disable the sink, **or** reflash the board back
  to the shippable **spoof** firmware from `origin/main` (`flash.ps1` on `main`) so the bike board returns
  to its normal ride identity. Note in Actual which state it was left in.

## Risks & mitigations
- **~8 KB idle heap + dual-role coex (the main risk).** The C3 already idles low; adding a central to the
  SB20 while the OBC peripheral + WiFi are up may OOM or wedge (we've hit coex hangs before → Ride-mode
  WiFi-off exists for exactly this). Mitigation: G1 first (no central), watch `/stats` heap continuously
  in G2, keep G2 bounded (~2 min), and treat a reset as a **finding** to log, not a session failure.
- **Feature-branch firmware.** This is `feat/obc-web-config`, not `main` — fine for a test; restore
  `main` after (Cleanup). It IS current with `main` (9 ahead, 0 behind), so not stale.
- **Discoverability.** obc_reader finds the board by the **`OBC-SB20`** name (Devmode) — Devmode must be
  ON for the observer to see it, even in G2/G3. That's why P0→G1 enables Devmode and keeps it on.

---

## Actual (fill in live — annotate each gate ✅/❌/⚠️ with the observed obc_reader lines, `/log`, `/stats`)

- **P0 flash:** …
- **P1 endpoints / baseline heap:** …
- **G1 Devmode → obc_reader:** …
- **G2 SB20 shifter → OBC (+ coex/heap):** …
- **G3 web-config round-trip:** …
- **Stretch (qz / nRF):** …
- **Cleanup — board left as:** …
- **Outcome:** …
