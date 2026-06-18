# SB20 shifter-over-BLE protocol — capture-derived

**Status:** **fully mapped (read side) in bike session 3 (2026-06-19)** — all 6 shifter buttons decoded,
the frame model confirmed, and the key behaviour established: the SB20 emits **stateless button-events**,
not a gear index. Session 2 (2026-06-18) found the channel; session 3 completed and *corrected* the model
(the bitmask is the **button pressed**, not a one-hot gear). This is a *future* capability (after the
power proxy) — read, and potentially re-present, shifts. Source captures:
`captures/SHIFTER-probe-3-20260619-0838.jsonl` (session 3, authoritative) and
`captures/SHIFTER-probe-20260618.jsonl` (session 2). MIT / clean-room — do not copy GPL prior art.

## Product target — emulate a Zwift Click for Zwift virtual shifting

The headline use case (owner, 2026-06-18): **read the SB20's shifter buttons and re-present them to
Zwift as a Zwift Click**, so an SB20 rider gets Zwift's **virtual shifting** (the Zwift Cog + Click
paradigm — <https://us.zwift.com/products/zwift-cog-and-click-upgrade-kit>) using the bike's own
shifters, no extra hardware. The ESP would: (1) subscribe to the SB20's gear char `0c46be60` (decoded
below), (2) act as a BLE peripheral Zwift accepts as a Click / virtual shifter, translating each SB20
press into a Zwift shift-up/down. This composes with the power proxy — **one ESP can be both the
crank-power peripheral and the Click**. **New research target:** the **Zwift Click / Zwift
virtual-shifting BLE protocol** (what Zwift expects) — clean-room; open prior art exists, do not copy
GPL.

**Second consumer of these same button events:** [`shifter-erg-control.md`](shifter-erg-control.md) —
use the shifter buttons to **nudge the SB20's Erg target watts** (write FTMS Set Target Power), a feature
the Stages app lacks. Both features share the read+debounce half decoded below.

## How it was found

`06_capture_ble.py --address E4:AA:5A:D6:0E:D4 --subscribe-all` connects to the **SB20's own GATT**
(it advertises as `Stages Bike 0105`, addr `E4:AA:5A:D6:0E:D4`, services FTMS `0x1826` + CSC `0x1816`)
and subscribes to **every** notify/indicate characteristic — not just power/CSC. At idle the vendor
channels are silent; pressing a shifter makes char `0c46be60` fire. That contrast (silent → bursts on
press) is the signal.

## SB20 GATT (relevant services)

| Service | Char | Props | Notes |
|---|---|---|---|
| DIS `0x180A` | — | read | mfr "Stages Cycling", model "SB20", serial `H0512210105`, FW `1.1`, SW `1.12.4+3792` |
| FTMS `0x1826` | `2ad2` Indoor Bike Data | notify | idle heartbeat alternating `1100af8a00` / `00000000` |
| | `2ada` Fitness Machine Status | notify | silent at idle |
| | `2ad9` Fitness Machine Control Point | write/indicate | |
| CSC `0x1816` | `2a5b` CSC Measurement | notify | idle `020c127ded` (crank-rev, static when not pedalling); sensor location "rear_wheel" |
| **Vendor** `0c46be5f-9c22-48ff-ae0e-c6eae1a2f4e5` | `0c46be60-…` | **notify** | **BUTTON STATE — all 6 shifter buttons (decoded below)** |
| | `0c46be61-…` | notify | **silent** across all 6 buttons + the range walk (sessions 2–3) |
| **Vendor** `0c46beaf-9c22-48ff-ae0e-c6eae1a2f4e5` | `0c46beb0-…` | notify | **silent** across all 6 buttons + walk |
| | `0c46beb1-…` | **write-without-response** | candidate WRITE channel (inject shifts?) — **not yet probed** |

> **The two parallel vendor services** (`0c46be5f`: be60 notify + be61 notify; `0c46beaf`: beb0 notify +
> beb1 write) fit a **main-pods + optional-remotes** layout. Hypothesis (owner, session 3): the silent
> `be61`/`beb0` are the **optional clip-on aero-bar remote shifter pods** — untestable without that
> accessory. Brake-lever buttons are also untested (session-4 candidate).
| Nordic DFU `fe59` | `8ec90003-…` | write/indicate | Buttonless DFU |

## Char `0c46be60` — button state (all notifications carry the BUTTON, not a gear)

