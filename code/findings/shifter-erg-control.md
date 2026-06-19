# Feature: shifter buttons adjust Erg target watts (the SB20's missing feature)

**Status:** feasibility / architecture — **research only, no code yet** (real-data-first: gated on one
unconfirmed capture, see *The gate*). Owner ask, 2026-06-19: *"use the shifter buttons to dial the target
watts up/down when in Erg mode"* — something the Stages app doesn't do. MIT / clean-room.

## The idea

In Erg mode a trainer holds a fixed target power. The Stages app lets you *set* a target but **not nudge
it from the bars mid-effort** — you reach for the phone. This feature closes that gap: **press a shifter
button → the target watts step up/down → the SB20 ergs to the new target.** No phone, no app.

It's now plausible because the two halves are both in hand:
- **Read the shifter** — session 3 fully mapped the SB20's shifter-over-BLE (see
  `shifter-ble-protocol.md`): **stateless one-hot button events** on char `0c46be60`. We can already
  observe every button press.
- **Set the target** — the SB20 is a full **FTMS Fitness Machine** (`0x1826`): its **Control Point
  `0x2AD9`** accepts **Set Target Power** (`0x05` + sint16 LE watts). Erg = writing that op.

So the missing feature is just **wiring the shifter events to FTMS Set-Target-Power** — a translation we
own, sitting between two interfaces the SB20 already exposes.

## The control loop

```
        ┌──────────────────── one ESP (BLE central to the SB20) ───────────────────┐
 SB20   │  subscribe 0c46be60 ──▶ debounce ──▶ erg target ±step ──▶ FTMS 0x2AD9 set │   SB20
shifter │  (button events)        (1 ev/press)   (held in ESP)      target power     │  erg holds
 press ─┼─────────────────────────────────────────────────────────────────────────▶┼──▶ new W
        └───────────────────────────────────────────────────────────────────────────┘
```

1. ESP connects to the **SB20 as a central** and subscribes to the shifter char `0c46be60`.
2. **Debounce** each press to one logical event (the `01 00 <bit>` frame streams while held — collapse a
   run; key off the `03`/`04`/`08` bracket — see `shifter-ble-protocol.md`).
3. Maintain the **target watts in the ESP** (the SB20/shifter is stateless — the consumer owns state).
   A designated *up* button does `target += step`; *down* does `target -= step` (clamp to the SB20's
   Supported Power Range `0x2AD8`).
4. Write FTMS **Set Target Power** to `0x2AD9`. The full erg handshake (from `capture_ftms.py`):
   `Request Control (0x00)` once → `Start/Resume (0x07)` → `Set Target Power (0x05) + <sint16 LE W>`;
   the machine indicates `Response 0x80 | req-op | result` (`0x01` success … `0x05` control-not-permitted).

## Button allocation — by mode (owner, refined 2026-06-19)

The right split depends on the mode, because **shifting is unused in erg mode** (the trainer holds the
target power regardless of "gear"). So:

- **Erg mode → repurpose the four MAIN up/down buttons** for erg adjust (they're idle for shifting in
  erg). Natural mapping is **fine + coarse**:
  - LEFT up / down = **erg +/− small** (e.g. ±5 W)
  - RIGHT up / down = **erg +/− big** (e.g. ±25 W)
  *(exact assignment is UX; the point is 4 buttons → small/big × up/down — no gestures needed for erg.)*
- **The two 3rd buttons (`0x0004` L / `0x0020` R) are RESERVED for *control*, across modes** — Zwift
  actions (backlog), **ESP-device control** (mode/feature toggles, etc.), menus. They're the always-free
  pair (unbound in the app, but they still emit on `0c46be60`), so keep them for cross-mode control
  rather than spending them on erg.

This rests on two things to confirm (session 4 §C + the app dig):
1. **Erg-mode detection.** The ESP reads **FTMS Fitness Machine Status (`0x2ADA`) / Training Status
   (`0x2AD3`)** to know it's in erg, and only *then* treats the main up/down presses as erg-adjust —
   otherwise they shift the bike normally.
