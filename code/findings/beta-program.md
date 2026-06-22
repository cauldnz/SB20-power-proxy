# Beta program — running the pre-beta with ~10 SB20 testers

**Status: the operating doc for the pre-beta (2026-06-22).** The "how we run it" companion to
[`pre-beta-plan.md`](pre-beta-plan.md) (the "what we're building"). Tester-facing material lives in
[`/beta`](../../beta/) (onboarding, ride protocol).

> **The one principle that shapes everything: a tester's time on the bike is the scarcest, most
> precious resource we have. We never send anyone to pedal until we have verified, off the bike, that
> everything is ready — and we do the analysis, not them.** Every decision below serves that.

## Who we want (~10, two pools)
Both pools are SB20 owners active in the Facebook group, willing to be **collaborators** (gather data,
trial meters we don't own), comfortable joining a WiFi network and following a short guide.
- **Accuracy-seekers** — own a BLE pedal meter (Assioma, Garmin Rally, Wahoo Powrlink, Favero, 4iiii,
  etc.) they trust, want the SB20 to read *that* (the ~11%-high finding is the hook).
- **Crank-rescue** — a dead/dying Stages crank; highly motivated, unambiguous value ("my bike works
  again"). Often the most engaged testers.

Aim for a spread of **meter brands** (that's how we grow support) and at least 2–3 crank-rescue cases.

## Screening (a short form before we ship a board)
1. SB20 confirmed? Firmware version (Stages app → about)?
2. Which use case — your own meter, or a failing crank?
3. If a meter: exact brand + model, and does it broadcast **Bluetooth** (not just ANT+)? (We read BLE.)
4. If crank-rescue: which crank is dead (L/R)? Does the surviving one still broadcast?
5. Training app(s) you use (Zwift, etc.).
6. Willing to: join a setup WiFi, pull a crank battery if needed, send us a log file after a ride,
   take an OTA update? (All yes = a good fit.)
7. Comfortable that this is **experimental, unaffiliated with Stages/Favero, use at own risk**?

## What ships
A **pre-flashed C3-OLED board**, pre-configured to their meter where we can (from the screening), plus
the [onboarding one-pager](../../beta/ONBOARDING.md) and the [ride protocol](../../beta/TESTER-RIDE-PROTOCOL.md).
Boards iterate by **OTA** — we never need one back to add meter support or fix a bug.

## The tester loop (capture → support → OTA)
```
ship pre-flashed ─▶ tester sets up (off bike) ─▶ pre-flight verify (dashboard shows their meter)
        ▲                                                   │
        │                                                   ▼
   OTA the fix ◀── we add/confirm support ◀── tester sends a /log capture ◀── short scripted ride
        └─────────────── (no board returns; we analyse offline) ───────────────┘
```
- **Works first try** (common for mainstream meters): tester confirms on the dashboard, does one short
  ride, done. Minimal of their time.
- **Meter not recognised / power looks wrong**: tester grabs the board's `/log` (one capture of their
  meter's CPS frames) and sends it. **We** decode it, add support (golden-vector test from their real
  bytes — the real-data-first discipline), and **OTA** the update. Their next ride confirms.
- Each new meter that lands this way grows the supported-meter + twin/calibration library — the whole
  point of testers-as-partners.

## Respecting testers' time (non-negotiables)
- **Verify before the bike.** All setup, pairing, and "is the board seeing my meter" happen **off the
  bike** via the dashboard. The [pre-flight checklist](../../beta/TESTER-RIDE-PROTOCOL.md) is a hard gate
  — green dashboard before they mount.
- **Async-first.** The board logs; the tester sends a file *after*. No live screen-share or real-time
  debugging required (offer it, never require it). We analyse offline and come back with a fix, not
  questions-during-the-ride.
- **Scripted, batched, short.** Each on-bike ask is a tight script (e.g. "5 min easy, watch the two
  numbers match, stop"). We batch our questions into one message, never drip-feed. A session is minutes
  of bike time, not an evening.
- **We carry the load.** We do the decoding, the fitting, the OTA. The tester's job is: set up, ride
  once, send a file. That's it.
- **No dead ends.** If a ride can't be made ready, we say "don't ride yet" rather than have them
  troubleshoot in the saddle.

## Feedback channels
- A short structured **form** per ride (what worked / power match / any hang / which meter) — 60 seconds.
- The **`/log` capture** for any meter/power issue (the real signal).
- A **group thread / DM** for back-and-forth, but the form + log are the system of record.

## Data & consent
We collect BLE power-meter frames + ride power/cadence (no personal/location data needed). State it
plainly in onboarding; testers opt in. Captures we use to add support get committed as canonical
fixtures (anonymised filename). MIT / clean-room; nothing copied from Stages/Favero.

## Cadence & exit
Weekly: triage forms + logs → batch a fix/OTA → confirm. **Exit:** ~10 SB20s reading their own meter or
restored from a dead crank; a live feedback/OTA loop; a growing meter library; a short list of the next
features testers actually asked for (likely shifter/buttons).

## Risks
- **Support load** of varied setups — mitigated by async + batching + the OTA loop (one fix → many).
- **A bad first ride** sours a tester — mitigated by the verify-before-bike gate.
- **Meter we can't support** (ANT+-only, oddball encoding) — set expectations in screening (BLE only);
  capture it anyway for the library.
- **Safety/liability** — experimental framing, the crank-battery step called out clearly, no medical/
  performance claims.