Every notification is `<type:u8> 00 <bitmask:u16 LE>[ <bitmask:u16 LE>]`. The `bitmask` is a **one-hot
identifier of which button was pressed** (uint16 LE, only bits 0–5 ever set):

| Button | Bitmask | raw `01`-frame (from the session-3 capture) |
|---|---|---|
| LEFT&nbsp;up   | `0x0001` (bit0) | `01000100` |
| LEFT&nbsp;down | `0x0002` (bit1) | `01000200` |
| LEFT&nbsp;3rd  | `0x0004` (bit2) | `01000400` |
| RIGHT&nbsp;up   | `0x0008` (bit3) | `01000800` |
| RIGHT&nbsp;down | `0x0010` (bit4) | `01001000` |
| RIGHT&nbsp;3rd  | `0x0020` (bit5) | `01002000` |

Left = bits 0–2, Right = bits 3–5. The names "up/down/3rd" are the physical positions; the SB20 does
**not** label them shift-up vs shift-down (it has no gear of its own — see *stateless*).

### Frame model (per press, observed byte sequences)

Each press emits a short burst, all on `0c46be60`:

| Frame | Layout | Meaning | Real example (button = RIGHT-3rd, bit `0x0020`) |
|---|---|---|---|
| **held**  | `01 00 <bit:u16 LE>` | streamed **continuously while the button is held** (~10–20 notifications per press — it streams *state*, not a clean edge) | `01002000` ×n |
| **commit** | `03 00 <bit:u16 LE> <bit:u16 LE>` | the shift "commit"; **both fields are the SAME bit** (NOT a from/to or left/right split) | `030020002000` |
| **terminator** | `04 00 <bit:u16 LE>` **or** `08 00 <bit:u16 LE>` | end of the burst; the **`04` vs `08`** choice is **state-dependent** | `04002000` *or* `08002000` |

- **`04` vs `08` terminator (the one open frame detail).** LEFT-up showed `04` on its first press but
  `08` on all 10 presses of the range-walk. Most likely **gear-changed (`04`) vs no-change / at-limit
  (`08`)** — i.e. the *consumer* (Stages app) had already shifted to a limit, so later presses were
  no-ops. Not yet pinned; an emulator can treat both as "press complete".
- **Debounce required.** Because `01`-frames stream while held (not one-per-press), any consumer/emulator
  **must debounce** — collapse a run of identical `01 00 <bit>` into a single logical button event
  (the `03`/`04`/`08` frames bracket the run and are the cleaner edge to key off).

### Stateless — the headline finding (session 3)

Holding **LEFT-up pressed 10 times** kept the bitmask at `0x0001` every time — **no counter, no wrap, no
clamp, no gear index anywhere in the payload.** The shifter reports *which button was actuated*, and
nothing else; **the virtual-gear index lives in the consumer** (the Stages app / Zwift), not on the bike.
This maps **1:1 onto the Zwift Click model** (a Click is also a stateless up/down button; the head unit
owns the gear) — so re-presenting SB20 presses to Zwift as a Click is a direct translation, not a
state-sync problem. **This is the key enabler for the Zwift-Click feature.**

### Haptics

No haptic on any pod. The "click" the rider feels is the **phone / Stages app** buzzing in response to
the BLE event — which retro-explains session 2's "no haptic on the 3rd button" (true of *all* buttons;
the 3rd just isn't bound to a shift in the app, so no buzz).

## Resolved vs open

**Resolved (session 3):** full 6-button map · frame model (`01`/`03`/`04`|`08`) · stateless (no gear on
the shifter) · silent channels characterised as *not* the main pods.

**Open → session 4 (`sessions/session-04-…`):**
1. **`04` vs `08` terminator** — confirm gear-changed vs at-limit (shift mid-range, watch which fires).
2. **Brake-lever buttons** — do they emit anything? (squeeze under `--subscribe-all`; watch `be61`/`beb0`
   + FTMS Status `0x2ADA`).
3. **Silent channels `be61`/`beb0`** — believed the optional aero-bar remote pods; needs that accessory.
4. **Write channel `0c46beb1`** (write-without-response) — can we *inject* a press? Read side first; probe
   cautiously (could desync the consumer's gear).

## Reuse

Capture with `06_capture_ble.py --address E4:AA:5A:D6:0E:D4 --subscribe-all`; narrate each press and
correlate notification timestamps. A short pass over the JSONL — filter `char_uuid` starting `0c46be60`,
read `data.raw_hex`, collapse runs of identical `01 00 <bit>` — gives the press timeline directly.
