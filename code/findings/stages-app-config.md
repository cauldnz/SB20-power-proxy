# Stages Cycling app — ride modes, Profiles & button config (owner recon, 2026-06-19)

**Status:** capture-before-code reference, from the owner exploring the app + bike (there are **no
official docs**). This is the config surface our erg / shifter-control features sit on top of. Open items
are flagged for the **session-4** capture.

## Ride modes — three, and they decide where our feature lives

| Mode | Gears | Power / grade set by | Our use |
|---|---|---|---|
| **External** | **work** | an **external controller** (over FTMS) | ⭐ **where our erg control lives** — the bike *expects* to be driven from outside (this is the Zwift mode) |
| **Grade** | work | the app (a `+/−` grade control) | sim-style; app owns grade |
| **Power (Erg)** | **OFF** | the app (a `+/−` pair or an FTP-zone slider) | the **app** owns the target — we can't inject here |

**The key realisation:** our "shifter nudges erg watts" feature does **not** live in the app's *Power/Erg*
mode (the app owns the target there). It lives in **External** mode — the SB20's own "defer to an outside
controller" mode. So the architecture is: **bike in External mode → our ESP is the external FTMS
controller** (holds a target, writes Set Target Power), and the rider nudges the target from the bars.
External mode *wanting* an external controller is a strong signal our FTMS writes will be accepted
(confirm in session-4 §C). In External mode gears still work — which is fine, because we free the buttons
we want via the **"external" button assignment** (below), not by fighting the app.

## Profiles

Named, user-created (e.g. "Power Erg Mode"), each holding a bundle of settings:

- **Gradient scale factor** (default **100%**) and **Equipment weight** (default **8 kg**) — the
  **sim/Grade-mode physics** (together they set the power needed to "lift the equipment up the scaled
  hill" at a given grade). Not relevant to Erg/External target-power, but useful context.
- **Vibrate / audio feedback on gear shift** (on/off toggle) — **confirms session 3**: the shift "click"
  is the **phone/app**, not the shifter pods (the pods have no haptics).
- **Gear Setup** — two styles: **Dream Drive** (just choose a number of continuously-adjustable gears) or
  **Custom** (pick a **chainring + cassette** to mimic a road bike → a real **2×** drivetrain, with front
  and rear shifts).
- **Shift button mode** — **Shimano / Campagnolo / Custom** (the shift-logic flavour).

## Buttons — five per side (ten total), three config slots per side

- **5 buttons per side / 10 total.** Buttons **4 & 5 are hidden under the bar tape**; session 3 only
  pressed 1/2/3. Session-3 BLE map (char `0c46be60`, one-hot): **LEFT 1/2/3 = `0x01`/`0x02`/`0x04`**,
  **RIGHT 1/2/3 = `0x08`/`0x10`/`0x20`**.
- **The app exposes three config slots per side: `{1,4}`, `{2,5}`, `{3}`** — it **ties 1≡4 and 2≡5** (you
  can't configure them separately in the app).
- **Each slot is assignable to:** *front gear easier*, *front gear harder*, *rear gear easier*, *rear gear
  harder*, or **external**.

### The "external" assignment — the keystone mechanism

Setting a slot to **external** makes the **app ignore those buttons** — but the bike **still broadcasts
them on `0c46be60`**, so **our ESP reads and repurposes them**. This is the app's *own* supported way to
hand buttons to an outside controller — no hack, no "disable shifting" workaround. (It's almost certainly
why **button 3 was inert in the app but still emitting** in session 3 — it was already set "external".)
So the rider creates a **"proxy" Profile** that marks the buttons we want as external, and we use whatever
is freed.

## Open — for session 4 (capture-before-code)

1. **Are 1≡4 / 2≡5 separable over BLE?** The app *ties* them in config, but if buttons 1 and 4 (and 2 and
   5) emit **different bits** on `0c46be60`, the ESP can still tell all five apart → up to **10** usable
   signals; if the same bit, 1≡4/2≡5 are indistinguishable to us (we're back to the mapped 6). → session-4
   §B presses 1-then-4 and 2-then-5 each side. *(Buttons 4/5 bits are unmapped.)*
2. **Does the SB20 accept a third-party FTMS Set Target Power (and erg off it) in External mode?** The
   go/no-go for the erg feature → session-4 §C (`capture_ftms.py --erg`). External mode *expecting*
   external control makes this likely, but it's unconfirmed.
3. **Shift behaviour in erg/External** — does pressing a (non-external) shift button do anything while an
   external controller holds the power? (Informs whether the rider must set buttons "external" or can
   leave shifting on.) → session-4 §C bonus.
4. **Hold = repeated shift-commits?** Unresolved (session 3 couldn't distinguish a hold from N taps) →
   session-4 §B deliberate hold-vs-taps.
5. **Custom 2× and the `03` frame** — with a Custom front+rear profile, do the `03`-commit frame's two
   gear fields differ (front vs rear)? (Session 3 saw them equal, but that was a Dream-Drive-style setup.)

## Why this matters — the SB20 is built to be driven externally

External mode + per-button "external" assignment are the SB20's **designed-in hooks for an outside
brain.** That's exactly what this project is becoming: the ESP relays the power source (Assioma), can hold
the **erg target** (FTMS Set Target Power), and reads the **freed buttons** — with the SB20 as the trainer
+ display. It de-risks the erg feature (External mode wants us) and is the incremental path toward the
"alternative to the Stages app" idea ([`shifter-erg-control.md`](shifter-erg-control.md) §Longer-term).
