# SB20 shifter-over-BLE protocol — capture-derived (WIP)

**Status:** discovered in bike session 2 (2026-06-18), partially decoded. The Stages SB20 **broadcasts
its virtual-gear / shifter state over BLE** on a proprietary Stages service. This is a *future*
capability (after the power proxy) — read, and potentially relay or inject, shifts. Source capture:
`captures/SHIFTER-probe-20260618.jsonl`. MIT / clean-room — do not copy GPL prior art.

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
| **Vendor** `0c46be5f-9c22-48ff-ae0e-c6eae1a2f4e5` | `0c46be60-…` | **notify** | **GEAR STATE (decoded below)** |
| | `0c46be61-…` | notify | silent in this capture — unknown |
| **Vendor** `0c46beaf-9c22-48ff-ae0e-c6eae1a2f4e5` | `0c46beb0-…` | notify | silent in this capture — unknown |
| | `0c46beb1-…` | **write-without-response** | candidate WRITE channel — inject shifts? |
| Nordic DFU `fe59` | `8ec90003-…` | write/indicate | Buttonless DFU |

## Char `0c46be60` — gear state

Notifies a short burst on each shifter press. Every message is `<type:u8> 00 <payload>`:

| Type | Layout | Meaning |
|---|---|---|
| `01` | `01 00 <gear:u16 LE>` | current-gear state (repeats several times through a press) |
| `03` | `03 00 <gear:u16> <gear:u16>` | shift event — gear field **twice** (from/to? front/rear? **TBD**) |
| `04` | `04 00 <gear:u16>` | shift-complete confirm (once, at the end of the burst) |

**Gear is a one-hot bitmask** (uint16 LE) — one bit set per gear position:
`0x0001`=g1, `0x0002`=g2, `0x0004`=g3, `0x0008`=g4, `0x0010`=g5, `0x0020`=g6, … (bits 0-5 seen so far).

### Observed press → gear (session 2: user pressed Right ①②③, then Left ①②③)

| Press | type-`01` payload | gear bit |
|---|---|---|
| Right ① | `01000800` | `0x08` (g4) |
| Right ② | `01001000` | `0x10` (g5) |
| Right ③ | `01002000` | `0x20` (g6) |
| Left ① | `01000100` | `0x01` (g1) |
| Left ② | `01000200` | `0x02` (g2) |
| Left ③ | `01000400` | `0x04` (g3) |

The right shifter walked toward higher bits, the left toward lower — consistent with up/down shifting;
exact button→direction mapping is **TBD**. Example burst (Right ①):
`01000800` ×n → `030008000800` → `01000800` → `04000800`. The rider felt a haptic click on each press,
1:1 with the BLE events. (Buttons "1 & 2" per side gave haptic "gear change"; button "3" behaviour is
to be characterised.)

## Open questions → controlled probe (`BIKE-SESSION-3.md`)

1. **Per-button direction.** Press ONE button repeatedly in isolation and watch the bit walk up/down.
2. **Full gear range.** Shift to both ends — how many gears (bits beyond `0x20`? does the field widen
   past one byte?), and does it **clamp or wrap** at the limits?
3. **Type `03`'s two gear fields.** (from, to) during a shift, or (front, rear) of a 2× drivetrain?
   (In session 2 both fields were always equal — favours from/to during a single-dimension shift.)
4. **The silent channels** `0c46be61` and `0c46beb0` — what triggers them? (a second shifter pair? a
   mode/long-press? front vs rear?)
5. **Write channel** `0c46beb1` (write-without-response) — can we **write a gear value** to drive the
   SB20's virtual shifting (inject shifts), reading back via `0c46be60`? Probe cautiously.

## Reuse

Run the probe with `--subscribe-all`; narrate each press and correlate notification timestamps with the
narration. A short Python pass over the JSONL (filter `kind=ble_notification`, `char_uuid` starts
`0c46be`, collapse runs) gives the press timeline directly.