2. **Harmless shift-in-erg.** Does the SB20 *do* anything when a shift button is pressed in erg mode? If a
   gear change there has no effect (erg overrides resistance), we just read the BLE press and repurpose it
   with **no Profile change needed**; if it does something unwanted, disable shifting via a Stages-app
   Profile while in erg.

## Input gestures — for the two control buttons (momentary)

Erg doesn't need gestures (4 main buttons cover small/big × up/down). Gestures matter for the **two 3rd
control buttons**, where two physical momentary buttons must cover several control actions. Prior art
exists in *this exact domain*:

- **Chord — both 3rd buttons at once.** Zwift's **SRAM-style** virtual shifting already uses "press *both*
  shift buttons together" (to change the chainring), so a both-buttons chord is a familiar gesture — e.g.
  *enter/exit a mode*. **Open (session-4 §B):** does the SB20 emit a simultaneous press as one frame with
  both bits (`0x0024`) or as two events? — decides whether chords are cleanly detectable.
- **Double-tap (one button).** Standard firmware pattern (QMK *Tap Dance*; ESP `Button2`-style libraries
  give single/double/triple/long with ~3–4 ms debounce) — a second action per button at ~250 ms latency.
- **Long-press / hold — *not* as out as it looked.** Session 3's capture shows the SB20 **streams
  `01 00 <bit>` continuously while held** (~10–20 notifications), so **hold *duration* is observable**.
  Tools like **BikeControl** added long-press for steering over exactly these systems. UX caveat: holding
  a bar button mid-effort is awkward — prefer tap / double-tap / chord; treat hold as a bonus to confirm.

So the two 3rd buttons + (single / double / chord) cover a handful of control actions (mode, menu,
ESP/Zwift) without touching the erg/shift buttons.

## Dependency — the Stages app's button "Profiles"

The Stages Cycling app configures button behaviour via **Profiles** (owner, 2026-06-19; no official docs).
This matters two ways:
1. **No conflict for the 3rd control buttons** — they're already unassigned, so the app won't fight us.
2. **The erg feature repurposes the *main* up/down buttons in erg mode**, so the key question is whether a
   shift press *does anything* in erg (see allocation point 2 above). If erg overrides resistance and a
   gear change is inert, no Profile change is needed; if not, the fallback is **disable shifting in a
   Profile** while in erg. Either way the Profile config is part of the feature's setup story — which is
   why the screenshots below matter.

### What would help — owner can gather (capture-before-code)
No docs exist, so annotated info from the app is real data we'd otherwise guess at. Most useful:
- Screenshots of the **Profiles / button-assignment** screen(s): what actions a button can be set to, and
  whether a button can be set to **None / disabled**.
- Whether the **3rd buttons** appear in the config at all (assignable, or fixed/unused?).
- Whether **shifting can be turned off per-button** (freeing a button for us to proxy).
- What the app calls erg / target-power mode, and whether shift buttons do anything **in erg mode**.

Drop screenshots + notes anywhere I can read them (commit to the repo, or describe in chat) and I'll fold
them in — same capture-before-code discipline as the byte captures.

## ⚠️ The gate — does the SB20 erg off a THIRD-PARTY Set Target Power?

**This is the go/no-go and it is UNCONFIRMED.** We know the SB20 *advertises* the FTMS Control Point and
that the Stages app drives erg through it — but we have **never captured the SB20 accepting a Set Target
Power from anyone but the app**, and FTMS machines can refuse control to an un-paired/secondary client
(`result 0x05 control-not-permitted`). There are **no FTMS/erg captures in `findings/captures/` yet**.

**Resolve it with the tooling we already built:** `code/scripts/capture_ftms.py --erg` does exactly the
recon — `Request Control → Start → Set Target Power` at a few targets, logging the SB20's `0x80`
responses and whether Indoor Bike Data power tracks. This is **Session G Part C**, pre-staged but never
run. **One bike capture answers it.** Until then, no firmware is built (capture-before-code).

