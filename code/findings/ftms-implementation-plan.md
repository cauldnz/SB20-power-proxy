# FTMS protocol — implementation plan (overnight, spec-built ahead of capture)

**Status: PLANNED** · authored 2026-06-21 · turnkey brief for an autonomous overnight session.

## Goal

Implement **as much of the FTMS (Fitness Machine Service, `0x1826`) "bike" protocol as possible**,
including **power control (erg)** — the codec, the erg-control client that drives the SB20, the
shifter-nudges-watts mapper, and (time permitting) the trainer-server role — and **validate the
on-air firmware against the real ESP32(s) connected to this machine**, not just compile it.

This closes the loop on the just-built Ride Director: it exposes **`erg_setpoint_w`**, and FTMS
**Set Target Power** is the wire that turns that setpoint into the SB20 actually changing resistance.

## The deliberate exception to real-data-first (owner-approved 2026-06-21)

The project's capture-before-code rule says don't build a codec ahead of the on-bike capture. The
owner has **explicitly relaxed it for FTMS** because the spec is strong and prior art is readable.
The discipline we keep instead:

- **Build from the public Bluetooth FTMS spec + the byte layouts already documented in
  `code/scripts/capture_ftms.py`** (which hard-codes the exact op-codes) and `shifter-erg-control.md`.
- **Golden vectors are clearly labelled _spec-derived_** (a `SPEC_VECTORS` constant with a comment),
  never passed off as real captures. When the on-bike capture lands (Session 4 §C, `capture_ftms.py
  --erg`), those frames become the golden source and **real data wins on any conflict** (reconcile,
  don't overwrite silently).
- **MIT / clean-room.** Read SHIFTR (JuergenLeber/SHIFTR), qz, etc. to *understand* the approach;
  **never copy** — reimplement from the spec.
- Every module carries a header: *"spec-built, pending real-capture validation (Session 4 §C)."*

## Decisions (resolved with owner)

- **Roles:** the **codec** (both directions, all chars) is built regardless. **Erg-control client
  first** (drive the SB20: Request Control → Start → Set Target Power, wired to the Ride Director's
  `erg_setpoint_w` + the shifter-nudges-watts mapper) — the "power control" path — **and the
  trainer-server** (ESP32 presents as a trainer apps can control) if time.
- **Both languages**, mirroring CPS: `code/src/sb20proxy/ble/ftms.py` + `firmware/lib/proxy/Ftms.h`,
  both host-tested. Firmware is the real runtime; Python is the desk twin + the ride-director erg client.
- **Firmware depth:** codec (host-tested) **+ flag-gated seam classes** (FTMS erg client / trainer
  server) **+ a real hardware test loop** against the connected ESP32(s) + the host's own BLE.

## Hardware (verified 2026-06-21 on this machine)

- ✅ **Two ESP32-C3 boards, both up** (confirmed booting + BLE-advertising):
  - `COM9` — MAC `38:44:BE:45:E9:A4` — the **OLED bike board** (answers `sb20proxy.local`, carries the
    Session-4 firmware). **Owner OK'd flashing it**, but prefer COM10 to keep it pristine.
  - `COM10` — MAC `E0:72:A1:70:02:7C` — the **spare/no-OLED dev board** — flash freely.
  - (A separate "USB Device — Descriptor Request Failed" is **not** an ESP32 — owner confirmed there is
    no third board; ignore it.)
- ✅ **Host BLE works** (Realtek BT adapter; bleak sees both boards at strong RSSI) → the **host is a
  reliable BLE FTMS peer**.
- ✅ **Reliable hang-free flashing solved + proven** (~6 cycles, zero wedges): **`code/scripts/flash_c3.py
  --env <env> --port COM10 [--verify-ble NAME]`**, which flashes pio's built bins with **esptool 4.11
  direct** (`--before default_reset --after hard_reset`), time-bounded with retries + a BLE alive-check.
  **`pio run -t upload` FAILS on these boards** — its bundled esptool 4.5.1 (`tool-esptoolpy@1.40501.0`)
  hits the C3 USB-JTAG "No serial data received" bug. **So: build with `pio run -e <env>`, flash with
  `flash_c3.py`.**

**Two hard constraints the run must honour:**
1. **Don't strand the bike board.** Prefer **`COM10` (the spare)** for the hardware loop so `COM9`
   (bike board) is never touched. If `COM9` *is* used, **re-flash + verify the canonical bike build
   (`esp32c3-oled-live-ota`) onto it at the end** so Session 4 isn't blocked.
2. **Flashing is best-effort and non-blocking.** `flash_c3.py` is time-bounded with bounded retries; on
   any failure it **logs and the run moves on** — the desk deliverables (codec + host tests +
   compile-gated seams) stand regardless. (Fallback if a wedge ever recurs: manual bootloader +
   `--before no_reset` + power-cycle — not needed with esptool 4.11.)

## Guardrails

- **Desk-first, hardware-second.** The codec + erg client + mapper are 100% host-tested (hermetic).
  The hardware loop is an *additional* validation tier, gated + non-blocking (above).
- **Git hygiene:** each phase a fresh branch off up-to-date `origin/main` → build + tests in the
  **same commit** → `pytest -q` green + `ruff check src tests` clean (+ `pio test -e native` / target
  compile for firmware) → PR → green CI → regular-merge → delete. Never commit to `main` directly.
- **Tests in the same commit; CI is the gate; never merge red.** If a phase can't go green, leave it
  on its branch, record why, stop.
- **No regressions:** the existing CPS path, Ride Director, and 210 tests stay green.

## Phases (each: branch → build + tests/compile same commit → PR → green CI → merge)

1. **F1 — Codec (Python).** `ble/ftms.py`, mirroring `cps.py`: Indoor Bike Data (`0x2AD2`)
   decode+encode (flags-driven variable layout); Control Point (`0x2AD9`) request encode + request/
   `0x80`-response decode; Feature (`0x2ACC`), Supported Power/Resistance/Speed/Inclination Range,
   Fitness Machine Status (`0x2ADA`), Training Status (`0x2AD3`) parse. `SPEC_VECTORS` golden set.
   `tests/test_ble_ftms.py` (encode↔decode round-trips, every flag combo, the documented op bytes).
2. **F2 — Codec (firmware).** `firmware/lib/proxy/Ftms.h` — header-only C++ twin of F1. Add cases to
   the native test (`pio test -e native`). Target compile.
3. **F3 — Erg-control client + Ride Director loop (Python).** `ble/ftms_erg.py`: a pure
   erg-controller state machine (Request Control → Start → Set Target Power, clamp to Supported Power
   Range, handle `0x80` results, rate-limit) host-tested against an **in-process fake FTMS server**;
   plus a bridge that drives Set Target Power from the Ride Director's `erg_setpoint_w` (on-change,
   clamped). Hermetic end-to-end test (ride director → erg client → fake SB20).
