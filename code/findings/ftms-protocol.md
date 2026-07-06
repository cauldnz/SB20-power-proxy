# FTMS — the Fitness Machine Service (0x1826), and our implementation

**Status: built (F1–F6), spec-built ahead of capture, on-air server seam PASS.** The byte-layout
reference + what we implemented + where each piece lives + what's still gated. Built per
[`ftms-implementation-plan.md`](ftms-implementation-plan.md); erg lives here and closes the Ride
Director loop (`erg_setpoint_w` → Set Target Power). Companion to [`shifter-erg-control.md`](shifter-erg-control.md).

## Real-capture status — passive surface VALIDATED 2026-07-06; the erg write round-trip still pending

The CPS codec was always pinned by **real captured Stages/Assioma frames**. The FTMS codec was
spec-built (owner-approved 2026-06-21; the spec is strong and SHIFTR/qz are readable — to *understand*,
never copy; MIT clean-room). **As of 2026-07-06 the bike's FTMS surface is validated against real bytes**
from a passive pre-ride GATT dump of the SB20 (`captures/QDZ-sb20-ftms-gatt-20260706-0739.jsonl`,
`E4:AA:5A:D6:0E:D4`) — see §"Real-capture validation" below and `decisions.md` 2026-07-06 (desk analysis).
The spec codec matched the wire (Feature bits, Power Range layout, Indoor Bike Data bit0 inversion), so
it's now grounded, not guessed.

