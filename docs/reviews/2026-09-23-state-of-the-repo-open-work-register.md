# Open-work register — appendix to the 2026-09-23 state-of-the-repo review

**Baseline:** `origin/main` = `8a64161`. Every open item found in the planning docs, the session
ledger, the checklists and the issue tracker, with its source and the bucket the review proposed for
`ROADMAP.md`. This table was the raw feed for the roadmap; the roadmap, not this table, is the
backlog. Paths in backticks, not links. Gate key: **bike** = needs the rider and an SB20; **board:X**
= a specific board; **owner** = a purchase, licence, decision or public action; **capture** = a
specific capture must land first; **desk** = pure desk work.

## 1. The timeline fact that frames everything

The last on-bike session is #13 on 2026-07-26. Commits ran daily to 2026-07-27, then nothing until
2026-09-15. All September work (#318–#326) was desk-only: the Guition port and validation on a
simulated meter, the C3 30-minute soak, the NVS-safe S3 flash, the OTA RSSI pre-flight. No July
"next" item (session 12, #290, #291) was touched by any September commit or document, and no
document records why the September work was chosen.

## 2. Bike-gated validations

| Item (all ids) | Source, status as stated | Gate | Notes | Proposed bucket |
|---|---|---|---|---|
| The erg go/no-go: does the SB20 move resistance on *our* FTMS Set Target Power, then a 53-min workout soak — tracked as session 12 G2/G3, `forward-plan` §14 phase 5, `ftms-protocol` "still the gate", `CAPTURE-sb20-erg-recovery` option A, dyno plan D4.1, and pre-beta Phase 0 + the 1-h soak | ledger "🟢 READY — next" (07-26); erg-recovery "🟢 READY … 5–10 min" | bike | one gate, six ids; session 12's pre-stage is stale (firmware changed 07-26/27 and 09-23; the OBC add-on is blocked by #291) | Now (session 14 G2/G3) |
| Real SB20 paddle → C3 shifter-sink → OBC → qz; live re-bind (session 11 G2/G3, #247) | ledger "🟢 READY"; doc "PLANNED"; session 13 "G2 BLOCKED" | desk decision (#291), then bike | three statuses for one item | Next (after #291) |
| On-device `/calibrate` meter-to-meter ride (session 5) | "🟢 READY" since 06-23 | bike (track bike + both meters) | the only ride-proof of the corrector mode; never scheduled | Later (opportunistic) |
| Session 10: real-SB20 erg from a head unit; tester UX on three boards; "which touch board earns the pre-beta slot" | ledger DEFERRED; header 🟢 READY; forward-plan §9 "next" | bike + boards | Day 1 absorbed by the erg item; Day 2 largely done at the desk; the Guition is absent from it | superseded |
| nRF BLE spoof vs a real SB20 (nrf-roadmap R3; session 12 stretch S1) | "never met a real SB20"; partial R3 on the bench 07-11 | bike + XIAO with S340 | the S340 SoftDevice exists in one checkout only | Later |
| Spoof the SB20 over its internal ANT+ link (nrf-roadmap; dreaming #13) | codec done; on-air open | bike + XIAO | in no session doc | Later |
| `forward-plan` §12 battery-out variant; single-crank app config | "still open … needs an nRF sniff" | bike + sniffer | not in session 12's gates | Later (session 14 stretch) |
| `forward-plan` §11 crank-length bridge; three grounding captures first | "🔲 capture-gated" | capture, then desk | related to #305 / R5b | Later |
| #288 degraded SB20 telemetry until the Stages app connects; Garmin-on-trainer-protocol A/B | "cause NOT established"; two A/B pcaps banked | bike (+ a desk pcap diff) | matters more for a stack that avoids the app | Now (session 14 G1b) |
| `BIKE-SESSION-READY` V1–V5 SB20 facts for the qz wiki; hold-to-repeat never ridden | "VERIFY on the bike" | bike (V3 needs root qz) | hold-to-repeat lives in the qz fork (PR #4850) | Next |
| Session 13 N0/N1/N2 (nRF SPA on the bike; nRF as OBC proxy; qz discovers the nRF) | "not run" | bike + XIAO; N2 needs the R9 desk fix | "nRF and C3 cannot both serve OBC" | Later |
| Pre-beta Phase 0 user-flow ride; Phase 2 1-hour on-bike soak | "the go/no-go"; "one unverified hypothesis" | bike | the 30-min desk soak of 09-23 is not the on-bike soak; session 14 G3 satisfies both | Now (folded into G3) |
| R1e.2 follow-up: CYD screen sweep after the view-model projection change | "wants the owner's eyes" | board:CYD (desk) | the bench camera (BOARDS.md 09-18) can close it | Next |
| `guition-board` §Still open: touch corner accuracy, INT/RST pin question, "not yet ridden", striping | open | board:Guition; ride = bike | in no session or plan doc | Now (GUITION2) / session 14 |

## 3. Desk decisions and architecture items that block the bike

| Item | Source, status | Gate | Proposed bucket |
|---|---|---|---|
| #291 discovery-ordering deadlock (C3 finds the SB20 by advertised name; a connected peripheral stops advertising; qz connects first) | "not solvable by ordering alone"; options: documented ordering / connect-by-address / C3 proxies everything | owner decision | Next (buttons only; the qz erg path needs none of it) |
| #290 system reference doc | not written; every session doc since 07-26 defers to it | desk | Now (REF) |
| Audit our OBC consumers for the seed-on-connect replay defect fixed in qz PR #4851 (decisions 07-26) | "not yet audited" | desk | Next |
| Session 13 R9: the nRF is invisible to qz as shipped (advertises `SB20 Bridge` + 0x1818 only) | "a firmware decision … desk work" | desk (XIAO to verify) | Later |
| Native SB20 decode + OBC listener double-fire in qz (session 13 R1) | "observed at the desk only" | desk (qz side) | Later |
| #289 OBC tap vs press-and-hold | open | protocol design | Later |
| #324 push/PR the qz-fork branch upstream (cagnulein #4785/#4791) | branch pushed and green; upstream PR not open; doc stale in two places | owner (public action) | Next |
| #292 validate the OBC producer against MyWhoosh | open | owner (Apple TV) | Later |
| #305 / R5b corrector-mode crank-length reply | "needs head-unit validation" | needs a Garmin | Later |
| #323 CI compiles neither S3 board env | confirmed: CI builds C3 + CYD + nRF only | desk | Now |
| #321 native MCP surface on the head unit | investigation issue; recommends a PC-side adapter first | desk | Next |
| #287 voice narration into the capture log | open | desk | Later |
| #131 esp-net-kit review, #144 shared POS services | June notices; nothing since | owner | close as acknowledged notices |

## 4. Structural checklists (R / U / P series)

| Item | Status as stated | Gate | Proposed bucket |
|---|---|---|---|
| R1b part 2: migrate the six Bridge write-callback bodies into `BridgeService` | "[~] part 1 done; part 2 deferred, best with hardware present" | board:XIAO + meter/IMU/trainer | Later |
| R1c IMU recorder extraction | "absorbed by Track Launch LC2; do not implement separately" | parked with launch control | Parked |
| R1d.3 nRF connection adapters | "do it only if it buys something concrete" | desk | Later |
| R1e.3 rest of the LCD block | "stays in main.cpp, on purpose" (box unticked) | — | closed |
| R2c Monkey-C golden-vector lock | "needs the Garmin SDK" | owner | Later |
| R3b named-offset accessor for the NVS line | "(optional) … deferred" | desk | Later |
| R4a/R4b config vocabulary | "low priority; opportunistic" | desk | Later |
| R7e truncated capture fields marked | box unticked but **shipped** as PR #316 | — | tick (E3) |
| R10 "not done here": the nRF codec→radio chain test | "revisit if the nRF grows its own CP path" | desk | Later |
| U1 `lcdHandleTap` ↔ LVGL `UiAction` parity test | "do it, or close U1 as satisfied" | owner decision | Later |
| P4 remainder: nRF ANT slave channel | "still open" | board:XIAO + ANT source | Parked |
| P5 / #249: third-party shifters over ANT → OBC | "needs a capture"; `AntControlsSource.h` exists host-tested despite "no code yet" | capture (no Di2/AXS transmitter owned) | Parked |
| nrf-roadmap follow-ups: remote reboot, gnu++17, `BOARD_HAS_IMU` gating, cadence −1 | "deferred" | desk | Later |
| Garmin Connect IQ app on-device | "builds; on-device pending" | owner (SDK + device) | Parked |

## 5. Feature backlogs

| Item | Status as stated | Gate | Proposed bucket |
|---|---|---|---|
| `forward-plan` §13 remainder: on-bike MCP-driven ride; persisted workout library; true pause/resume | "remaining" | bike / desk | Later |
| `forward-plan` §14 phase 5 = the erg item above; the doc, the findings index and the plan give it three statuses | "phase 4 SHIPPED 07-05" vs "remaining: phase 4" vs "(PLANNED)" | bike | Now (folded) |
| `meter-compare-visualization` §6: web plots; real two-meter data | "today every number is fabricated" | session 14 stretch | Next |
| `forward-plan` §8 distinct advertised identity per board | "backlog"; now a ride hazard (boards at `Stages 62144`/`62145`) | desk | Now (IDENT) |
| `forward-plan` §8 arbitrary (non-Stages) spoof identity | never tried; own-id `62145` works (session 8) | bike | Later |
| `ota-update-plan` P3/P4 signed-pull; tester log upload | "blocked on the backend + signing key" | owner | Parked |
| Pre-beta Phases 4–5: flash and ship ~10 boards; recruit | "then Phase 3–5" | owner | Parked (round 0 = the two of us first) |
| `forward-plan` §6 ESP Web Tools flasher | "for the production-deployment review" | desk | Later |
| `forward-plan` §3 Lane-2 leftovers; ANT+ Phase 1B | ledger "⛔ SUPERSEDED" | — | history |
| `advanced-board-s3-touch`: S3 ArduinoOTA deaf to espota | "open issue" | board:S3 | Later |
| The two dreaming lists (41 ideas; two graduated and paused) | "not a roadmap" | owner prioritisation | Parked |

## 6. What the documents said was "next" (date order)

| Date | Document | Stated next step |
|---|---|---|
| 2026-06-23 | `pre-beta-plan.md` | Phase 0 user-flow ride + two unknowns (arbitrary identity; 1-h soak) → ship pre-flashed, recruit |
| 2026-07-05 | `forward-plan.md` §9 | session 10 (2-day); desk: LVGL host tests (done 07-13), picker scrolling, `flash_s3.py` NVS fix (done 09-23); make `firmware` a required check (done) |
| 2026-07-10 | `HANDOFF-NEXT-SESSION.md` | hardware verifications (done 07-11) → R1b part 2 |
| 2026-07-26 | `sessions/README.md` banner | session 12, "but see #290/#291 first"; also READY: 11 and 5 |
| 2026-07-26 | session 13 retro | write #290; an expected-topology pre-gate; agent hygiene; Bayesian rules (done) |
| 2026-07-27 | `BIKE-SESSION-READY.md` | read #290 before planning; #291 and #288 first; validate hold-to-repeat; V1–V5 |
| 2026-07-27 | decisions.md ×6 | R1d.3 deliberately not done; nRF chain test not done; CYD sweep wants eyes |
| 2026-09-23 | decisions.md ×2 | Guition "not yet ridden"; C3 DoD notes for the next runner |

They agree on the bike (session 12, with #290 and #291 first) except `forward-plan` §9 (session 10)
and `pre-beta-plan` (Phase 0). They disagree on the desk (ship/recruit vs LVGL tests vs R1b vs
#290/#291). The September work follows none of them. Resolution: `ROADMAP.md`.

## 7. Done in code or the decision log but still listed open somewhere

- `forward-plan` §10 "remaining: the on-air confirm" — confirmed in session 9 (2026-06-26).
- `forward-plan` §9 desk items — two of three done (U5 on 07-13, the NVS-safe flash on 09-23).
- `perf-coex-plan` "no code yet" — phases A–D built; §12 definition of done met 09-23.
- `forward-plan` §8 FTMS layer "gated on that ride capture" — captured 07-06; only the erg write round-trip remains.
- `architecture-remediation` R7e unticked — shipped as #316; PROJECT-MAP already lists it ✅.
- PROJECT-MAP nRF "⏳ needs licensed S340" — provisioned 07-13; ANT+ master on air 07-14/15.
- `HANDOFF-NEXT-SESSION` §4 "ANT+ stick" — found and enumerated 07-26.
- `obc-protocol` header "M1 built; transports next" — M1–M6 and the shifter sink are built; G1 proven on air.
- `qz-upstream-contribution` §3 fix-2 and §7 — stale per session 13 (the name match landed; qz PRs #4850–#4852 exist).

## 8. Presented as done or ready while actually open

- Session 10 header 🟢 READY vs ledger DEFERRED; session 11 ledger 🟢 READY vs #291 BLOCKED.
- `on-device-workout-engine` "remaining: phase 4" and the index "(PLANNED)" vs `forward-plan` §14 "phase 4 SHIPPED".
- `obc-shifter-sources` "no code yet" vs `AntControlsSource.h` present and host-tested.
- `pre-beta-plan` "does the SB20 accept an arbitrary spoof identity" still open — the own-id half was answered in sessions 8/9; a non-Stages name was never tried.
- The findings index labels the approved launch-control spec/plan "(draft …)".

## 9. Hazards surfaced by the register

- The CYD was spoofing `Stages 62144`, the real L crank, in session 13 G0; the Guition was too on 09-23 ("renamed to 62145 now"), which is the C3 ride board's identity.
- The S340 SoftDevice and ANT key are gitignored and live in one checkout; the bike laptop cannot build `xiao-sense-s340`.
- CI builds neither S3 env (#323): exactly where every September change landed.
- `route_baseline.py` once wiped the C3's correction curve (decisions 07-27); the board's curve is the PR #233 artefact, not a ride calibration.
