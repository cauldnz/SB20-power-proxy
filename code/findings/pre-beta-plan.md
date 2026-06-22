# Pre-beta plan — SB20 meter proxy → ~10 collaborator-testers

**Status: the north-star plan (updated 2026-06-23).** Refocuses [`forward-plan.md`](forward-plan.md) (still
the technical backlog) onto one goal: get the **SB20 meter proxy** onto ~10 SB20-owner testers from the
Facebook group. Read [`decisions.md`](decisions.md) for the grounding; this is *what next, in what order*.

> **Progress:** **Phase 1 (user-configurable: any meter / surviving crank, + spoof identity) ✅ BUILT**
> and **Phase 2 (ride-mode WiFi-off + coex hardening) ✅ largely done** — the device is now self-service
> over WiFi, no rebuild (PRs #80–#90, 2026-06-23). **The critical path is now the bike:** Phase 0 (prove
> the loop as a user) + the open on-bike unknowns — does the SB20 accept an *arbitrary* spoof identity,
> and does Ride-mode actually eliminate the rare coex hang (1-hour soak). Then Phase 3–5 (collaboration
> loop is desk-ready; ship pre-flashed; recruit).

## The product (decided)
**One core, two headline use cases:** a small ESP32-C3 on the bike reads a BLE power source (Cycling
Power Service `0x1818`) and re-presents it as the Stages L crank (`Stages 62144`, byte-faithful `0x2F`),
which the SB20 accepts as its own. The two reasons an SB20 owner wants that:

1. **Correct power, natively** — make the SB20 read/broadcast the meter you *trust* (e.g. an Assioma)
   instead of its native Stages crank. Hook: *the SB20 reads ~11 % high vs the Assioma (≈1.11×, = the
   367/330 dual-FTP workaround); the proxy makes it read your meter.*
