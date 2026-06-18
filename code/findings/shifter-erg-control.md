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

## Which buttons

The **3rd buttons** (`LEFT-3rd 0x0004`, `RIGHT-3rd 0x0020`) are the obvious candidates: session 3 found
they're **not bound to any app shift** (no haptic/no-op today), so claiming them for erg ±/− steals
nothing. Options, in order of preference:
- **A — the two 3rd buttons = erg down / erg up.** Simple, dedicated, no mode. *(Recommended starting point.)*
- **B — a mode toggle** (e.g. hold a 3rd button) that flips the up/down shift buttons between "virtual
  shifting" and "erg nudge". More buttons for shifting, but stateful + needs an indicator.
- **C — long-press vs short-press** on the up/down buttons. Needs press-duration from the frame stream
  (the held `01`-frames give duration) — more decoding, more ambiguity.

Step size: start ~**±5 W** (fine) with maybe a ±10 W on the "other" pair, or accelerate on rapid presses.
Tunable; decide against real feel on the bike.

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
