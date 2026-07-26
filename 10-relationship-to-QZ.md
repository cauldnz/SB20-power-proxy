# 10 — Relationship to QZ (qdomyos-zwift)

> ⛔ **SUPERSEDED — historical.** Part of the **pre-pivot brief**, written before the on-bike captures
> and before the firmware existed. Kept for provenance. For the current state read
> **[`PROJECT-MAP.md`](PROJECT-MAP.md)** (what already exists) and
> **[`code/findings/decisions.md`](code/findings/decisions.md)** (what was decided and measured).
> Where this doc disagrees with those, **they win.**

## The question

Could this project's deliverable be a contribution to QZ (cagnulein/qdomyos-zwift, https://github.com/cagnulein/qdomyos-zwift) rather than a standalone tool?

## Short answer

Build standalone first; revisit upstream contribution after Phase 2 proves the approach works. This document explains the reasoning so the option stays open without committing to it prematurely.

## What QZ does today

QZ is a man-in-the-middle bridge for indoor fitness equipment. It connects to bikes / treadmills / rowers (over BLE primarily) and re-broadcasts their data as standard BLE Cycling Power / FTMS / FE-C so that training apps (Zwift, Peloton, Strava, etc.) see them as standard fitness devices. It runs on iOS, Android, macOS, Windows, Linux. Built in C++/Qt. Active community around it (maintainer: cagnulein).

QZ's architectural claim is **between bike and apps**: it changes what apps see, not what the bike's internal control loop uses.

## What this project does

This project's architectural claim is **between cranks and bike**: it changes what the bike's internal control loop uses by impersonating the bike's own crank power meters. The bike's existing FE-C/FTMS/CPS broadcasts to external apps continue unchanged from the apps' perspective.

The owner phrases this clearly: *"controlling the SB20 by controlling the power meter data it uses for internal control."*

Different layer from QZ. Different problem.

## Could the two coexist?

Yes, easily. If a user runs both:

```
                                                  ┌────────┐
        cranks                                    │ Apps   │
        (real or our spoof)                       │ (Zwift,│
                  │                               │  TR…)  │
                  ▼                               └───▲────┘
        ┌─────────────────────┐                       │
        │ SB20 internal       │                       │
        │ computer            │ ── BLE/ANT+ ─►  ┌─────┴─────┐
        │ (runs erg loop)     │  bike broadcast │  QZ       │
        └─────────────────────┘                 │  bridge   │
                                                └───────────┘
        ┌── this project ──┐                    └── QZ ────┘
```

These are two independent man-in-the-middles at different layers, each solving a different user need.

## Could the two merge?

Maybe. The technical and project considerations:

### Technical

- **QZ on phones is BLE-only.** Phones don't have practical ANT+ TX (Android has `ANT-Android` services on some devices, iOS has nothing). Integrating ANT+-side spoofing into QZ's mobile builds isn't really feasible without external hardware.
- **For QZ-as-host to be the right home, we would need BLE-side spoofing.** The SB20 supports BLE pairing of its cranks via the Stages app's "Pair with Bluetooth" toggle. But we don't yet know whether BLE-paired cranks behave identically to ANT+-paired cranks in the bike's internal control loop. *This is the additional Phase 0 capture session described in `03-central-hypothesis-and-phase-zero.md` — Session G.*
- **C++/Qt vs Python.** QZ is C++/Qt. Once protocol behaviour is fully understood (Phase 0/1/2), porting an algorithm to C++ is mechanical engineering — but *understanding the protocol* is the expensive part, and Python is the fastest path to understanding.

### Project

- **QZ maintainer may or may not want this.** "Control the bike's internal state" is a different scope claim from "bridge data to apps". cagnulein has been receptive to feature additions in the past; that doesn't guarantee acceptance of a scope expansion. Best to ask after we have working code and a protocol writeup to show — not before.
- **Standalone has independent value.** Plenty of SB20 owners don't use QZ; they should still be able to use this. A small Pi service has a different distribution model from QZ's mobile app.
- **Iteration speed during exploration.** Phases 0 and 1 are exploratory. Forking QZ and learning its build/architecture would slow protocol discovery significantly. Get the protocol working in Python first; talk to QZ once we have results.

## Recommended position

**Now (Phase 0–2):** Standalone Python on a Pi/laptop. ANT+-side. Don't fork QZ. Don't even think about it yet.

**Phase 0 augmentation — Session G (added):** Capture BLE-paired-crank traffic. This costs little (one extra session, one extra capture script that uses bleak/pycycling instead of openant) and either opens or closes the BLE-spoof / QZ-integration door early. See `03-central-hypothesis-and-phase-zero.md` for the session description.

**Phase 3+:** If Phase 0 Session G suggests BLE-side spoofing is viable, add a `StagesBleTarget` implementation (already on the roadmap as a post-v1 item in `04-architecture.md`). At that point we have a working implementation in *both* protocols, and the option of porting either to QZ.

**Phase 4+ (productisation):** Open a discussion with cagnulein. Show working code, protocol writeup, and a concrete proposal for how an "SB20 internal-control mode" would slot into QZ's architecture. Ask whether it would be a welcome contribution or whether it's out of scope for QZ. Decide based on the answer.

Either outcome is fine. This project keeps having value as a standalone tool regardless of QZ's reception.

## What to keep in mind in the meantime

A few small architectural disciplines that keep the QZ-port option cheap to exercise later, without slowing us down today:

- **The source/target seam in `04-architecture.md` is the contract.** A `StagesBleTarget` can be added later without disrupting `StagesAntTarget`. Don't introduce assumptions in the abstract base classes that wouldn't survive a C++ port (e.g. don't bake `asyncio.Future` into the public API).
- **Document protocol bytes, not Python idioms.** When Phase 0 captures resolve into a "what we need to spoof" specification, write it as a protocol doc — byte layouts, page sequences, calibration response shape — not as Python code. The doc ports; the code may not.
- **Keep configuration declarative.** TOML config that lists "manufacturer ID = X, channel period = Y, pages emitted = [list]" maps cleanly to a C++ port. Imperative setup logic doesn't.

These are all things we'd want to do anyway for clean Python; flagging them as also serving the future-port option just makes the discipline easier to maintain.

## Concrete next-step decision points

The choice "QZ contribution or standalone forever" doesn't need to be made now. But these are the moments where it does become a real decision:

1. **End of Phase 0** — Session G results tell us whether BLE-side spoofing is even viable. If BLE-paired cranks turn out to use a custom Stages BLE service we don't understand, the QZ path is dead until we reverse-engineer it. If they use standard CPS, QZ is plausible.
2. **End of Phase 2** — we have working ANT+ spoofing. At this point the protocol is understood. Decision: keep building productisation features for the standalone Python tool, or pause and explore the QZ contribution path?
3. **End of Phase 3** — the standalone tool is a tagged release. Decision: invest in the QZ port, or move on to Phase 4 (multi-input source support, etc.)?

Don't deliberate this earlier than the listed moments; you don't have the information you'd need.