2. **Crank rescue** — a **dead or dying SB20 Stages crank** kills the bike's power (the L crank is the
   master). The proxy reads the **surviving crank** (e.g. the right, `Stages 4963`) — or *any* meter —
   and rebroadcasts it as the master, **restoring full SB20 function** for the price of a tiny board
   instead of an expensive crank replacement / re-pairing hacks. A *highly motivated* tester pool ("my
   bike is broken, this fixes it").

Both run the identical proxy — only the configured *source* differs (external meter vs surviving crank).
**Long tail (post-beta):** shifter/button relay (the SB20 shifter is fully mapped → Zwift-Click-ready),
virtual shifting, the training-director / erg path, the meter-to-meter corrector for non-SB20 bikes.

**Decisions locked** (owner, 2026-06-22):
- **Value-prop = the meter/crank proxy** (the two use cases above) — *not* the training-director/erg path,
  which is long-tail.
- **Any BLE pedal-based meter** (and the surviving crank), but **pre-beta testers are collaborators** —
  prepared to gather data and trial meters we don't own. ⟹ the **data-collaboration loop is a first-class
  feature**, not an afterthought.
- **Ship pre-flashed boards** (owner flashes ~10 C3-OLED boards and mails them ready) + **iterate by OTA**
  (we push meter-support fixes to live boards — no re-shipping).

## Where we are (honest)
**Hard technical risk retired.** The SB20 accepts our spoofed crank carrying third-party power+cadence
(session 2), survives reconnect + the calibration handshake (session 3); the power topology is resolved
and FIT-confirmed (sessions 4→7); firmware is a host-tested, CI-gated dual-role BLE proxy with Stages
framing, the control-point responder, L/R balance forwarding, OLED + captive portal + HTTP/OTA. **But it's
a developer artifact:** config is hardcoded (`Config.h`), flashing needs a CLI, and the C3 can hang under
WiFi+BLE+OLED coex. And the user-flow keystone is proven only piecemeal.

## The three gaps between here and 10 bikes
1. **Hardcoded config** → today every meter needs a custom build. Need web-UI device discovery + identity → NVS.
2. **CLI flashing** → solved for pre-beta by **shipping pre-flashed** + OTA updates (testers don't flash).
3. **Reliability** → a coex hang mid-ride is a beta-killer; the proxy needs no WiFi *during* a ride.

## Phases

### Phase 0 — Prove the loop as a user *(1 bike session — the go/no-go)*
The keystone, end-to-end as a tester would: pull the real L-crank battery → pair the SB20 to our spoof →
feed a real Assioma → ride ~20 min in Zwift → confirm the SB20 **broadcasts the Assioma power reliably**
and resistance behaves, for the whole ride. Most pieces are proven; this is the clean *user-flow* proof.
**Exit:** "yes, this is a product." (If erg/resistance interplay surprises us, that's the thing to learn here.)

### Phase 1 — User-configurable: any meter *or* the surviving crank — ✅ BUILT (2026-06-23, PRs #81–#89)
The single biggest unlock, now done: a tester sets up their source **and** crank identity over WiFi with
no rebuild. Web `/setup`: scan → pick the source (meter or surviving crank, with a "crank" tag) or match
by name → **single-sided ×2** toggle → **Crank identity** (spoof name + serial) → Save → NVS → reboot.
Dashboard at `/` shows METER IN→CRANK OUT + L/R balance + the connected source name; `/setup` shows a live
"Reading … ✓" banner. Source pinning by address + the loop guard track the runtime config. Covers
[`forward-plan.md` §8 "device discovery + identity"] + "single surviving right-crank" + "meter-source
pinning". *Remaining:* the generic CPS read hardens further as testers send varied real frames.

<details><summary>original plan</summary>
The single biggest unlock — [`forward-plan.md` §8 "device discovery + pairing + identity"] + the
"single surviving right-crank proxy" + "meter-source pinning" items (this is where the two use cases meet):
- Web-UI **BLE scan → pick the source (an external meter, *or* the surviving Stages crank e.g. `4963`) →
  confirm/choose the spoof identity → persist to NVS** (replaces `METER_NAME_FILTER`/`SPOOF_NAME`).
  Pieces: a firmware BLE scan endpoint, a `/ui` source+identity picker, NVS storage.
- **Source pinning by address** so the relay is deterministic with several meters in range (the
  bike-session-2 source-bouncing bug) — and so crank-rescue targets the surviving crank on purpose.
- **Single-sided handling:** a right-only crank (or single-sided meter) usually doubles its leg for total
  — a configurable ×2 vs the dual-sided pass-through (grounded by the L/R-balance work).
- Harden the **generic CPS read** for meter diversity (already flags-aware: balance/torque/crank-rev
  offsets, sticky balance). Verify against varied real frames as testers send them.
**Exit:** a tester sets up *their* source (meter or surviving crank) from the web UI with no rebuild.
</details>

### Phase 2 — Survive an unattended ride — ⚙ largely DONE (2026-06-23, PR #90)
- **"Ride mode": WiFi off on demand** (dashboard → /wifi/off) so the ride is BLE-only — frees the radio
  from the WiFi+BLE+OLED coex (opt-in, reversible by power-cycle). PR #90.
- The coex hardening (PerfMonitor + `/stats`, task watchdog + reset-reason, OLED off the hot loop, BLE
  scan-duty) already landed earlier. **Remaining:** a **1-hour soak** confirming no hang + that Ride mode
  actually eliminates it (hardware/bench — the one unverified hypothesis). **Exit:** a board survives an
  hour untouched, repeatably.

### Phase 3 — The collaboration loop *(desk — the leverage)*
This is how "any meter" actually scales with testers as partners:
- **Tester-facing capture:** the board already logs each meter's raw CPS frame to `/log`; make "grab a
  capture of my meter + send it" a one-tap/one-file path. We ingest it (the existing `pcap/fit → SQLite`
  + golden-vector pipeline) → add/confirm support → **OTA the fix** to that tester. Grows the supported-meter
  + twin/calibration library (the long-held "beta testers grow the library" idea, now the mechanism).
- **Feedback channel** (FB thread / a short form): what worked, hangs, power mismatches, which meter.
**Exit:** a new meter goes from "tester reports it" → captured → supported → OTA'd, without a board returning.

### Phase 4 — Package + ship *(desk + owner)*
One-page onboarding (unbox → set up → **pull the crank battery / pair to our ID** → ride → FAQ); confirm
the ~10 C3-OLED boards; **flash + ship them pre-configured**; a known-good default build + an OTA channel.

### Phase 5 — Recruit + run the beta
~10 collaborator-testers from the SB20 FB group across varied meters; ship/guide; weekly structured data
loop + OTA iteration. **Two natural pools:** accuracy-seekers (have a meter they trust) and **crank-rescue**
(dead/dying crank — highly motivated, unambiguous value, vocal in the group). Recruit some of each.
**Exit:** ~10 SB20s restored/reading their own meter, a live feedback/▶OTA loop, a growing meter library.

## Cross-cutting (decide once, carry through)
- **Correction = pass-through** (the SB20 broadcasts the meter's exact watts; the 1.11× is informational,
  not applied — erg/target = meter watts by construction).
- **Pairing collision:** the real crank (`62144`) and our spoof collide on-air → the setup step is "pull
  the crank battery (or pair to a distinct spoof ID)". Make this unmissable in onboarding.
- **Framing:** experimental · clean-room MIT · **not affiliated with Stages/Favero** · use at own risk.
- **Support load:** 10 varied setups is real support; the structured-capture + OTA loop is what makes it
  tractable for one maintainer.

## Sequencing
Phase 0 gates everything (is there a product?). Phases 1–3 are parallel desk work; **Phase 1 (config UI)
and Phase 2 (ride reliability) are the critical path to a shippable board**, Phase 3 (collaboration loop)
is the leverage that makes "any meter" real. Phase 4–5 once 1+2 are solid. The known-good Assioma path can
ship to the first tester(s) while Phase 1 generalises to any meter.
