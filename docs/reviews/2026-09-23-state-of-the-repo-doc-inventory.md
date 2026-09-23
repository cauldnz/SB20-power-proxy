# Doc inventory — appendix to the 2026-09-23 state-of-the-repo review

**Baseline:** `origin/main` = `8a64161` for the audit; last-commit dates computed on the branch that
carries this appendix. Classes: **living** (kept current), **index**, **plan** (proposes work),
**session** (a record of a physical session), **historical** (superseded; must carry a ⛔ banner),
**tester** (tester-facing), **generated** (do not edit), **record** (a dated review). Dispositions name
the PR of the review plan (C–F), `handoff` (a file the hardware session owns), `debt` (recorded, not
fixed), `leave` (correct as is) or an owner decision. Paths are in backticks, not links, on purpose.

## 1. Every Markdown document (143)

| Path | Class | Status as written | Last commit | Disposition |
|---|---|---|---|---|
| `01-project-brief.md` | historical | ⛔ SUPERSEDED banner | 2026-07-27 | leave |
| `02-technical-context.md` | historical | status note 06-15; no ⛔ | 2026-06-15 | E3: add banner |
| `03-central-hypothesis-and-phase-zero.md` | historical | status note 06-15; no ⛔ | 2026-06-15 | E3: add banner |
| `04-architecture.md` | historical | ⛔ SUPERSEDED | 2026-07-27 | leave |
| `05-implementation-phases.md` | historical | no banner | 2026-06-15 | E3: add banner |
| `06-prior-art-and-references.md` | historical | ⛔ SUPERSEDED | 2026-07-27 | leave |
| `07-hardware-and-environment.md` | historical | ⛔ SUPERSEDED | 2026-07-27 | leave |
| `08-risks-and-gotchas.md` | historical | ⛔ SUPERSEDED | 2026-07-27 | leave |
| `09-exploring-captures.md` | historical | ⛔ SUPERSEDED | 2026-07-27 | leave |
| `10-relationship-to-QZ.md` | historical | ⛔ SUPERSEDED | 2026-07-27 | leave |
| `11-ble-and-esp32-path.md` | historical | ⛔ SUPERSEDED | 2026-07-27 | leave |
| `12-digital-twins-and-capture.md` | historical | ⛔ SUPERSEDED | 2026-07-27 | leave |
| `AGENTS.md` | living | mirror of CLAUDE.md §Diagrams | 2026-07-13 | E1: map |
| `BIKE-SESSION-2.md` | session | ✅ DONE 06-18 | 2026-06-19 | E1: map as session record |
| `BIKE-SESSION-3.md` | session | ✅ DONE 06-19 | 2026-06-19 | E1: map as session record |
| `BIKE-SESSION-READY.md` | historical | ⛔ (07-27) yet the only home of live blockers #291/#288 and the V1–V5 table | 2026-07-27 | D: move live content to session 14; E1: reclass historical |
| `BOARDS.md` | living | none; Guition row says Arduino_GFX (abandoned); cites a non-existent scan_all.py | 2026-09-23 | handoff to the hardware session |
| `CALIBRATION-RIDE-CARD.md` | historical | ⛔ SUPERSEDED | 2026-07-27 | leave |
| `CHANGELOG.md` | historical | Revision 13, last 06-15; unmaintained | 2026-06-15 | E3: ⛔ banner; E1: reclass |
| `CLAUDE-CODE-PROMPT.md` | historical | "Outdated (06-15)" | 2026-06-15 | E3: add banner |
| `CLAUDE.md` | living | none; says numbered docs run 01–10 (there are 12); §Architecture lacks ui/, LVGL boards | 2026-07-25 | D: ROADMAP pointer; E3: two lines |
| `DEV-PLAYBOOK.md` | living | living | 2026-07-05 | leave |
| `HANDOFF-NEXT-SESSION.md` | historical | ⛔ (07-27); §4/§5 frozen at 07-11 | 2026-07-27 | E3: banner note; E1: reclass |
| `HANDOFF.md` | historical | ⚠️ soft note only | 2026-06-15 | E3: add banner |
| `NEXT-BIKE-SESSION.md` | historical | ⛔ SUPERSEDED | 2026-06-19 | leave |
| `PROJECT-MAP.md` | index | stale: 28 docs, session-04…09, living list, missing rows (see §2) | 2026-09-23 | E1 |
| `README.md` | index | rewritten 07-27; lacks Guition and src/ui/ | 2026-07-27 | D: ROADMAP row; E3 |
| `RIDE-CARD.md` | historical | ⛔ SUPERSEDED | 2026-07-27 | leave |
| `START-HERE.md` | historical | ⛔ SUPERSEDED | 2026-07-27 | leave |
| `USERS-PLAYBOOK.md` | living | living; "we haven't engaged a single user yet" | 2026-06-26 | leave (round-0 lessons later) |
| `beta/ONBOARDING.md` | tester | none; portal IP 192.168.4.1 is wrong (172.29.4.1) | 2026-06-26 | E3: IP fix |
| `beta/RELEASE-AND-OTA.md` | tester | runbook 06-26 | 2026-06-26 | leave |
| `beta/RIDE-FEEDBACK-FORM.md` | tester | none | 2026-06-26 | leave |
| `beta/TESTER-RIDE-PROTOCOL.md` | tester | none | 2026-06-23 | leave |
| `beta/comms-templates.md` | tester | none | 2026-06-23 | leave |
| `beta/pitch-posts.md` | tester | none | 2026-06-23 | leave |
| `beta/recruiting-and-selection.md` | tester | none | 2026-06-23 | leave |
| `code/README.md` | living | ANT+/Pi-era status table; links three ⛔ docs and a CLAUDE.md section that no longer exists | 2026-06-15 | E3 |
| `code/findings/README.md` | index | (PLANNED)/next markers stale on 5 entries; (draft) on approved docs | 2026-09-23 | E2 |
| `code/findings/advanced-board-s3-touch.md` | plan | planning §1–7 + bring-up log; board ✅ 07-02/05 | 2026-07-26 | E3: status line |
| `code/findings/architecture-remediation.md` | plan | IN PROGRESS; R7e unticked but shipped (#316) | 2026-07-27 | E3: tick R7e, ROADMAP pointer |
| `code/findings/beta-program.md` | plan | operating doc 06-22 | 2026-06-26 | E3: PARKED status |
| `code/findings/captures/README.md` | index | indexes 15 of 54 files | 2026-07-06 | E2: full index + guard |
| `code/findings/cyd-board.md` | living | ✅ PORTED + TWIN-TESTED 07-03 | 2026-07-03 | leave |
| `code/findings/decisions.md` | living | append-only; 07-27 → 09-23 gap; the 09-16 Guition bring-up never logged | 2026-09-23 | close-out entry; gap flagged to the hardware session |
| `code/findings/domain-primer.md` | living | orientation 06-23 | 2026-06-23 | leave |
| `code/findings/erg-response-dyno-autotuner-plan.md` | plan | OWNER-APPROVED; IMPLEMENTATION PAUSED | 2026-07-20 | leave (ROADMAP Parked) |
| `code/findings/erg-response-dyno-autotuner-spec.md` | plan | OWNER-APPROVED; PAUSED | 2026-07-20 | leave (ROADMAP Parked) |
| `code/findings/erg-response-dyno-autotuner.md` | plan | RESEARCH COMPLETE; PAUSED | 2026-07-23 | leave (ROADMAP Parked) |
| `code/findings/forward-plan.md` | plan | "Last updated 07-20"; §0–8 pre-pivot, §9 names a deferred session | 2026-07-23 | E3: banner (§0–8 historical, §9 → ROADMAP) |
| `code/findings/ftms-implementation-plan.md` | plan | PLANNED 06-21 (executed) | 2026-06-21 | E3: mark EXECUTED |
| `code/findings/ftms-protocol.md` | living | built F1–F6 | 2026-07-06 | leave |
| `code/findings/guition-board.md` | living | Phase-1 validated 09-23 | 2026-09-23 | handoff (hardware session's file) |
| `code/findings/mcp-workout-server.md` | living | ✅ desk-complete 06-26 | 2026-06-26 | leave |
| `code/findings/meter-compare-visualization.md` | plan | header PLANNED 07-16; body says shipped/landed 07-20 | 2026-07-20 | E3: fix header |
| `code/findings/meter-to-meter-proxy.md` | living | built 06-23 | 2026-06-23 | leave |
| `code/findings/nrf-roadmap.md` | plan | P1–P4 done, P5 open | 2026-07-15 | E3: ROADMAP pointer |
| `code/findings/nrf-sniffer.md` | living | ✅ verified live 06-21 | 2026-07-06 | leave |
| `code/findings/nrf52-sense.md` | living | "bring-up in progress (07-04)" (stale; proven 07-14/15) | 2026-07-14 | E3: status line |
| `code/findings/obc-protocol.md` | living | "M1 built; transports next" (stale; on air s13) | 2026-07-07 | E2: index line; E3: header |
| `code/findings/obc-shifter-sources.md` | plan | research, no code 07-07 (but AntControlsSource.h exists) | 2026-07-07 | E3: status line |
| `code/findings/on-device-workout-engine.md` | plan | IN PROGRESS 06-28; phase 4 TODO (shipped 07-05) | 2026-06-28 | E3: status line |
| `code/findings/ota-update-plan.md` | plan | P0–P2 shipped; P3/P4 gated on backend | 2026-06-24 | leave (ROADMAP Parked) |
| `code/findings/perf-coex-plan.md` | plan | "planning doc (no code yet)" (wrong; DoD met 09-23) | 2026-06-19 | E3: status line |
| `code/findings/perf-results.md` | living | append-only | 2026-09-23 | leave (hardware session's file) |
| `code/findings/phase-0-report.md` | living | complete 06-15; still the spoof spec | 2026-06-15 | leave |
| `code/findings/pre-beta-plan.md` | plan | north star (06-23) | 2026-06-26 | E3: PARKED status + un-park condition |
| `code/findings/qz-upstream-contribution.md` | living | stale in §3 and §7 per session 13 | 2026-07-25 | debt (ROADMAP Next: docs reconciliation) |
| `code/findings/ride-director-uplift-plan.md` | plan | PLANNED 06-20 (executed) | 2026-06-20 | E3: mark EXECUTED |
| `code/findings/ride-director.md` | living | built Phases 1–6 | 2026-06-21 | leave |
| `code/findings/sb20-hardware-reference.md` | living | secondary source 06-27 | 2026-06-27 | leave |
| `code/findings/sb20-power-topology.md` | living | ✅ RESOLVED 06-22 | 2026-06-22 | leave |
| `code/findings/screenshots/favero-assioma-app/README.md` | living | app screenshots | 2026-06-14 | leave |
| `code/findings/screenshots/stages-app/README.md` | living | app screenshots | 2026-06-14 | leave |
| `code/findings/screenshots/stages-power-app/README.md` | living | app screenshots | 2026-06-14 | leave |
| `code/findings/screenshots/zeekr-charger/README.md` | living | "unrelated to the SB20 work" | 2026-06-14 | owner decision (remove?) |
| `code/findings/session-G-ble-capture-spec.md` | living | executed capture spec (no status line) | 2026-06-14 | leave |
| `code/findings/shared-services-adoption.md` | living | verified 06-25 | 2026-06-25 | leave |
| `code/findings/shifter-ble-protocol.md` | living | mapped 06-19; decoder built 06-23 | 2026-06-23 | leave |
| `code/findings/shifter-erg-control.md` | plan | "research only, no code yet" (stale; Shifter.h shipped) | 2026-06-19 | E3: status line |
| `code/findings/sqlite-analysis-layer.md` | living | built + tested | 2026-07-27 | leave |
| `code/findings/stages-app-config.md` | living | reference 06-19 | 2026-06-19 | leave |
| `code/findings/supported-meters.md` | living | canonical 06-23 | 2026-06-23 | leave |
| `code/findings/track-launch-ble-transfer.md` | plan | research only 07-20 | 2026-07-20 | leave (ROADMAP Parked) |
| `code/findings/track-launch-control-plan.md` | plan | APPROVED BASELINE; PAUSED | 2026-07-20 | leave (Parked); E2: index says (draft) |
| `code/findings/track-launch-control-research.md` | plan | RESEARCH COMPLETE 07-20 | 2026-07-20 | leave (Parked) |
| `code/findings/track-launch-control-spec.md` | plan | APPROVED BASELINE; PAUSED | 2026-07-20 | leave (Parked); E2: index says (draft) |
| `code/findings/traffic-observability.md` | living | tooling built | 2026-06-21 | leave |
| `code/findings/ui-unification.md` | plan | IN PROGRESS; only U1 open (owner decision) | 2026-07-24 | E3: ROADMAP pointer |
| `code/findings/wsl-capture-runbook.md` | living | runbook | 2026-06-26 | leave |
| `code/findings/zwift-controls-research.md` | plan | parked to BACKLOG 06-19 | 2026-06-19 | E3: superseded by OBC |
| `code/scripts/BLE-LOOP.md` | living | runbook | 2026-06-17 | E1: map |
| `code/scripts/PC-CRANK.md` | living | runbook | 2026-06-17 | E1: map |
| `code/scripts/RIDE-WEB.md` | living | runbook | 2026-06-16 | E1: map |
| `design/REMOTE-RENDER-RECIPE.md` | living | recipe | 2026-06-27 | E1: map |
| `design/SHOW-IMAGE-RECIPE.md` | living | recipe | 2026-06-27 | E1: map |
| `design/s3-case/README.md` | living | 3D case | 2026-07-03 | E1: map |
| `design/s3-case/board-reference/README.md` | living | STEP geometry | 2026-07-03 | E1: map |
| `design/ui-architecture-review.md` | plan | review + plan; much shipped since (rescued 07-24) | 2026-07-26 | E1: map as design of record |
| `design/ui-schema-design.md` | plan | DESIGN 07-11; design of record | 2026-07-24 | E1: map |
| `docs/agents/domain.md` | living | skill config | 2026-07-25 | E1: map |
| `docs/agents/issue-tracker.md` | living | skill config | 2026-07-25 | E1: map |
| `docs/agents/triage-labels.md` | living | skill config | 2026-07-25 | E1: map |
| `docs/architecture.md` | living | conceptual architecture, 4 diagrams; linked from nothing | 2026-07-13 | E1: map as canonical |
| `docs/bike-session-workflow.md` | living | "not a second run book"; orphan | 2026-07-23 | E1: map; debt: fold into PLAYBOOK? |
| `docs/diagrams/README.md` | index | convention | 2026-07-13 | E1: map |
| `docs/dreaming-20-ideas.md` | historical | idea list, no status line | 2026-07-15 | E3: status line; E1: map |
| `docs/reviews/2026-09-23-review-sources.md` | record | dated snapshot (PR #327) | 2026-09-23 | leave |
| `docs/reviews/2026-09-23-review-synthesis.md` | record | dated snapshot (PR #327) | 2026-09-23 | leave |
| `docs/reviews/2026-09-23-state-of-the-repo-branch-triage.md` | record | appendix (this PR) | (this PR) | C |
| `docs/reviews/2026-09-23-state-of-the-repo-code-inventory.md` | record | appendix (this PR) | (this PR) | C |
| `docs/reviews/2026-09-23-state-of-the-repo-doc-inventory.md` | record | appendix (this PR) | (this PR) | C |
| `docs/reviews/2026-09-23-state-of-the-repo-open-work-register.md` | record | appendix (this PR) | (this PR) | C |
| `docs/reviews/2026-09-23-state-of-the-repo.md` | record | dated snapshot (this PR) | (this PR) | C |
| `docs/reviews/README.md` | index | folder index (this PR) | (this PR) | C |
| `docs/sol-dreaming-17Jul26.md` | historical | "DREAMING, not a roadmap" | 2026-07-20 | E3: status line; E1: map |
| `firmware-nrf/GATT.md` | living | Bridge GATT contract, PROTO_VER 1 | 2026-07-20 | leave |
| `firmware-nrf/ciq/README.md` | living | Connect IQ app | 2026-07-05 | leave |
| `firmware-nrf/vendor/softdevice/README.md` | living | S340 drop zone | 2026-07-27 | E1: map |
| `firmware/BENCH-FLASH.md` | living | header names a dead branch, "expect 56/56", two portal IPs, env table omits LCD/nRF envs | 2026-09-23 | handoff (hardware session's file) |
| `firmware/README.md` | living | "initial scaffold, verified" (three months stale); lists a non-existent env | 2026-07-11 | E3 |
| `firmware/lib/monocypher/LICENCE.md` | living | vendored licence | 2026-06-24 | leave |
| `firmware/webflash/README.md` | living | browser flashing | 2026-06-23 | E1: map |
| `sessions/CAPTURE-qdomyos-sb20-passive.md` | session | ✅ DONE 07-06; not in the ledger | 2026-07-06 | E2: ledger row |
| `sessions/CAPTURE-sb20-erg-recovery.md` | plan | 🟢 READY 07-06; not in the ledger | 2026-07-06 | E2: ledger row (folded into session 14 G2a) |
| `sessions/PLAYBOOK.md` | living | living; l.112 still says Wireshark/tshark | 2026-07-27 | E3: two lines |
| `sessions/README.md` | index | omits 4 session docs; disagrees with s10/s11 headers; cold-start points at a ⛔ doc | 2026-07-26 | D/E2 |
| `sessions/nrf-s340-ant-bringup.md` | session | ✅✅ DONE 07-14; not in the ledger | 2026-07-14 | E2: ledger row |
| `sessions/session-04-enhanced-offset-and-brake-levers.md` | session | ✅ DONE 06-21 | 2026-07-27 | leave |
| `sessions/session-05-meter-calibration-capture.md` | plan | 🟢 READY since 06-23, never run | 2026-06-28 | E2: ⏸️ DEFERRED |
| `sessions/session-06-sniff-and-power-topology.md` | session | ✅ DONE 06-21 | 2026-06-21 | leave |
| `sessions/session-07-comprehensive-monitor.md` | session | ✅ DONE 06-22 | 2026-06-22 | leave |
| `sessions/session-08-sb20-spoof-calibration.md` | session | ✅ DONE 06-25 | 2026-06-25 | leave |
| `sessions/session-09-zero-reset-onair-confirm.md` | session | ✅ DONE 06-26 | 2026-06-26 | leave |
| `sessions/session-10-spin-bike-ui-tryout.md` | plan | header 🟢 READY; ledger ⏸️ DEFERRED | 2026-07-25 | E2: ⛔ SUPERSEDED |
| `sessions/session-11-obc-bike-test.md` | plan | header PLANNED 07-08; ledger 🟢 READY; G2 blocked by #291 | 2026-07-12 | E2: 🔒 BLOCKED |
| `sessions/session-12-LAPTOP-HANDOFF.md` | living | cold-start for session 12; no status line; orphan | 2026-07-25 | E2: status line + ledger row |
| `sessions/session-12-erg-workout-validation.md` | plan | 🟢 READY 07-25; pre-stage stale | 2026-07-25 | D/E2: ⛔ SUPERSEDED by session 14 |
| `sessions/session-13-qz-obc-consumer-and-sb20-buttons.md` | session | ✅ DONE 07-26 | 2026-07-26 | leave |
| `tools/README.md` | living | names the shipping env esp32c3-oled-live (the OTA path needs -ota) | 2026-07-26 | E3: env name |
| `ui-schema/web-json.md` | generated | GENERATED, do not edit | 2026-07-20 | E1: map as generated |
| `web/HTTP-API.md` | living | "added in U4 (deferred)" (stale; U4 shipped #268) | 2026-07-10 | E3 |
| `web/README.md` | living | "ESP32 … (planned)" (stale; hardware-verified #234) | 2026-07-05 | E3 |

By class: generated 1, historical 23, index 7, living 58, plan 30, record 7, session 10, tester 7.
By disposition: C 6, D 3, D/E2 2, E1 22, E2 10, E3 32, close-out 1, debt 1, handoff 3, leave 62, owner 1.

## 2. Index staleness — the exact lines (at `8a64161`)

### `PROJECT-MAP.md` (last changed 2026-07-27)
| Where | Stale statement | What is true |
|---|---|---|
| l.12–13, l.177–179 | CI "fails if a `beta/` **or `sessions/`** doc isn't mapped here" | `test_project_map.py` checks `beta/*.md` and three index paths only; sessions 10–13, the S340 bring-up, both `CAPTURE-*` docs and the laptop hand-off are unmapped with CI green |
| l.123 | "`code/findings/` … (**28 docs**)" | 47 top-level findings docs |
| l.130 | "one doc per session (`session-04`…`session-09`)" | session docs run 04…13 plus three un-numbered ones |
| l.138–143 | "living operational docs" include `BIKE-SESSION-READY.md`, `HANDOFF-NEXT-SESSION.md`, `CHANGELOG.md` | the first two carry a ⛔ banner (added 07-27 by the same arc that left the map untouched); the third stopped 06-15 |
| §A | no row for OBC, Guition, Waveshare S3, CYD (in passing only), the on-device workout engine, the LVGL host harness, the bridge codegen, the web-JSON lint, the nRF BLE spoof mode, the ANT+ master, the flash guards, `flash_s3.py` NVS-preserve, MeterCompare, the bench camera, the 30-min DoD soak, `test_doc_links.py`, R1a–R1d/R3a | all shipped between 07-03 and 09-23 |
| l.101 | nRF "ANT+BLE mix — ⏳ needs licensed S340 SoftDevice" | S340 + key provisioned 07-13; ANT+ master on air 07-14; full loop bench-proven 07-15 |
| §C l.153–161 | tool list | omits 12 scripts: `03_ingest_jsonl_to_influx`, `15_monitor_ride`, `bench_s3`, `check_generated`, `compare_meters`, `flash_s3`, `gen_bridge`, `gen_webjson`, `import_workout`, `obc_reader`, `route_baseline`, `run_capture.sh` |
| §C l.162–164 | envs: `native`, `esp32c3-supermini`, `esp32c3-wifi`, `esp32c3-oled-live` "(the shippable build)" | 37 envs; the shipping build is `esp32c3-oled-live-ota` |
| §D l.171–173 | `firmware/src/` = `ble/`, `net/`, `disp/` (OLED) | `src/ui/` (LVGL) exists; `disp/` holds LCD seams; `lib/` also has `esp_lcd_axs15231b/` and `monocypher/`; ~30 pure headers and 5 Python modules unlisted |
| l.3–6 | "the index of … every doc" | 41 docs in no index; `docs/`, `design/`, `ui-schema/`, `code/scripts/*.md`, `firmware/BENCH-FLASH.md` absent from §B |

### `code/findings/README.md` (last changed 2026-09-23)
| Where | Stale statement | What is true |
|---|---|---|
| l.39 | on-device-workout-engine "(PLANNED)" | phases 1–4 on main (FTMS wire 07-05) |
| l.52 | meter-compare-visualization "(PLANNED)" | shipped/landed 07-16 and 07-20 per its own body |
| l.69 | obc-protocol "M1 done; BLE/network transports next" | transports, shifter-sink and web config merged 07-10; on air in session 13 |
| l.72 | advanced-board-s3-touch "planning ideas" | board boots with the full LVGL touch UI (07-05) |
| l.77 | nrf-roadmap "P1 (the C++ ANT page codec) is DONE" | P2 done, BLE spoof 07-10, S340 07-13, ANT+ master on air 07-14/15 |
| l.81–82 | track-launch spec/plan "(draft …)" | the docs' own status is APPROVED BASELINE; PAUSED |
| l.85 | captures/README "the index of committed capture files" | it lists 15 of 54 |

### `sessions/README.md` (last changed 2026-07-26)
| Where | Stale statement | What is true |
|---|---|---|
| title | "the single index of every … session" | omits `nrf-s340-ant-bringup.md`, both `CAPTURE-*.md`, `session-12-LAPTOP-HANDOFF.md` |
| l.23 | "cold-start: BIKE-SESSION-READY.md" | that doc opens with "⛔ SUPERSEDED … Do not follow it" |
| l.21–22 | open desk items include "the nRF sniffer" | proven live 07-25 |
| row 10 | "⏸️ DEFERRED" | the doc header says 🟢 READY |
| row 11 | "🟢 READY" | the doc header says PLANNED; G2 is blocked by #291 |
| row 5 | "🟢 READY" (06-23) | never run in three months |

### `README.md` (last changed 2026-07-27)
| Where | Stale statement | What is true |
|---|---|---|
| l.76 | "`firmware/` ← ESP32-C3 / CYD / S3" | the Guition is a fourth target |
| l.78 | "`src/` ← ble/, net/, disp/" | `src/ui/` omitted |
| l.141–142 | "Each [historical root doc] carries a banner" | 02, 03, 05 carry only a status note; `HANDOFF.md` a ⚠️; `CLAUDE-CODE-PROMPT.md` "Outdated"; `CHANGELOG.md` nothing |

## 3. Other doc findings

1. `decisions.md` jumps from 2026-07-27 to 2026-09-23; the 09-23 Guition entry refers to "the 2026-09-16 bring-up (above)", which was never logged. The narrative exists only in `guition-board.md`.
2. `BOARDS.md` says the Guition is "driven via Arduino_GFX (pinned 1.6.0)"; `guition-board.md` says Arduino_GFX was abandoned for the vendored `esp_lcd_axs15231b` driver. `BOARDS.md` also cites `code/scripts/scan_all.py`, which does not exist, and does not say which C3 is the ride board.
3. `firmware/BENCH-FLASH.md` still instructs `git checkout claude/esp32-bike-powermeter-urnc0c`, says "expect 56/56", links the ⛔ `NEXT-BIKE-SESSION.md`, and gives the setup-portal address as both `172.29.4.1` and `192.168.4.1`. The correct address (`172.29.4.1`, PR #228) is contradicted by `firmware/README.md` and the tester-facing `beta/ONBOARDING.md`.
4. The shipping C3 env is named three ways: `esp32c3-oled-live` (PROJECT-MAP, RELEASE-AND-OTA, tools/README, the `flash.ps1` default), `esp32c3-oled-live-ota` (CLAUDE.md, README, decisions 09-23, CI) and `esp32c3-oled96sh-live` (BOARDS.md, for the now-defective 0.96" board).
5. `sessions/PLAYBOOK.md` l.112 still says "the nRF dongle + Wireshark/tshark", the refuted sniffer path the 07-27 arc removed elsewhere in the same file.
6. Three "how many tests pass" figures drift across docs (45/45, 56/56, ~462, 471); none is current. Counts date a document; they should not be repeated in indexes.
7. `BIKE-SESSION-READY.md` is simultaneously "do not follow" and the only home of the #291/#288 blockers, the V1–V5 qz-compatibility table and "hold-to-repeat built, never ridden".
