# ROADMAP — the one backlog (Now / Next / Later / Parked)

**Status: LIVING · owner-prioritised · updated 2026-09-24.** This file is the source of truth for
*what next*. `code/findings/forward-plan.md` §9, `code/findings/pre-beta-plan.md` (as "the north
star") and the `sessions/README.md` banner defer to it. Only **Now** items are mirrored as GitHub
issues (labels `ready-for-agent` / `ready-for-human`); Next, Later and Parked live only here. When
an item ships, delete it here (git keeps the history), move a one-liner to "Recently landed", and
log the durable outcome in [`decisions.md`](code/findings/decisions.md). The audit behind this file
is the [2026-09-23 state-of-the-repo review](docs/reviews/2026-09-23-state-of-the-repo.md); what
already exists is [`PROJECT-MAP.md`](PROJECT-MAP.md).

## North star (owner, 2026-09-23)

**A training stack for two riders on two SB20s.** Done = each rider gets on their own SB20, powers
their own Guition head unit, the bike reads *their* pedals through our crank spoof and runs a
structured erg workout from the head unit; same room, same time, no Stages app, no phone, no agent
in the loop; repeatably across a four-week block. The qz/Peloton path still works on either bike
(never two erg controllers on one bike at once). **Round-zero beta = the two of us**, using the
tester kit on ourselves. The pre-beta *capabilities* stay; the tester *programme* is parked.

Confirmed configuration: bike 2 has working Stages cranks and its rider has her own Assiomas
(accuracy proxy on both bikes: the app pairs L = our spoof, R = that bike's real right crank, the
session-8/9 recipe); a Guition JC3248W535 on each bike (the second to be ordered; the C3-OLED
`.165` stays the proven spoof board for bike 1's first ride); the on-device workout engine is the
primary erg driver and qz/Peloton is kept; rides happen together and apart; pedals are owner
`ASSIOMA 17039L / 22428R`, daughter `ASSIOMA 29064L / 26807R`.

## NOW (ordered; each mirrored as an issue — the reference doc, #290, landed 2026-09-23 and moved to "Recently landed")

| # | id | goal | gate | issue / canonical doc | why it is not already done |
|---|---|---|---|---|---|
| 1 | **PELOTON** | ⭐ **Owner wants this ASAP (2026-09-24).** Ride a Peloton **Power Zone** class and have our stack drive erg from the class's own targets, the way qz does: detect the live ride from Peloton's servers, read its power-zone target data, steer the trainer. **Most of this exists** — the on-device workout engine, `/workout`, the FTMS erg drive and `Segment.zone`/`pct_ftp` + `RiderProfile` are all built, so Peloton is a new workout **source**, not a new erg engine: a `from_peloton()` beside `from_zwo`/`from_fit`. **Phase 0 (capture a real PZ ride's API responses) before any code.** Confirmed 2026-09-24: the ride object carries the **complete target timeline up front** (`target_metrics_data.target_metrics`, per-entry `offsets`) and qz plays it back locally — it polls only workout *status*. So this is a downloaded timeline, which our engine already runs, and Phase 2 on-device needs no streaming or continuous connectivity. No class picking — the head unit detects the class the rider started, as qz does (owner, 2026-09-24). Phase 1 desk-side adapter → `POST /workout`; Phase 2 on-device. Blocked on rider FTP (Next item 3) for two riders; needs a qz-interlock decision (never two erg controllers on one bike) | owner (Phase 0: a real PZ ride with the browser session cookie; OAuth needs a client id we lack) → desk, agent | #342 · [`code/findings/peloton-integration.md`](code/findings/peloton-integration.md) (research + the Phase 0 recipe, #350) | promoted from Next 2026-09-24; the scoping it asked for is in #342 |
| 2 | **UIMAP** | The UI feature map ([`docs/ui-feature-map.md`](docs/ui-feature-map.md)): every feature on every surface, the owner's device-vs-web placement per row, the test map; then the **bench pass** ([`sessions/bench-ui-pass.md`](sessions/bench-ui-pass.md)) that burns down every never-seen-on-a-panel cell before session 14, filing the gaps (#346 LCD defects, #347 SPA-over-HTTP phantom routes). `/app` is the one web UI; the legacy pages retire once every web-placed row is green | desk | #345 · `docs/ui-feature-map.md` | the map (#349) and every placement (§2, 2026-09-24) are done; the bench pass is not yet run — it needs the boards on the bench with the camera, serial and a phone |
| 3 | **WEBRIDE** | The web app ride-ready (owner, 2026-09-24: all four are must-haves before riding with a phone on the bars): target/segment/time-left on the ride hero, a Wake Lock + HTTP reconnect, the power chart, a full-screen ride mode (map F14/F16/F18/F45), plus the #347 fields and routes the ride view needs | desk, agent | #351 · [`docs/ui-feature-map.md`](docs/ui-feature-map.md) §2i | `/app` has none of the four and has never been used on a ride |
| 4 | **S14-G0** | Session-14 desk pre-stage: both head units re-flashed from `main` with unique identities, the bike-2 inventory (SB20 #2 name/address, crank ids, battery), expected-topology cards, the capture rig proven live, restore values for both bikes | desk → hardware | #331 · `sessions/session-14-two-bike-first-rides.md` | the ride board was last flashed 07-26; nothing in the repo describes the second SB20 |
| 5 | **GUITION2** | ⚠️ **Confirm the model before ordering** — "JC3248W535" is off the owner's AliExpress listing, *not* the PCB, and the variant suffix is unknown (no ESP32 board self-reports a model); read the silkscreen first, or you may order a different variant. Then: order the second board (owner); bench bring-up per `guition-board.md` with the bench camera; own identity; a 30-minute `perf_soak.py` on a Guition before it rides; close the open touch/pin checks | owner + desk | #332 · `code/findings/guition-board.md` ("Provenance") · decisions 2026-09-23 | the Guition has never ridden; one exists, two are needed |
| 6 | **ERG** | The standing erg go/no-go and the 53-minute workout soak on bike 1. One item for what six documents track separately: session 12 G2/G3, `CAPTURE-sb20-erg-recovery` option A, ftms-protocol "still the gate", forward-plan §14 phase 5 and §13 on-bike drive, dyno plan D4.1, pre-beta Phase 0 and its 1-hour soak | bike | #331 (G2/G3) | session 12 §9 is empty; the workout engine has never driven a real SB20 |
| 7 | **BIKE2** | Bike 2 pairs to its own head unit and pedals; then both bikes at once | bike | #331 (G4/G5) | no document mentions the second SB20 |
| 8 | **#288** | The single-variable A/B (Garmin trainer-protocol pairing vs not) on a cold start, as session 14 G1b; diff the two banked pcaps at the desk first | bike (+ a desk sub-task) | #288 | "cause NOT established"; a stack that avoids the Stages app needs the answer |
| 9 | **ROUND0** | Round-zero beta on ourselves: onboarding one-pager per bike, the ride protocol, `/report` → `parse_diag` after each ride, the feedback form, OTA to a fleet of two; lessons into `USERS-PLAYBOOK.md` | owner | #333 · `beta/`, `code/findings/beta-program.md` | `USERS-PLAYBOOK.md`: "we haven't engaged a single user yet" |

## NEXT (roughly ordered)

1. **Docs reconciliation leftovers** (agent): `qz-upstream-contribution.md` §3/§7; the
   `advanced-board-s3-touch.md` split (board doc vs tier plan); fold `docs/bike-session-workflow.md`
   into the playbook or keep it as the short companion; the `BOARDS.md` / `BENCH-FLASH.md` items
   handed to the hardware session; the un-logged 2026-09-16 Guition bring-up in `decisions.md`.
2. **Affinity by address** (agent): `trainerAddress` and `shifterAddress` in `RuntimeConfig`;
   `FtmsErgClient` and `BleShifterClient` pin by address; the nRF `PeerRole` equivalent. The fleet
   table's naming discipline covers session 14; this closes the code gap.
3. **Rider profile per head unit** (agent): `ftp_w` in `RuntimeConfig`; presets assume 250 W today;
   `import_workout.py --ftp` per rider until then.
4. **PICKER** (agent): both pickers list every BLE device and filter to power meters and trainers
   (heart rate later); today the device list is pre-filtered to CPS/FTMS and the web cannot pin by
   address or rescan (#347) — map §2i.
5. **WEBPARITY → retire the legacy pages** (agent): the web-placed rows only the legacy pages serve
   today (forget WiFi, WiFi-off, reset, skip, change workout, import, the calibration wizard over HTTP,
   devmode + virtual press in Settings, the tester report, version, reboot — map §2i); then
   `WebApp.h` / `ConfigPage.h` / `CalibrationPage.h` go in one PR.
6. **DEVROWS** (agent, after the bench pass): the device rows the placements add — Forget WiFi,
   WiFi-off, Reboot and Version on More, bias ± on the Workout console, FTP shown — and the removals
   (the dead Calibrate button; Compare shrinks to the verdict once real data exists) — map §2i.
7. **Larger Guition head unit** (owner wants to try one): the 4.3-inch JC4827W543 is QSPI like the
   3.5-inch, so it is the closest fit to the existing `GuitionDisplay` seam; the 5-inch JC8048W550
   (800×480) is reportedly an RGB-parallel panel, a bigger bring-up. Verify the panel controller
   before ordering; budget a display-seam bring-up plus a CI env, like the JC3248W535 port.
8. **Two-rider motivation features** (owner wish): a shared dashboard or pace-match between the two
   head units; needs the fleet-status instrument below first.
9. **#291 decision → #247 G2** and the OBC seed-on-connect audit (decisions 07-26): buttons only;
   the qz/Peloton erg path needs none of it.
10. **#324** push the qz-fork branch and open the upstream PR (owner's public action).
11. **V1–V5 SB20 facts and the hold-to-repeat ride** (qz path; the table lives in session 14's annex).
12. **forward-plan §12** single-crank pairing test (session 14 stretch).
13. **R1e.2 CYD screen sweep** via the bench camera.
14. **#321** as a PC-side MCP adapter over the head unit's HTTP API (discover boards by mDNS).
15. **`fleet_status.py`**: poll N boards' `/status` and `/stats` (`perf_soak.py` is single-board).
16. **MeterCompare with two real Assioma sets** (session 14 stretch S2; today every number in that
    feature is fabricated).
17. **#334** IP + RSSI on the LCD Settings/More tab (re-filed from PR #255).

## LATER

forward-plan §11 crank-length bridge (three captures first) · §8 L/R balance value grounding ·
#305 / R5b (needs a Garmin) · #289 tap vs hold · #292 MyWhoosh validation · #287 voice narration ·
session 5 corrector ride (opportunistic, track bike) · forward-plan §12 battery-out variant (needs an
nRF sniff) · §13 remainder (persisted workout library, true pause/resume) · §6 ESP Web Tools flasher
· nRF: R3 spoof vs a real SB20, ANT+ spoof on air, session-13 N0–N2 + R9, P4 slave channel, R1b
part 2, R1d.3, gnu++17, `BOARD_HAS_IMU` gating, cadence −1 in the Status notify · R2c Monkey-C
golden lock · R3b · R4a/b · U1 parity test (do it or close it) · session-13 R1 double-fire exclusion ·
the doc debt recorded in the 2026-09-23 review (dead modules, unguarded generated files, stale code
references, version strings) · a heart-rate strap as a source, under an HR filter in the pickers and
on the ride view (map F46; owner: "at some point").

## PARKED (un-park condition in brackets)

- **Pre-beta programme** (pre-beta-plan Phases 3–5, `beta-program.md`, the recruiting kit)
  [round zero ran four weeks on both bikes AND the owner re-approves; capabilities stay live].
- **Dyno → Autotuner** (forward-plan §16, three findings docs) [explicit owner go after ERG passes
  and four weeks of real erg data].
- **Track launch control** (forward-plan §15, four findings docs; R1c rides with it) [explicit owner
  go plus the LC0 procurement].
- **The dreaming lists** (`docs/dreaming-20-ideas.md`, `docs/sol-dreaming-17Jul26.md`) [an idea
  graduates only by appearing in Next here].
- **Garmin Connect IQ app on-device** (`firmware-nrf/ciq/`, R2c) [a Garmin head unit enters the
  training stack and the SDK is installed].
- **nRF ANT+ slave channel and third-party shifters (P5, #249)** [an ANT-only meter or a Di2/AXS
  transmitter enters the rig].
- **OTA signed-pull P3/P4** (`ota-update-plan.md`) [the backend and the signing key exist].
- **The Python/ANT+ Pi proxy path, `zwift-controls-research.md`, forward-plan §3 Lane 2**
  [superseded; history only].
- **#131 / #144 cross-repo notices** [closed as acknowledged; reopen on a concrete request].

## Recently landed (September 2026)

Guition JC3248W535 port and end-to-end validation on a simulated meter (#318, #320, #322) · the C3's
30-minute ride-ready soak: zero reboots, heap flat, dual-role BLE costs nothing, the only stalls are
the synchronous web server (#325) · `flash_s3.py` no longer wipes NVS (#322) · `flash.ps1`'s OTA
RSSI pre-flight revived; C3 push-OTA reliable at −72 dBm (#326) · `sb20proxy-guition.local` · the
bench camera (`BOARDS.md`) · the `fake_meter` stale-instance guard · the 2026-09-23 review synthesis
(#327) and state-of-the-repo review (#329) · `ROADMAP.md` (#335) · the ledger and captures guards (#336) ·
the PROJECT-MAP refresh (#337) · the status-line pass (#338) · the system reference doc, issue #290
(`docs/system-reference.md`) · branch and worktree hygiene · CI compiles the Guition and Waveshare S3 ride
builds, with pioarduino in its own core dir and cache so the two Arduino platforms stop overwriting each
other's packages (#348, closes #323) · fleet identity: a board with no stored name advertises its
own `Stages 9NNNN` derived from its MAC (the nRF from its BLE address), `/status` shows where the name
came from plus the pinned pedals and the trainer, and the web app no longer force-posts `Stages 62144`
(#354, closes #330; the hardware checks ride in the bench pass and session 14 G0).

## Maintenance rule

1. Every session close-out (`sessions/PLAYBOOK.md` §3) updates Now/Next here in the same PR as
   the ledger row.
2. The PR that lands a Now item deletes it here, adds a one-liner to "Recently landed" and closes
   its issue.
3. Next → Now promotions are the owner's call; agents may add to Next/Later with a source citation.
4. `forward-plan.md` §9, `pre-beta-plan.md`'s header, the `sessions/README.md` banner and
   `BIKE-SESSION-READY.md` carry a one-line "see ROADMAP.md" pointer and are never edited for
   sequencing again. `code/tests/test_doc_links.py` guards the links here.
