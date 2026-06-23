# The users playbook — how we work with the people who use this

The third companion to [`DEV-PLAYBOOK.md`](DEV-PLAYBOOK.md) (how we build) and
[`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md) (how we run an on-bike session). This one is **how we
treat the humans on the other end** — testers, beta users, eventually customers. We haven't engaged a
single user yet; this is the standard we hold *from the first message*, so the first cohort is run as
well as the tenth. It's the principles layer; the live instance it governs is
[`code/findings/beta-program.md`](code/findings/beta-program.md) (the current pre-beta) plus the
tester-facing material in [`beta/`](beta/).

**Keep it living.** After every cohort / support thread / launch, fold what worked (and what annoyed a
user) back in here — *before* moving on. A lesson a user paid for in patience that we don't write down,
we make the next user pay for again.

Four rules above the detail:

1. **A user's time and goodwill are the scarcest things we have — never waste either.**
2. **They're collaborators, not test subjects — we carry the load; they get real value.**
3. **Async-first, scripted, batched — never require a live debugging session.**
4. **Promise only what you can keep; report status honestly — including bad news, early.**

---

## 1 · Who this product is for (and who it isn't)

The product is **power-meter pedals becoming the crank replacement** — so the one non-negotiable is that
the user owns a **Bluetooth power-meter pedal set**. Two real jobs, two different people; pitch and screen
to the *job*, not "anyone with an SB20":
- **Accuracy-seekers with a *working* crank** *(recruit these first)* — own power pedals they trust
  (Assioma, Rally, Powrlink, SRM EXAKT…) and are bothered the SB20 reads ~11% off them. Hook: *the SB20
  reads your pedals, natively, in erg.* The working crank is a known-good reference to validate against —
  the right first cohort.
- **Crank-rescue** *(take 2–3 in round one — the clearest win)* — a dead/dying Stages crank replaced by
  their pedals. Hook: *your bike works again* — the most motivated pool, and they prove the headline
  directly. Take a couple this round (keep *most* of the cohort working-crank, for the validation
  reference); reassure everyone else ("you're who this is for — hold while we prove it, don't bin the bike").
- **Tease the roadmap, don't lead with it:** erg-target control from the SB20 shifter buttons, Zwift
  integration. They build excitement and shape what's next — but pedals first; promise only what's shipped.

Screen *out* honestly too: **no power pedals** (crank-arm / spider meters are out of scope for this
product), **ANT+-only** pedals (we read BLE), people who want a polished consumer product today, anyone
uncomfortable that this is experimental and unaffiliated with Stages/Favero. A bad-fit user is a
frustrated user and a support sink — "not yet, here's why" protects both sides.

## 2 · Recruit & set expectations at the door

- **Honest, specific, no hype.** Lead with the real problem and the real state ("experimental, DIY, I
  built this, here's what works and what doesn't"). Never imply affiliation with Stages/Favero/Zwift;
  never make medical, safety, or performance-gain claims. Under-promise.
- **Recruit collaborators, not customers.** The pitch is "help me prove this on your setup, and you get
  it working on your bike" — a fair trade of their data/time for the fix. Make the experimental framing
  and the at-own-risk reality unmissable *before* they opt in, not in fine print.
- **Screen before you ship.** A short form (meter brand+model, BLE-or-ANT+, use case, app, willingness to
  do the async loop) keeps a board from going to a setup we can't support. The selection is a *fit*
  judgement, not first-come. See [`beta/`](beta/) recruiting + screening material.
- **Aim for a spread of meter brands** — each new brand that works grows the supported-meter library;
  that's the whole point of a data-collaboration beta.

## 3 · Onboard so the first 15 minutes work

- **Verify before the bike — always.** Setup, pairing, "is the board seeing my meter" all happen *off*
  the bike via the dashboard; a green pre-flight is a hard gate before anyone pedals (the on-bike
  discipline of `sessions/PLAYBOOK.md`, applied to the user). Never send a user to ride something we
  haven't verified is ready.
- **Ship it working.** Pre-flashed, pre-configured to their meter where the screening let us; an
  OTA-iterated board never has to come back. The onboarding one-pager + ride protocol do the rest.
- **No dead ends.** If a board can't be made ready, say "don't ride yet" rather than letting them
  troubleshoot in the saddle. A first bad ride sours a tester; the verify-gate exists to prevent it.

## 4 · The collaboration loop (capture → support → OTA)

The engine of a data-collaboration beta — and the contract that makes it respectful:
```
ship pre-flashed → user sets up off-bike → short scripted ride → user sends a file
        ▲                                                              │
        │                                                              ▼
   OTA the fix ◀──────── WE do the analysis (offline) ◀────────────────┘
```
- **We carry the load.** The user's whole job is: set up, ride once, send a file. We do the decoding,
  the fitting, the OTA. No live screen-share, no real-time debugging — ever required (offer it, never
  demand it). Tools that make this real: the device `/diag` report → `parse_diag.py` /`route_smoke.py`.
- **Each artifact a user sends becomes canonical** — a committed fixture, a new supported meter. Their
  patience buys the library; honour it by using what they send.

## 5 · Communicate like you respect their attention

- **Scripted, batched, short.** Each ask is a tight script ("5 min easy, watch the two numbers match,
  stop"). Batch questions into one message; never drip-feed. A session is minutes of their time.
- **Deliver bad news early and plainly** — a bug, a delay, "your meter needs work we haven't done yet."
  The dev playbook's "built ≠ validated" applies to user comms: don't say "fixed" until it's confirmed
  on *their* setup. A user told the honest state trusts the next thing you say.
- **One system of record.** A short structured form per ride + the `/diag`/`/log` file are the signal; a
  group thread / DM is for back-and-forth, not the record. Reply on the user's timezone, not mid-ride.
- **Tone:** a fellow rider who built a thing and wants it to work for them too — not a vendor, not
  support-ticket boilerplate, not breathless marketing.

## 6 · Consent, data & the legal frame

- **Plain-language consent, opt-in.** State exactly what we collect (BLE power-meter frames, ride
  power/cadence — no personal/location data needed) and why, in onboarding, before they join.
- **Anonymise what becomes canonical.** Captures we commit as fixtures get a neutral filename; no name,
  no location.
- **Experimental, unaffiliated, at-own-risk — said clearly, repeatedly.** Call out the crank-battery
  step and the "this isn't a Stages/Favero product" reality up front. MIT / clean-room throughout —
  nothing copied from anyone's app.

## 7 · Cadence & exit

Weekly: triage forms + logs → batch a fix/OTA → confirm. **Exit (pre-beta):** ~10 SB20s reading their
own meter or restored from a dead crank; a live feedback/OTA loop; a growing meter library; and a short
list of the next features users *actually asked for*. Then decide what "beta → product" needs.

## 8 · The compounding loop (what makes this worth writing down)

Every cohort teaches something about the humans, not just the firmware — a screening question that
should've been asked, a pitch line that landed, a support reply that defused a frustrated tester. Fold
it back here. `CLAUDE.md` points future work at this doc and expects it to grow, the same ratchet as the
dev playbook.
