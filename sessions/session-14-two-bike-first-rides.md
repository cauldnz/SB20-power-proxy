# Session 14 — the two-bike training stack: first rides

**Status: PLANNED (2026-09-23)** · tracked in [`sessions/README.md`](README.md) and as
[issue #331](https://github.com/cauldnz/SB20-power-proxy/issues/331). Run via [`PLAYBOOK.md`](PLAYBOOK.md).
**Bikes:** SB20 #1 (`Stages Bike 0105`, `E4:AA:5A:D6:0E:D4`) and **SB20 #2 (name/address to be
inventoried in G0)**. **Riders:** the owner (pedals `ASSIOMA 17039L / 22428R`) and the owner's daughter
(pedals `ASSIOMA 29064L / 26807R`). **Boards:** the C3-OLED ride board `sb20proxy.local` /
`192.168.1.165` (bike 1's proven spoof board), the Guition `sb20proxy-guition.local` (bike 2's head
unit, or bike 1's once a second Guition exists). **Branch:** `main`. **Budget:** ~90 min of rider time
over one or two evenings; every gate has an explicit pass/fail.

> **Design principle (inherited from session 12):** the erg gate *is* the workout. If the SB20 takes our
> target power, the rider rides a real structured session driven by our own stack, and that ride
> simultaneously soaks the spoof, the workout engine, coex/heap and the captures. The two-bike goal
> (`ROADMAP.md` north star) adds two things session 12 never had: **a second bike** and **identity
> discipline** so two spoofs, two real crank sets and two pedal sets can share a room.

**The rider's job is the hardware and the pedals. The agent runs every tool** (PLAYBOOK "the
human-in-the-loop contract"). Nothing below is attempted until G0 is green at the desk.

## 1. Why now

- The standing erg go/no-go (does the SB20 move resistance on *our* FTMS Set Target Power?) has been
  "READY, next" since 2026-07-25 and is tracked under six different ids; no ride has run since session
  13 on 2026-07-26. It is the highest-information unproven thing in the repo.
- The owner bought a second SB20 and restated the goal on 2026-09-23: a training stack for two riders,
  each on their own head unit and pedals, together and apart, with the two of them as beta round zero.
- Session 12's plan was written for one rider, one bike and one board; its desk pre-stage is stale
  (firmware changed on 07-26/27 and 09-23; see Annex B). This session reuses its gates and its pairing
  recipe (`session-12-erg-workout-validation.md` §5) rather than re-deriving them.

## 2. Desk pre-stage (G0) — before the rider is involved

- [ ] **Fleet identity.** Every board that will be powered in the room has a *unique* spoof identity
      and a row in the fleet table ([`docs/system-reference.md`](../docs/system-reference.md) §2; the MAC-derived default is #330): board →
      identity → bike → pinned pedal address → trainer full name. No board may be on the default
      `Stages 62144` (bike 1's real left crank) and no two boards may share an id (on 2026-09-23 the
      Guition and the C3 ride board were both `Stages 62145`).
- [ ] **Re-flash both head units from current `main`** (`firmware/flash.ps1`, OTA; the ride guard
      refuses bench/mock envs). Confirm the build SHA on `/status` for each.
- [ ] **Bike-2 inventory.** BLE scan for its `Stages Bike XXXX` name and address; the ids of its real
      L and R cranks (`Stages NNNNN` adverts, `code/scripts/16_scan_ant.py` for the ANT+ ids); crank
      battery levels (fresh CR2032s ready). Record them in this doc's §4 and in the fleet table.
- [ ] **Pinned pedals.** `/setup` on each board pins its rider's pedals by address (`meterAddress`),
      never by the `ASSIOMA` name substring, and names the trainer by its full `Stages Bike XXXX` name.
- [ ] **Capture rig proven live** on the bike laptop: `tools\doctor.ps1` green; the nRF sniffer
      (`sniff_ble.py`) and the ANT+ stick each seen capturing real air before the rider arrives.
- [ ] **Expected-topology card per gate** (who is powered, who connects to whom, what `/status` must
      show on each board), so a gate can be *asserted* rather than discovered. Every board not in the
      card is powered off.
- [ ] **Restore values written down for both bikes** (§4) before any pairing is touched.
- [ ] **Workouts staged:** the `4×8 Threshold` preset with each rider's FTP
      (`code/scripts/import_workout.py --ftp` per rider until `ftp_w` lives in the config).
- [ ] **Two `/status` watch loops** ready (one per board); `perf_soak.py` is single-board.
- [ ] **The bench UI pass has run once** ([`bench-ui-pass.md`](bench-ui-pass.md)): every never-seen-on-a-panel
      cell of the UI map burned down at the desk and its gaps filed, so no rider time goes to a screen the
      camera could have checked (owner, 2026-09-24: bench first, then this session).

## 3. Bring-list

Both bikes powered; both riders' pedals charged; fresh CR2032s for every real crank; the C3 ride board
and the Guition(s) on the ride WiFi; the bike laptop with the sniffer dongle and ANT+ stick; the phone
with the Stages app (bike 1) and a second phone or the daughter's phone for bike 2 (whether one phone
can hold two bike pairings is unknown, see §7); a Garmin if G1b is to run.

## 4. Restore-list — write these down before touching any pairing

| Bike | Real L crank | Real R crank | Crank length | Cal offsets | Spoof id on its board | Pinned pedals |
|---|---|---|---|---|---|---|
| SB20 #1 | `62144` | `4963` | 165 mm | 903 / 951 | (fleet table) | `ASSIOMA 17039L` address |
| SB20 #2 | (G0) | (G0) | (G0) | (G0) | (fleet table) | `ASSIOMA 29064L` address |

The session-8/9 pairing recipe applies to both bikes: the app pairs **L = our spoof, R = that bike's
real right crank**; the SB20 needs *both* ids findable, so the real R crank stays powered.

## 5. The gates — in information order

| Gate | Proves | Needs | Pass looks like |
|---|---|---|---|
| **G0** desk pre-stage | everything in §2 | boards, laptop, both bikes powered | every box in §2 ticked; the topology cards written |
| **G1** bike 1 pairs | the session-8/9 path still holds with a unique spoof id | a fresh CR2032 in the real L crank, the Stages app | the SB20 shows watts from the pedals; `/log` says `source:connected`; the app shows L = spoof, R = `4963` |
| **G1b** #288 A/B | whether a Garmin paired over the *trainer* protocol degrades the bike's telemetry | a Garmin | one cold start with the Garmin trainer pairing removed, one with it present; the `0x2AD2` frame set is healthy (`0x00c5`) in one case and degraded (`0x0011`) in the other, or the hypothesis is refuted |
| **G2a** erg go/no-go (laptop) | the SB20 takes *our* FTMS Set Target Power | the rider pedalling steadily | `capture_ftms.py --erg --erg-targets 120,180,90 --erg-hold 25`: control-point indications `80 05 01` and felt resistance; golden erg vectors committed |
| **G2b** erg from the head unit | the on-device workout engine drives the real bike | the Guition only (no phone) | `/setup` trainer = the bike's full name; a preset; Start → `erg: ON <target>W` and the resistance follows; Skip and Stop release it |
| **G3** the workout | the whole stack under a real 53-minute session | — | `4×8 Threshold`, erg-driven if G2 passed else manual; `/stats` heap flat, no reset; ride-mode WiFi-off during the ride |
| **G4** bike 2 | the second bike pairs to its own head unit and pedals | bike-2 ids, the second phone | repeat G1 and G2b on SB20 #2 with spoof-2 and the daughter's pedals pinned by address |
| **G5** both at once | two bikes, two spoofs, two pedal sets, two head units in one room | two riders | each head unit shows its own rider; each SB20 ergs to its own target; no cross-binding on either `/status`; both `/stats` clean |
| stretch S1 | single-crank pairing (forward-plan §12) | 10 min | does the SB20 pair with only the spoof findable? |
| stretch S2 | MeterCompare with two real Assioma sets | both pedal sets on one bike | real A/B numbers replace the fabricated ones |
| stretch S3 | the qz/Peloton path through the proxy, head-unit erg OFF | qz on a phone/PC | one Peloton ride per bike; one erg controller at a time |

**The UI map rides along.** [`docs/ui-feature-map.md`](../docs/ui-feature-map.md) §4 marks each row bench or
bike; the bench pass ([`bench-ui-pass.md`](bench-ui-pass.md)) runs first, and the bike rows plus the rider's
check of every device-placed row are listed in Annex C with the gate they attach to.

## 6. Risks and landmines (designed around, not discovered at the bike)

1. **Identity collision.** A board at `Stages 62144` collides with bike 1's real crank; two boards at
   the same id collide with each other. G0 makes every powered board unique.
2. **Name-substring matching.** `ASSIOMA` matches both riders' pedals and `Stages Bike` matches both
   SB20s; pin by address and full name in `/setup`.
3. **Two erg controllers on one bike.** The head unit and qz must never both drive erg; in S3 the
   head-unit erg is off.
4. **Degraded telemetry until the Stages app connects** (#288). G1b answers it before the long ride.
5. **One phone, two bikes.** Unknown whether the app holds two pairings; use two phones.
6. **Radio load.** Two erg loops, two spoofs, two dual-broadcast pedal sets, phones and a laptop:
   ride-mode WiFi-off on both boards, non-participants off.
7. **The Guition has never ridden and is not compiled in CI** (#323). Its 30-minute soak (#332) runs
   before it rides.
8. **Bike 2's cranks are unknowns**; a dead R crank blocks pairing. Fresh batteries; inventory in G0.

## 7. Cleanup

Reset both boards to their fleet-table identities and pinned pedals; restore the app pairings from §4
if they were changed; power the non-participants back on; commit every capture to
`code/findings/captures/` and index it; write §8 and §9.

## 8. Actual — fill in as we go

| Gate | Result | Observed |
|---|---|---|
| G0 | | |
| G1 | | |
| G1b | | |
| G2a | | |
| G2b | | |
| G3 | | |
| G4 | | |
| G5 | | |

## 9. Retro — mandatory before close-out

What cost time, what the playbook should say, what `ROADMAP.md` Now/Next should change, and which
durable findings go to `decisions.md`.

## Annex A — carried forward from `BIKE-SESSION-READY.md` (moved here 2026-09-23)

**Desk blockers before any further OBC-on-bike work** (buttons only; the rides above need neither):
- [#291](https://github.com/cauldnz/SB20-power-proxy/issues/291) — the discovery-ordering deadlock: the
  C3 finds the SB20 by advertisement; qz's connection stops it advertising. Whoever attaches first
  locks the other out. Do not re-attempt OBC G2 on a rider's clock until this is decided.
- [#288](https://github.com/cauldnz/SB20-power-proxy/issues/288) — degraded telemetry, cause not
  established; the single-variable A/B is gate G1b above.

**Also carried forward from session 13:** hold-to-repeat on the SB20 buttons is built (qz fork
[PR #4850](https://github.com/cagnulein/qdomyos-zwift/pull/4850)) but never ridden; the nRF gates
N0/N1/N2 never ran; the nRF and the C3 cannot both serve OBC (reproduced twice).

**SB20 compatibility facts to VERIFY on the bike** (feeds the qz device docs
[PR #4852](https://github.com/cagnulein/qdomyos-zwift/pull/4852) and the Equipment Compatibility wiki
row, which currently reads `Stages | ? | Yes | Yes | Yes | Yes | ? | No`). Run them on a qz day (stretch S3),
not on the head-unit rides.

| # | Claim to test | Why it is not settled | How to test |
|---|---|---|---|
| V1 | Is qz's speed derived, or reported by the bike? | The bike's `0x2AD2` flags are `0x00c5`; bit 0 set *should* mean instantaneous speed is absent, but the flags were misread once already (a degraded `0x0011` frame was called a spec violation). Inference only until measured. | Ride at a steady cadence, then change gear/resistance without changing cadence. If qz's speed tracks power rather than anything wheel-like, it is derived. Cross-check the raw `0x2AD2` payload length against the flags. |
| V2 | Does the SB20 relay a heart-rate strap? | Never tried; the wiki says `?`. | Pair a HR strap to the bike in the Stages app, then see whether qz shows HR without pairing the strap directly to qz. |
| V3 | Zwift via qz's virtual bike | Untestable at the bench: the `-allow-nonroot` bench flag disables the virtual-device (BLE peripheral) role that Zwift needs. Needs a root qz run. | Run qz as root, pair qz (not the bike) to Zwift, confirm power/cadence reach Zwift and that ERG/auto-resistance works through it. |
| V4 | Hold-to-repeat on the SB20 buttons | Shipped in PR #4850, flagged as untested there. | Hold LEFT up ~3 s: expect ~13 actions (1 immediate, then every 200 ms after a 450 ms delay). Then a quick tap: expect exactly one. Report whether 450/200 ms feels right; they are named constants chosen by guess. |
| V5 | Is the `0x03` commit frame really rare, and does press *style* change it? | The central claim of PR #4850 rests on one session's data (2 commits vs 32 terminators, ~6%), mostly short taps; commit rate may depend on press duration. | Controlled counts on `0c46be60`, same button: 10 quick taps, 10 medium presses (~0.5 s), 10 long holds (~2 s). Count `0x01`/`0x03`/`0x04`/`0x08` per group; report the commit rate per group, not pooled. Note whether `0x04` always follows a commit and `0x08` never does. |

Whatever V5 finds, it does not invalidate PR #4850's fix (triggering on the `0x01` held-stream is
correct at any commit rate); it could change the explanation given to the maintainer.

**Already settled by the owner's day-to-day use (do not re-test):** auto-resistance/ERG through qz is
reliable, including qz-managed Peloton Power Zone workouts; power and cadence are reliable in normal use.

## Annex B — session-12 assumptions that no longer hold

Board firmware `6897578` is current (no: firmware changed on 07-26/27 and 09-23); PR #281 is pending
(merged 07-25); "OBC paddles folded in for free" (blocked by #291); "no ANT+ stick on the desk" (found
07-26); `L=62145` uniquely identifies the C3 (the Guition was also `62145` from 09-23); one rider, one
bike, `ftp_w 250`; the RSSI pre-flight in `flash.ps1` works (it was dead until 09-23); "OTA needs better
than −72 dBm" (first-attempt at −72/−82 now); the C3 as the only ride board (the Guition exists).

## Annex C — UI map rows checked in this session (added 2026-09-24)

The bike rows of [`docs/ui-feature-map.md`](../docs/ui-feature-map.md) §4 plus the rider's check of the
device-placed rows. Bench-testable rows are not repeated: they are the bench pass's job. Mark each
✅ / ❌ / ⚠️ with what was seen, at the gate it attaches to.

| Map row | What the rider checks | Gate | Result |
|---|---|---|---|
| F13 | live watts, cadence and balance from real pedals on the head unit; the phone on `/app` alongside if #351 has landed | G1 / G2b | |
| F15 | source and trainer names, link dots and RSSI on the ride title bar | G1 | |
| F16 | the current target, segment and time left are findable on the device mid-workout (today: the Workout screen only, not the ride view — record whether that is enough at the bike) | G2b | |
| F17 | the erg line walks searching → connected → controlled against a real SB20 | G2b | |
| F19 | identity and mode on More agree with the fleet table | G0 / G1 | |
| F20–F23 | load, start, pause, resume, skip, stop and change from the head unit while riding | G2b / G3 | |
| F27 | ±10 W from the SB20 buttons during erg (the OBC path; only if #291 is resolved) | G3 | |
| F28 | the profile bar, next block and clock stay legible for 53 minutes | G3 | |
| F38 | the build SHA readable on each board (today only via `/status`, #346) | G0 | |
| F26 | Peloton as the workout source — only once #342 Phase 1 exists; otherwise the qz path is stretch S3 | stretch S3 | |
| F33 | Compare with two real Assioma sets replaces the fabricated ×1.11 numbers | stretch S2 | |