- **If the SB20 accepts it →** the feature is real; build it grounded in the captured FTMS frames.
- **If it refuses (control-not-permitted) →** the feature needs the ESP to be the **sole** FTMS controller
  (no Stages-app erg at the same time), or it's blocked over BLE — which feeds the *alternative-app* path.

## Open design questions (beyond the gate)

- **FTMS single-controller contention.** FTMS expects one controller. If the Stages app holds control,
  can the ESP also write? Most likely the ESP must **own** the FTMS control (no concurrent app erg) — which
  is fine, and aligns with the no-app direction below.
- **Erg-mode detection.** Only nudge when the bike is actually in erg/target-power mode (vs SIM/level).
  Read **Fitness Machine Status `0x2ADA`** / **Training Status `0x2AD3`** / the Indoor Bike Data flags to
  know the mode; otherwise a press might fight the app or do nothing.
- **Coex on the C3 (real).** The proxy already runs **central→Assioma** + **peripheral→SB20 (crank)** +
  WiFi + OLED. Adding **central→SB20 (shifter + FTMS)** is a 3rd/4th BLE role on one radio — heavy (cf. the
  loop-stall history). Mitigations: gate the feature behind a build flag; consider a **dedicated device**
  for shifter-erg; or only hold the SB20-central link while erg is active. Measure with `/stats`.
- **Safety / feel.** Clamp to the Supported Power Range; rate-limit writes; ignore the held-frame stream
  (one step per physical press). A runaway target is a bad ride.

## Relationship to the other shifter consumer (Zwift Click)

This feature and the **Zwift Click** idea (`shifter-ble-protocol.md` → re-present SB20 presses to Zwift
as a Click) are **two consumers of the same shifter events**. They share the read+debounce half; they
differ only in the *output*: this writes the **SB20's own FTMS erg target** (ESP = central to the SB20),
the Click presents to **Zwift** (ESP = a peripheral Zwift connects to). Worth a shared "debounced shifter
event" seam so both can be built on it. (Zwift-Click protocol research is the separate
`zwift-click-protocol.md` thread.)

## Longer-term — an alternative to the Stages app

The Stages app is expected to lose support in the next year or two (owner, 2026-06-19). Everything the app
does for a ride is **already reachable over BLE** and increasingly mapped here: power-source selection
(we relay the Assioma), **erg target (FTMS Set Target Power)**, shifter input (`0c46be60`), and the bike's
own metrics (Indoor Bike Data, CSC). So an **ESP + a lightweight phone/web companion** could become a
self-contained replacement for the core ride-control: pick the power source, set/hold erg, **nudge erg
from the bars (this feature)**, and virtual-shift. This feature is a natural first brick in that wall —
it's the smallest piece that delivers something the app *can't*. Capture as a strategic direction; don't
over-scope it yet.

## Plan (capture-first, mirrors the CPS bring-up)

1. **Gate (bike):** run `capture_ftms.py --erg` — does the SB20 erg off a third-party Set Target Power?
   Commit the JSONL. *(Add to a session doc — natural fit for session 4/5.)*  ← **nothing below starts until this passes.**
2. **Codec (desk, gated):** `Ftms.h` (firmware) + `sb20proxy.ble.ftms` (Python) — Indoor Bike Data decode +
   Control Point encode/decode — **golden-vectored from the captured frames**, host-tested in the same
   commit (exactly as `Cps.h`/`cps.py` were).
3. **Erg-shifter logic (desk, gated):** a pure, host-tested mapper: debounced button event → target
   ±step (clamped) → Set-Target-Power bytes. No hardware needed to test the logic.
4. **Central + write (firmware, gated):** ESP central to the SB20 (subscribe shifter, own the FTMS control
   point), behind a build flag; bench-test against a fake FTMS peripheral before the bike.
5. **Bike:** confirm a bar press moves the held watts; tune step size + feel; check coex on `/stats`.
