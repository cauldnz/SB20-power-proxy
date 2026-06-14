# Session G — BLE capture spec (what the ESP32 BLE proxy needs)

The goal of Session G is to capture **everything required to (a) decide the BLE path
is viable and (b) impersonate the Stages crank as a BLE peripheral on an ESP32.**
Two complementary capture methods; do both.

- **Active recon** (we play a BLE central, like the bike): characterises the crank's
  peripheral profile and the *crank's* calibration response. Tooling: `06_capture_ble.py`
  + the `raedian-probe` toolkit patterns (`enumerate`/`listen`/`probe_write`). Runs on
  the Windows-side BLE.
- **Passive sniff** (nRF52840 → Wireshark → JSON, per the owner's
  `raedian-probe/docs/sniffer_setup_runbook.md` + `pcap_analyze.py`): captures the
  *bike↔crank* link the active method can't see — bonding/security and the bike's
  exact calibration write.

> **Mode exclusivity (sequence matters).** A crank BLE peripheral accepts one central.
> - Active recon needs the crank **BLE-free** → do it with the bike on **ANT+ cranks**
>   (the crank advertises CPS even while ANT+-paired — confirmed day-1: `Stages 4963`).
> - Passive sniff needs the bike **BLE-paired to the crank** ("Pair with Bluetooth" ON).
> So: ANT+ work + active BLE recon **first**, then flip to BLE cranks for the sniff.

---

## Part A — Active recon (crank BLE-free; we connect as central)

Capture for **both** the Stages crank and the Assioma (the proxy impersonates one,
consumes the other):

1. **Advertising payload** — device name (`Stages 4963` / `ASSIOMA…`), service UUIDs
   (CPS `0x1818`, the Stages **custom service** `d445fe01-…` seen day-1, Device Info
   `0x180A`, Battery `0x180F`), manufacturer data, appearance, flags, TX power.
   → *the ESP32 must advertise byte-identically.*
2. **Full GATT enumeration** — every service / characteristic / descriptor + properties
   (read/notify/indicate/write) + CCCD handles. Don't skip the **Stages custom service**
   — that's the likely home of Stages-specific config (crank-length push? proprietary
   calibration?). → *the ESP32's GATT table must match.*
3. **Static reads** — Device Information (`0x2A29` manufacturer, `0x2A24` model, `0x2A25`
   serial, `0x2A26`/`0x2A27`/`0x2A28` fw/hw/sw), **CPS Feature `0x2A65`** (uint32 bitfield
   — tells the bike what the crank supports), **Sensor Location `0x2A5D`**, Battery
   `0x2A19`. → *values the bike may validate; mirror them (manufacturer-ID question again).*
4. **CPS Measurement `0x2A63` notifications** — subscribe, log flags + field layout +
   rate. → *the format the ESP32 emits with Assioma-derived numbers.*
5. **Calibration response (crank side)** — using a guarded `probe_write`: write the
   Cycling Power **Control Point `0x2A66`** Start-Offset-Compensation op (the BLE
   analogue of the ANT+ zero-reset that gave us `0xAC`/903), and capture the **indication**
   back. → *the response the ESP32 must produce when the bike calibrates.* Also probe the
   Stages custom-service characteristics for any calibration/config there.

Tooling note: add a guarded Control-Point write to `06_capture_ble.py` (mirror
`raedian-probe/probe_write.py`) — one explicit op, log the indication, never a blind loop.

## Part B — Passive sniff (bike BLE-paired to crank; nRF dongle)

With the bike's **"Pair with Bluetooth" ON** and the nRF sniffer running on the
SB20↔crank link, capture a full pairing → zero-reset → pedal → (optional) erg cycle:

6. **Pairing / bonding (SMP)** — does the bike initiate pairing? Just-Works vs LE Secure
   Connections, bonding (key storage), identity/MAC binding? → *the single biggest BLE
   risk — decides how hard impersonation is.*
7. **Connection parameters** the bike requests (interval / latency / supervision timeout).
   → *ESP32 connection config.*
8. **The bike's calibration WRITE** — the exact bytes the SB20 writes to `0x2A66` (and/or
   the custom service) during its zero-reset, and the indication it expects. → *the exact
   request/response to satisfy (the BLE counterpart of the ANT+ handshake we have).*
9. **Steady-state** — confirm the CPS Measurement frames the bike consumes match Part A.
10. **The Stages custom service in action** — what the bike reads/writes there during
    pairing (crank-length config? a Stages-proprietary unlock?). → *may be load-bearing.*

## Part C — The GATE (must answer, or the ESP32 path is void)

11. **Does erg work fully with BLE-paired cranks?** Flip to BLE cranks, set an erg target,
    pedal — does the bike hold power / does resistance control behave exactly as on ANT+?
    Capture the bike's FE-C/FTMS output too if practical.
    - **Yes** → the ESP32/BLE path is live; proceed to impersonation.
    - **No / degraded** → the SB20's BLE crank mode is a dead end for erg; the proxy stays
      ANT+/Pi and ESP32 is shelved. **Find this out before building anything.**

---

## What each captured item gives the ESP32 build

| Captured | Enables |
|---|---|
| Advertising + GATT + static reads (A1–A3) | the ESP32 *presents* as the crank (the bike connects & accepts it) |
| CPS Measurement format (A4) | the ESP32 *streams* Assioma-derived power/cadence in the right shape |
| Calibration response + bike's write (A5, B8) | the ESP32 *survives the zero-reset* the bike issues |
| Bonding/security + conn params (B6, B7) | the ESP32 *completes pairing* the way the bike demands |
| Custom-service behaviour (A5, B10) | covers any Stages-proprietary step the standard profile misses |
| Erg-works gate (C11) | go/no-go for the entire BLE/ESP32 direction |

## Success criteria for Session G

- A complete, committed BLE protocol model of the Stages crank (advert + GATT + reads +
  measurement + calibration), good enough to start an ESP32 NimBLE peripheral from.
- A clear bonding/security answer from the sniffer.
- A definitive **erg-works-on-BLE-cranks** yes/no.

If we only get Part A + C in one session, that's already a go/no-go plus most of the
impersonation surface; Part B (the sniff) can be a focused follow-up.
