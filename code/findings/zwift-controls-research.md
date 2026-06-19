# Zwift controllers + the SB20 — research (Click / Play / Ride, virtual shifting, ride-ons)

**Status:** research / orientation — **no code**. Answers the owner's questions (2026-06-19): *how does
Zwift Click work, what does it offer, what makes sense on the SB20, does it route via FTMS or via Zwift,
and is what we actually want really just buttons for steering / ride-ons?* Clean-room — all protocol
facts below are from public reverse-engineering write-ups (cited); **do not copy GPL prior art**
(`SHIFTR`, `qdomyos-zwift/zwiftplay` are GPL-3.0 — read to understand, reimplement clean).

## TL;DR for the SB20

- **Two completely different integrations, for two different contexts** — don't conflate them:
  - **In Zwift (riding the game):** to make the bars *do Zwift things*, the ESP **emulates a Zwift
    controller** (Click/Play). It sends button/shift events to **Zwift**; Zwift does the rest. Routes
    **via Zwift, not FTMS.**
  - **Erg workouts / no Zwift:** the ESP writes **FTMS Set Target Power straight to the SB20** — the
    *"shifter nudges erg watts"* feature in [`shifter-erg-control.md`](shifter-erg-control.md). No Zwift,
    no controller protocol.
- **Virtual shifting on the SB20 is partly redundant** — the SB20 already has its own gears (the shifters
  change the *bike's* resistance through the Stages app). Zwift virtual shifting exists for single-cog
  trainers. So the **highest-value Zwift integration for the SB20 is probably the *non-shifting* controls**
  the bike can't do today: **steering, Ride-Ons, Power-Ups, menu/workout control** — which is exactly the
  owner's instinct ("maybe all we really want is buttons for steering, ride-ons").
- **Emulating a Zwift controller is real work** (custom GATT + a "RideOn" handshake + protobuf, and for
  the newer Play/Ride an **ECDH + AES-256-CCM encrypted** channel). The FTMS erg path is far simpler. Pick
  the path by the goal.

## What Zwift's controllers are, and what each offers

| Device | What it does | Relevance to the SB20 |
|---|---|---|
| **Zwift Click** | A tiny 2-button (`+`/`−`) clicker for **virtual shifting only** (up to 24 virtual gears, paired with the Zwift Cog). BLE-only to Zwift. | Maps cleanly to the SB20's up/down buttons — but shifting is partly redundant (the bike already shifts). |
| **Zwift Play** | Handlebar game controllers (L+R): rocker for **steering + braking**, buttons for **Ride-Ons, Power-Ups, menu navigation, workout skip, teleport**, plus virtual shifting; haptics. | **The interesting one** — these are game controls the SB20's bars can't do at all. |
| **Zwift Ride** | The controllers built into the Zwift Ride frame; same protocol family as Play. | Protocol reference (most reverse-eng work targets Play/Ride). |

**Virtual shifting itself** is a *software* feature: the controller tells Zwift "shift up/down", and
**Zwift recomputes the simulated gear ratio and the resistance and sends that to the trainer** over the
trainer's own connection. The controller never talks to the trainer. Requirement: the **trainer must be
connected to Zwift over BLE / WiFi / Ethernet — *not* ANT+** (Zwift needs the fast resistance channel).
*(Sources: Zwift Insider, Elite KB, Wahoo support — below.)*

### So: FTMS or Zwift? — it routes **via Zwift**

```
  SB20 shifter ──(emulated Click/Play)──▶ ZWIFT ──(FTMS / trainer protocol)──▶ trainer resistance
       press                               (does the gear + resistance math)
```
The shift/button event goes to **Zwift the app**, not to the trainer's FTMS control point. Zwift owns the
gear state and the resistance. (Contrast the erg-shifter feature, where *we* write FTMS Set Target Power
to the SB20 directly and Zwift isn't involved.)

## The Zwift controller BLE protocol (clean-room facts, for orientation)

From public reverse-engineering (cite, don't copy). The **Play/Ride** "Race Controller" (RC1) profile:

- **Service** `00000001-19ca-4651-86e5-fa29dcdd09d1`, with characteristics
  **Measurement/notify** `…0002…`, **Control-Point/write** `…0003…`, **Response/indicate** `…0004…`
  (plus standard DIS `180A`, Battery `180F`). Older write-ups describe the same idea as a serial-port-like
  service with `ASYNC` / `SYNC_RX` / `SYNC_TX` characteristics.
- **Handshake:** begins with the ASCII token **`RideOn`** (`52 69 64 65 4f 6e`). For Play/Ride it's an
  **ECDH key exchange** (curve `secp256r1`): app writes `RideOn 01 02 + <64-byte pubkey>` to the control
  point; device indicates `RideOn 00 09 + <device pubkey>`; both derive a key via **HKDF-SHA256**, then
  the channel is **AES-256-CCM encrypted** (8-byte nonce). The original Click is reportedly simpler
  (RideOn + protobuf, lighter/!encrypted) — **to be confirmed against a captured device.**
- **Messages: Protocol Buffers.** Idle keep-alive is a single `0x15` ~1 Hz. A button event is a short
  (~19–21 byte) protobuf, opcode `0x07`, with varint fields tagging the controls — e.g. pad id, the
  face buttons (Y/Z/A/B), ON/OFF, **shifter**, and the **joystick** (signed L/R for steering, unsigned for
  brake force, 0–100%). *(Source: MAKINOLO "Connecting to Zwift Play controllers".)*

**Implication:** an ESP **emulates a Zwift controller** = a BLE *peripheral* exposing the RC1 service,
answering the RideOn handshake (and, for Play/Ride, doing the ECDH/HKDF/AES crypto — the C3 has hardware
AES/SHA + mbedTLS, so feasible but non-trivial), and emitting protobuf button messages translated from the
SB20's shifter events. Zwift connects to it as a central, exactly as it would a real Click/Play.

## Mapping the SB20's buttons → Zwift

We have the SB20 shifter fully mapped (`shifter-ble-protocol.md`): **6 stateless one-hot buttons** on
`0c46be60` (LEFT/RIGHT × up/down/3rd). They're discrete presses — **no analog axis**, so Zwift Play's
*analog* steering/braking rocker can only be approximated as **discrete left/right / brake** events. A
plausible mapping (emulating a Play):

| SB20 button | Zwift action (example) |
|---|---|
| LEFT up / down | steer left / right (discrete) — or shift up/down |
| RIGHT up / down | Ride-On / Power-Up — or shift up/down |
| LEFT-3rd / RIGHT-3rd (currently unbound) | menu select / workout skip, or a mode toggle |

Exact mapping is a UX choice; the point is the **6 buttons comfortably cover the discrete Zwift actions**
the owner cares about (Ride-Ons, Power-Ups, steering, menu) even without an analog stick.

## What makes sense for the SB20 — recommendation

1. **If the goal is in-game controls (steering, Ride-Ons, Power-Ups, menus):** emulate a **Zwift Play**
   (the RC1 protocol). This is the genuinely additive capability — the SB20 bars can't do any of it today.
2. **If the goal is shifting feel in Zwift:** emulating a **Click** works, but weigh it against the SB20's
   own shifting (which already changes the bike's resistance). Possibly lower value than #1.
3. **If the goal is erg-watts-from-the-bars (structured training, no game):** that's **not** a Zwift
   controller at all — it's the direct-FTMS [`shifter-erg-control.md`](shifter-erg-control.md) feature, and
   it's much simpler. **This is the owner's stated "really want", and it's the lower-effort, higher-certainty
   build** — do it first (gated on the FTMS-erg capture, session 4 §C).

These compose: one ESP could be the crank-power peripheral **and** (in a Zwift session) a Play emulator,
**and** (in an erg session) the FTMS erg controller — different consumers of the same debounced shifter
events. Not all at once; pick the mode.

## Feasibility, risks, unknowns

- **Crypto + protobuf on the C3** for Play/Ride (ECDH secp256r1, HKDF-SHA256, AES-256-CCM, protobuf) — a
  real chunk of work; the original Click may be lighter (confirm by capture).
- **Zwift may reject non-genuine devices** or change the protocol (it has evolved across Click → Play →
  Ride). This is an unofficial, moving target with **no Zwift support**.
- **Coex:** a Zwift session would add a BLE-peripheral role to Zwift on top of the existing roles — measure
  on `/stats` (cf. the loop-stall history). Likely a *Zwift mode* that doesn't run the SB20-crank spoof
  simultaneously.
- **Licensing:** the best references (`SHIFTR`, `qdomyos-zwift`) are **GPL-3.0** — read to understand the
  wire protocol (a fact, not copyrightable), **reimplement clean-room** for this MIT project.

## Research-first plan (no code until understood)

1. **Decide the target & goal** with the owner: Play-style game controls (steering/Ride-Ons) vs Click-style
   shifting vs the (separate, simpler) FTMS erg feature. Recommendation above: **erg feature first**, then
   **Play-style controls** as the Zwift play.
2. **Capture a real device** if one's available (a Click/Play, or sniff the Zwift app's BLE) to ground the
   RC1 service, the RideOn/handshake bytes, and the protobuf button messages — real-data-first, like every
   other protocol here. *(HCI snoop log + Wireshark is the documented method.)*
3. **Confirm Click-vs-Play crypto** (is the original Click unencrypted? cheaper target?).
4. **Prototype the handshake only** (RideOn + key exchange) against Zwift before any button logic — that's
   the make-or-break.

## References (clean-room — read, don't copy)

- Zwift Insider — *All About Virtual Shifting*: <https://zwiftinsider.com/virtual-shifting/>
- Zwift support — *Connecting your Zwift Click*: <https://support.zwift.com/en_us/connecting-your-zwift-click-to-zwift-HycbzrXZp>
- Elite KB — *Zwift Cog & Click virtual shifting*: <https://elitesrl.zendesk.com/hc/en-us/articles/23525319856028>
- Zwift Newsroom — *Zwift Play launch* (steering/braking/Ride-Ons): <https://news.zwift.com/en-WW/226668-zwift-launches-zwift-play/>
- MAKINOLO — *Connecting to Zwift Play controllers* (RC1 UUIDs, RideOn handshake, ECDH/AES, protobuf):
  <https://www.makinolo.com/blog/2023/10/08/connecting-to-zwift-play-controllers/> · *Zwift Ride protocol*
  <https://www.makinolo.com/blog/2024/07/26/zwift-ride-protocol/> · *Zwift Trainer protocol*
  <https://www.makinolo.com/blog/2024/10/20/zwift-trainer-protocol/>
- `ajchellew/zwiftplay` (decoding the Play controllers): <https://github.com/ajchellew/zwiftplay>
- `SHIFTR` (GPL-3.0 — BLE→Direct-Connect virtual-shifting bridge; reference only):
  <https://github.com/Theo-Marshall/SHIFTR-Hardware>
- PedalSmart.blog — SB20 *Fixing Broken Shifters* (mechanical; confirms worn shifters are a known SB20 pain
  point — supports reading/relocating shifter input over BLE as a side benefit): <https://www.pedalsmart.blog/>