**Still the gate:** the **`capture_ftms.py --erg` write round-trip** — *does the bike's resistance
actually track a Set Target Power we send?* — remains uncaptured. A qdomyos Peloton ride (2026-07-06)
did NOT answer it: the sniff was polluted by our own boards + iOS rotating addresses and **did not
capture qz's control channel** (`decisions.md` 2026-07-06 + its *correction*). Note the open risk: the
SB20 reportedly "doesn't answer at all" to a naïve control request (qz issue #1649), so our own erg write
may need a specific handshake — this capture is what tells us. The erg drive stays the §14-phase-5 /
recovery-capture gate, and the `SPEC_VECTORS` in `ftms.py` remain spec-derived until we capture a real
erg exchange.

## The service (from the SB20 recon `G-stagesL-ble-recon-20260615-064641.jsonl`)

| Char | UUID | Props | Purpose |
|---|---|---|---|
| Indoor Bike Data | `0x2AD2` | notify | live speed/cadence/power/… |
| Fitness Machine Control Point | `0x2AD9` | write + indicate | commands (erg = Set Target Power) |
| Fitness Machine Feature | `0x2ACC` | read | machine + target-setting capability bits |
| Supported Power Range | `0x2AD8` | read | s16 min, s16 max, u16 increment (W) |
| Fitness Machine Status | `0x2ADA` | notify | events (Target Power Changed, …) |
| Training Status | `0x2AD3` | read + notify | mode |

## Indoor Bike Data (0x2AD2) wire layout

`flags (u16 LE)` then each present field in ascending flag-bit order. **The bit0 inversion is the trap:**
instantaneous speed is present when bit0 (*More Data*) is **0**.

| bit | field | type | units |
|---|---|---|---|
| 0 | *More Data* — Instantaneous Speed present when **clear** | u16 | 1/100 km/h |
| 1 | Average Speed | u16 | 1/100 km/h |
| 2 | Instantaneous Cadence | u16 | 1/2 rpm |
| 3 | Average Cadence | u16 | 1/2 rpm |
| 4 | Total Distance | u24 | m |
| 5 | Resistance Level | s16 | unitless |
| 6 | Instantaneous Power | s16 | W |
| 7 | Average Power | s16 | W |
| 8 | Expended Energy | u16+u16+u8 | kcal, kcal/h, kcal/min |
| 9 | Heart Rate | u8 | bpm |
| 10 | Metabolic Equivalent | u8 | 1/10 |
| 11 | Elapsed Time | u16 | s |
| 12 | Remaining Time | u16 | s |

Spec vector: `4400 b80b b400 c800` = flags `0x0044` (speed-present via More-Data=0 + cadence + power),
**30.00 km/h, 90 rpm, 200 W**.

## Control Point (0x2AD9) — erg

Write `<op>[params]`; the machine indicates `0x80 <req-op> <result> [params]` (result `0x01` success …
`0x05` control-not-permitted). Erg = **Request Control (0x00) → Start/Resume (0x07) → Set Target Power
(0x05 + s16 LE watts)**. Other ops we encode: Reset (0x01), Stop/Pause (0x08 + u8), Set Targeted Cadence
(0x14), Set Indoor Bike Simulation (0x11). On Set Target Power the machine also notifies **Status (0x2ADA)
op 0x08 Target Power Changed + s16 watts**. Spec vectors: `05 fa00` = Set Target Power 250 W; `80 05 01`
= success; `80 05 05` = control-not-permitted.

**Feature (0x2ACC)** = two u32 LE: Machine Features then Target Setting Features. The erg flag is
**Target Setting bit3 = Power Target Setting Supported**. (Machine: cadence bit1, power-measurement bit14.)

## Real-capture validation — the SB20 bike, on air (2026-07-06)

Passive pre-ride GATT dump of the actual SB20 (`captures/QDZ-sb20-ftms-gatt-20260706-0739.jsonl`,
device `E4:AA:5A:D6:0E:D4`; **advertises unnamed** — filter by address, not `--name SB20`). DIS:
mfr "Stages Cycling", model "SB20", serial "H0512210105", fw 1.1, sw 1.12.4+3792.

| Char (read) | Raw | Decoded |
|---|---|---|
| Feature `0x2ACC` | `8a4000000e200000` | Machine `0x0000408a` = cadence·inclination·resistance·power-measure · Target `0x0000200e` = inclination·resistance·**power(erg)**·indoor-bike-sim |
| Supported Power Range `0x2AD8` | `0000a00f0100` | **0…4000 W, 1 W** (this is the erg clamp on the real bike; sim uses 0…1000) |
| Supported Resistance Range `0x2AD6` | `0000ff000100` | 0…255, step 1 |
| Supported Inclination Range `0x2AD5` | `18fce8030100` | −100.0…+100.0 %, 0.1 % (placeholder-wide) |

**Char set on E4:** the 0x1826 chars above **plus** Indoor Bike Data `0x2AD2` + Status `0x2ADA` +
**Control Point `0x2AD9` (write+indicate)**. **No Training Status `0x2AD3`** here (the spec-built table
above lists it — don't assume it). Non-FTMS: CSC `0x1816` (not CPS), Stages proprietary `0xbe5f`/`0xbeaf`,
Nordic DFU `0xfe59`.

**Indoor Bike Data is emitted as SEPARATE frames**, not one combined packet — dispatch on flags each
notification: `0x00c5` = 8 B `cad(u16 ×0.5) power(s16) avgpower(s16)` (e.g. `c5005a00bd000000` → 45 rpm,
189 W); `0x0000` = 4 B speed only; `0x0011` = distance. Confirms the bit0 (More-Data) speed inversion.

**Net:** the bike's own FTMS is fully erg-capable (power-target + writable Control Point + 0…4000 W), so
`FtmsErgClient` (Request Control → Start → Set Target Power) should drive it directly. Unproven only: the
actual resistance response to a Set-Target-Power WE send (the `--erg` round-trip; the §14-phase-5 gate).

## What we built (the implementation map)

| Piece | File | Tested |
|---|---|---|
| Codec (Python) | `code/src/sb20proxy/ble/ftms.py` | host (`test_ble_ftms.py`) |
| Codec (firmware) | `firmware/lib/proxy/Ftms.h` | native (`test_main.cpp`) |
| Erg client + Ride Director loop | `code/src/sb20proxy/ble/ftms_erg.py` | host (`test_ftms_erg.py`, vs an in-process fake) |
| Shifter → erg mapper | `code/src/sb20proxy/ble/shifter_erg.py` | host (`test_shifter_erg.py`) |
| Trainer-server seam (firmware) | `firmware/src/ble/FtmsTrainerServer.*` | compile + **on-air (F6)** |
| Erg-client seam (firmware) | `firmware/src/ble/FtmsErgClient.*` | compile; on-air bench-deferred |
| Test envs | `esp32c3-ftms-server` / `esp32c3-ftms-ergclient` | compile |
| Reliable flasher | `code/scripts/flash_c3.py` | proven (COM10) |
| On-air loop | `code/scripts/ftms_hw_loop.py` | **PASS** |

The Python `ErgController` and the firmware `FtmsErgClient` are twins; the `InProcessFtmsServer`
(Python) and `FtmsTrainerServer` (firmware) are twins — same codec, same handshake.

## On-air proof (F6, no SB20)

`ftms_hw_loop.py` drove the ESP32 FTMS server (on COM10) from the host as the FTMS controller:
Request Control → Start → Set Target Power(225) → **all ACKed, Status reported Target Power Changed →
225 W, Indoor Bike Data streamed 220 W / 90 rpm, Feature erg-capable**. Record:
`findings/captures/F-ftms-hwloop-server-20260621-0116.jsonl`. So the codec + the trainer-server seam talk
FTMS correctly on real BLE. Reliable hang-free flashing (esptool 4.11 via `flash_c3.py`) made this safe
to run unattended; the bike board (COM9) was left untouched.

## What remains (gated / bench)

- **The real gate: Session 4 §C** — `capture_ftms.py --erg` on the bike answers *does the SB20 erg off a
  third-party Set Target Power?* and supplies the real golden frames to validate this codec against.
- **Client-seam on-air test** — `FtmsErgClient` driving an FTMS server, via ESP↔ESP (touches the bike
  board) or a host WinRT FTMS server; compile + logic verified, on-air proof at the bench.
- **Wire erg into the runtime** — connect `RideErgBridge`/`FtmsErgSession` (or the firmware client) into
  the shipping flow so a Power-Zone workout auto-sets the SB20; measure C3 BLE coex (`/stats`) when the
  FTMS role stacks on the CPS spoof + WiFi.