4. **F4 — Shifter-erg mapper (pure).** debounced button event → target ±step (small/big) → clamped →
   Set Target Power bytes (per `shifter-erg-control.md`). Pure + host-tested; a firmware twin in
   `Ftms.h`/a small header if cheap.
5. **F5 — Firmware seams (flag-gated).** `src/ble/FtmsErgClient.{h,cpp}` (NimBLE central: own the SB20
   control point, Set Target Power) and `src/ble/FtmsTrainerServer.{h,cpp}` (NimBLE peripheral:
   advertise FTMS, Indoor Bike Data notify from a power source, Control-Point write+indicate handler) —
   both delegating to `Ftms.h`. New `platformio.ini` envs (`esp32c3-ftms-server`,
   `esp32c3-ftms-ergclient`). Compile-gated.
6. **F6 — Real hardware loop (best-effort, non-blocking).** `code/scripts/ftms_hw_loop.py`; flash with
   **`flash_c3.py`** (build first: `pio run -e <env>`). Prefer **`COM10`** (spare) so the bike board
   stays pristine.
   - **Server seam:** flash `esp32c3-ftms-server` → `COM10`; host (bleak, reuse `capture_ftms.py` client
     logic) connects, Request Control → Set Target Power(N) → assert the board acks + reflects N in
     Indoor Bike Data.
   - **Client seam:** flash `esp32c3-ftms-ergclient` → `COM10`; host stands up a **WinRT FTMS peripheral**
     (per `ble-peripheral-winrt`) → assert the board connects + writes the right Set-Target-Power bytes.
   - **ESP↔ESP (both boards):** `esp32c3-ftms-server` → one board, `esp32c3-ftms-ergclient` → the other;
     observe via serial/`/log`. If `COM9` is used, **restore `esp32c3-oled-live-ota` + verify** at the end.
   - Every flash via `flash_c3.py` (bounded retries + timeout); log + continue on any failure.
7. **F7 — Docs + ledger.** `code/findings/ftms-protocol.md` (the byte-layout reference + spec-built-vs-
   capture-validated status + the hardware-loop how-to); `decisions.md` entry; `forward-plan.md` update;
   link `erg_setpoint_w` → the erg client. Note Session 4 §C remains the real-capture gate.

Order by value/safety: F1→F2→F3→F4 (safe, hermetic, high value) → F5 (compile) → F6 (hardware,
best-effort) → F7. Trainer-server role lands in F5 (firmware) + is exercised in F6 (host-as-client).

## Out of scope / gated

- **On-bike erg proof** — Session 4 §C (`capture_ftms.py --erg`) remains the real gate; this builds
  ahead of it, ready to validate against the captured frames.
- **C3 BLE coexistence** at full load (central→Assioma + peripheral→SB20-crank + WiFi + OLED +
  FTMS) — a bench/bike concern; the seams are flag-gated and tested in isolation, not stacked.
- **Auto-wiring the firmware FTMS into the live proxy build** — kept behind flags; integrating into
  the shipping bike build is a later, coex-measured step.

## Verification

- Per phase: `pytest -q` + `ruff check src tests` + (firmware) `pio test -e native` / target compile,
  all green on the PR before merge.
- Codec: spec-vector round-trips + every flag combination (Python and C++).
- Erg client + ride-director loop: hermetic test vs an in-process fake SB20 FTMS server.
- **Hardware loop (the new tier):** a real Set-Target-Power round-trip over BLE between the ESP32 and
  the host (both seam directions), captured to JSONL; ESP↔ESP if the second board is up. Best-effort —
  a flash wedge is logged, not fatal.
