# State of the repository — 2026-09-23

**Document date:** 2026-09-23. **Status:** dated snapshot and disposition record; not a backlog.
**Audited baseline:** `origin/main` at `8a64161` (PR #325 merged). **Hygiene performed at:** `be32624`
(PR #326 merged). **Method:** read-only sweeps of a clean snapshot (every Markdown doc; every source,
test, script and build env; every branch, PR and issue on GitHub), reconciled against the append-only
[decision log](../../code/findings/decisions.md). The evidence tables are the four appendices listed in
[`README.md`](README.md). This review supersedes nothing: the
[2026-09-23 synthesis](2026-09-23-review-synthesis.md) stands as the reconciliation of the July
five-model review, and this document is the whole-repo audit that follows it.

## 1. Verdict

The engineering is in good shape and the record is complete: nothing valuable is lost. What had
become a mess is the **layer that tells a session what to do next**: a stale local checkout, about
80 leftover branches, four planning documents that each claim to be the plan, three index documents
that describe a July repository, and a north star (the ten-tester pre-beta) that nobody had
re-affirmed or retired while the work moved elsewhere for two months. The fixes are documentary and
procedural, and each of them can be guarded by a test so the same drift cannot recur silently.

Three things a maintainer should know before anything else:

1. **The only unmerged code in the whole repository was PR #255** (27 lines, "IP + RSSI on the LCD
   More tab", closed 2026-07-11 without a comment). Every other branch was a squash-merge leftover.
2. **Two identity hazards are live on the bench today.** Every ESP32 build defaults to `Stages 62144`
   (`firmware/lib/proxy/Config.h`), which is bike 1's real left crank; and on 2026-09-23 the Guition
   was renamed to `Stages 62145`, the identity the C3 ride board has carried since session 11. With a
   second SB20 now in the house, an identity collision stops being a bench nuisance.
3. **The owner reset the north star on 2026-09-23** (§4): a two-bike training stack for two riders,
   with the two of them as beta round zero. The pre-beta *programme* is parked; its capabilities stay.

## 2. Addendum to the 2026-09-23 synthesis

The synthesis inspected `6d5afa2` and named PR #322 as open. Since then:

| PR | Merged | What |
|---|---|---|
| #322 | 2026-09-23 | Guition validated end to end on a simulated meter; `flash_s3.py` stops wiping NVS on a USB flash; per-board hostname `sb20proxy-guition.local` |
| #325 | 2026-09-23 | The C3's 30-minute "ride-ready" soak: zero reboots, heap flat, dual-role BLE costs nothing; the only loop stalls are the synchronous web server and they do not touch the crank stream; `fake_meter` no longer fails silently |
| #326 | 2026-09-23 | `flash.ps1`'s OTA RSSI pre-flight had been dead for months (it read `/` after `/` became HTML); C3 push-OTA is reliable at −72 dBm |

Branch protection re-read on 2026-09-23 (the synthesis asked for this): a pull request is required
(0 approving reviews), required status checks are `pytest (3.10)`, `pytest (3.12)`, `firmware` and
`bridge-parity` with strict up-to-date enforcement, `pytest (3.14)` is **not** required although
the bike laptop runs 3.14, `enforce_admins` is off, force-pushes and deletions are blocked, no
rulesets. Whether to require 3.14 and enforce for admins is an owner decision; not changed here.

## 3. The repository in numbers

| Measure | Value |
|---|---|
| Commits on `main` | about 690 (May 2, June 441, July 235, **August 0**, September 13) |
| Pull requests | 326 at the audit; #327 (the synthesis) opened by this review |
| Issues | 18 total, 13 open; three carry a triage label |
| Markdown documents | 135 (47 top-level `code/findings/` docs; 30 at the repository root) |
| `decisions.md` | 3,887 lines, 151 entries; a two-month gap between 2026-07-27 and 2026-09-23 |
| Python | 71 modules, 49 scripts, 463 tests in 66 files |
| ESP32 firmware | 53 pure headers + 1 spoof profile in `lib/proxy/`, 37 PlatformIO envs, 10 native suites (347 cases) |
| nRF firmware | 6 pure headers, 4 envs, 3 native suites (81 cases) |
| CI | one workflow, four jobs; compiles 6 of the 37 ESP32/nRF envs |
| Captures | 54 files, 43 MB; the index lists 15 |
| TODO/FIXME in source | 2 |

## 4. The north-star reset (owner, 2026-09-23)

The value proposition was locked on 2026-06-22 as "power-meter pedals become the crank
replacement, shipped pre-flashed to about ten SB20-owner testers" (`pre-beta-plan.md`,
`beta-program.md`). Almost all work since 2026-07-05 went to head-unit boards, the nRF bridge and
ANT+, the OBC button proxy and qz, the on-device workout engine, Garmin Connect IQ and MCP surfaces.
No tester was ever shipped a board, and no document re-stated the goal.

On 2026-09-23 the owner restated it: **a training stack for two riders on two SB20s.** A second SB20
with working cranks; each rider on their own Assioma pedals; a Guition head unit on each bike (a
second one to be ordered; a larger Guition variant may be tried); the on-device workout engine as
the primary erg driver with the qz/Peloton path kept working; riding together and apart; and the
owner and their daughter as **beta round zero**, using the tester kit on themselves. Consequences:

- The pre-beta *programme* (recruiting, shipping, the weekly loop) is parked with an explicit
  un-park condition. The *capabilities* it produced (setup UI, `/diag` and `/report`, the QA card,
  the OTA fleet runbook, the tester documents) are exactly what round zero exercises.
- "FTMS / erg / training: long-tail per the locked value-prop" (PROJECT-MAP) is no longer true; the
  erg path is the centre of the Now lane.
- The single most valuable missing document is the one issue #290 asks for: how the pieces are meant
  to fit at runtime, now including two bikes, two pedal sets and two head units in one room.

The priorities that follow from this live in `ROADMAP.md` at the repository root, not here.

## 5. Where the mess is

### 5.1 The local checkout and the branch list

The desk checkout sat at `df0d015` (2026-07-20), 82 commits behind `origin/main`, while other
machines advanced the repository. Three git worktrees from July were still registered (two under
`.claude/worktrees/`, one under the Microsoft account's `copilot-worktrees` folder). There were 32
remote and 45 local branches. `git cherry` against `origin/main`, cross-checked with the commit lists
of the merged PRs, showed all but two to be squash-merge leftovers (appendix: branch triage). The
two exceptions: `cauldnz-review-synthesis` (a finished, unmerged Copilot session; PR #327) and
`feat/cyd-wifi-status` (PR #255).

### 5.2 Plan fragmentation

Open work was spread across `forward-plan.md` §0–§16 (the §0–§8 lanes are pre-pivot ANT+ history
never marked as such; §9 "Do this next", refreshed 2026-07-05, names a session the ledger deferred),
`pre-beta-plan.md` (updated 2026-06-23), `architecture-remediation.md` R1–R10, `ui-unification.md`
U0–U5, `nrf-roadmap.md` P1–P5, two owner-approved-then-paused programmes (erg dyno/autotuner in
three docs, track launch control in four: 4,200 lines with no code), two "dreaming" lists, 13 open
issues and four planned bike sessions. The same erg go/no-go gate is tracked under four different
identifiers. No document ranks any of it, and no "next" statement is newer than 2026-07-27; the
September work (Guition port, C3 soak) follows none of the July "next" statements and no document
records why it was chosen. Details: the open-work register appendix.

### 5.3 Index staleness

`PROJECT-MAP.md` still says "28 findings docs" (there are 47) and "session-04…session-09" (there
are docs to 13), lists `BIKE-SESSION-READY.md`, `HANDOFF-NEXT-SESSION.md` and `CHANGELOG.md` as
"living" although the first two carry a ⛔ superseded banner and the third stopped on 2026-06-15,
and has no capability row for the OBC button proxy, the CYD/S3/Guition head units, the on-device
workout engine, the LVGL host harness, the bridge codegen or the nRF ANT+ master. Its own CI test
checks `beta/` coverage only, although the map says it also checks `sessions/`. The findings index
carries "(PLANNED)" on two shipped docs and "(draft)" on two approved ones. The session ledger
omits four session documents and disagrees with two session headers about their status. The
captures index lists 15 of 54 files and has no test. 41 documents are in no index at all. Details
and the exact stale lines: the doc inventory appendix.

### 5.4 Product-line drift

Ten product lines exist in some state (crank spoof, meter-to-meter corrector, OBC button proxy, nRF
bridge and ANT+, four head-unit boards, on-device erg/workouts, Garmin Connect IQ, MCP, and the two
paused programmes). The governance docs call one of them core and the rest long-tail; the activity
says otherwise. §4 resolves this; the map's new product-lines section records it.

## 6. Which document wins

| Question | Canonical answer | Superseded or duplicate copies |
|---|---|---|
| What exists | `PROJECT-MAP.md` §A | CLAUDE.md §Architecture (partial), `firmware/README.md` (scaffold era), `code/README.md` (ANT+ era) |
| How the system is shaped | `docs/architecture.md` (four rendered diagrams) | `04-architecture.md`, `11-ble-and-esp32-path.md` (both ⛔), `design/ui-architecture-review.md` (design of record for the UI only) |
| What was measured or decided | `code/findings/decisions.md` | `CHANGELOG.md` (stopped 2026-06-15) |
| What to do next | `ROADMAP.md` (from PR D onward) | `forward-plan.md` §9, `pre-beta-plan.md` §Sequencing, `HANDOFF-NEXT-SESSION.md` §5, `BIKE-SESSION-READY.md` blockers, the checklists' unticked boxes |
| How to run a bike session | `sessions/PLAYBOOK.md` | `docs/bike-session-workflow.md` (a short companion, not a second run book) |
| Cold start on the bike machine | the session doc plus `sessions/session-12-LAPTOP-HANDOFF.md` | `BIKE-SESSION-READY.md`, `HANDOFF-NEXT-SESSION.md`, `HANDOFF.md`, `NEXT-BIKE-SESSION.md` (all ⛔) |
| FTMS | `code/findings/ftms-protocol.md` | `ftms-implementation-plan.md` (executed, unmarked) |
| Ride Director | `code/findings/ride-director.md` | `ride-director-uplift-plan.md` (executed, unmarked) |
| Build and flash | `tools/README.md` (guards, pinned toolchain) + `firmware/BENCH-FLASH.md` (env table) | CLAUDE.md §Commands, README quick start, `beta/RELEASE-AND-OTA.md`, `firmware/README.md` |
| Which board is which | `BOARDS.md` | the per-board findings docs (detail, not inventory) |

## 7. Index and guard gaps, and the guards that close them

| Gap found | Guard (PR) |
|---|---|
| Session docs can exist without a ledger row | `code/tests/test_sessions_ledger.py` (E2) |
| Capture files can exist without an index row; the index can name missing files | `code/tests/test_captures_index.py` (E2) |
| A doc the map calls "living" can carry a ⛔ banner | `test_project_map.py::test_living_docs_are_not_banner_superseded` (E1) |
| `docs/` and `design/` docs are in no index | `test_project_map.py::test_docs_and_design_are_mapped` (E1) |
| A historical root doc can lack the banner | `test_project_map.py::test_historical_docs_carry_a_banner` (E3) |
| Generated files without a guard | recorded as debt (§11): `webflash/manifest.json`, the LVGL fonts, `LcdFont.h`, `docs/diagrams/*.svg`, the CIQ `BridgeBle.mc` mirror |

Already present and working: `test_findings_index.py`, `test_doc_links.py` (583 links),
`check_generated.py` (pre-push and CI), the required-check set.

## 8. Code findings worth acting on

None of these is urgent; all are recorded so they are not re-discovered.

- **Only-their-tests-use-them modules:** `code/src/sb20proxy/llm.py`, `obs.py`, `logparse.py`;
  `sources/base.py` is an unused re-export shim. Firmware: `lib/proxy/AntControlsSource.h` and
  `ObcSb20Map.h` are included only by tests; the non-LVGL LcdCanvas render path in
  `firmware/src/main.cpp` is never compiled for a board.
- **Duplicate implementations, deliberate and recorded:** the capture-side CPS decoder in
  `06_capture_ble.py` versus the runtime `ble/cps.py` (R7b: different contracts); `decode_fec` in
  `07_capture_multi.py` versus `ant/fec.py`; a third least-squares `linfit` in `08_analyze_grid.py`.
- **Unguarded generated artifacts:** listed in §7.
- **Stale references in code:** `firmware/platformio.ini` line 89 cites a `readboot.py` that does
  not exist; `design/render/_gen.py` hard-codes an absolute repository path and a Chrome path;
  several script docstrings cite example capture filenames that never existed.
- **CI coverage:** the `firmware` job compiles `esp32c3-supermini`, `esp32c3-wifi`,
  `esp32c3-oled-live-ota`, `esp32cyd`, `xiao-sense` and `feather-nrf52840`; none of the six
  `esp32s3-pio*` or five `esp32-guition*` envs (#323). Every September firmware change landed on an
  env CI does not build.
- **Version strings disagree:** Python `0.0.1`, firmware `SB20_FIRMWARE_VERSION "0.1.0"`,
  `webflash/manifest.json` a stale short SHA.

## 9. Branch and worktree hygiene performed (2026-09-23)

- Removed the three stale worktrees (all detached, clean).
- Fast-forwarded the local `main` from `df0d015` to `be32624`.
- Deleted 43 local branches: 38 whose every commit was patch-equivalent to `origin/main`, and 5
  that carried only a sync-merge plus a commit landed by a merged PR (#301, #302, #309, #313, #280).
- Deleted 28 remote branches after re-running `git cherry` on each (a 29th had already been deleted
  by the hardware session that day).
- Kept `cauldnz-review-synthesis` (opened as PR #327) and `feat/cyd-wifi-status` (PR #255; to be
  re-filed as an issue and then deleted).
- The full name-to-SHA listing taken before deletion is in the branch-triage appendix; any branch can
  be restored with `git push origin <sha>:refs/heads/<name>`.

## 10. Hazards the two-bike goal introduces

| Hazard | Evidence | Cheapest mitigation |
|---|---|---|
| The Guition and the C3 ride board share `Stages 62145` | decisions.md 2026-09-23; session 12 §G0 | rename now; a fleet identity table |
| A fresh or erased board advertises `Stages 62144`, bike 1's real crank | `Config.h`; session 13 G0 found the CYD doing it | a MAC-derived default identity (a Now item); until then a pre-ride checklist line |
| `ASSIOMA` matches both riders' pedals | session 2's source bouncing | pin `meterAddress` per board (exists) |
| `"Stages Bike"` and the trainer name filter match both SB20s | `main.cpp` shifter scan; `FtmsErgClient` name filter | use the full names now; address pins next |
| Two erg controllers on one bike (head unit and qz) | an ordering constraint not yet written down | an explicit mode rule in the system reference doc |
| SB20 telemetry can boot degraded until the Stages app connects | #288, unresolved | the single-variable A/B first thing in the next session |
| The Guition has never ridden and is not compiled in CI | `tests.yml`; guition-board.md | #323 plus a 30-minute soak before it rides |
| No fleet-wide instrument for two boards | `perf_soak.py` and `route_smoke.py` are single-board | two `/status` loops now; a fleet status script next |

## 11. Disposition register

| Finding | Disposition |
|---|---|
| Stale local checkout, branches, worktrees | Fixed (§9) |
| Copilot review synthesis unmerged | PR #327 |
| No single backlog; four "next" statements | `ROADMAP.md` (PR D); banners on the superseded statements (PR E3) |
| PROJECT-MAP stale lines and missing capabilities | PR E1, with the stronger test |
| Findings index markers, ledger rows, captures index | PR E2, with two new tests |
| Executed plans still marked PLANNED; missing ⛔ banners; wrong portal IP in tester docs; three names for the shipping env | PR E3 |
| No runtime reference (issue #290), now for two bikes | PR F |
| Identity defaults, CI coverage of S3/Guition envs (#323), address-based affinity | ROADMAP Now items for a code session; not changed by this docs line |
| `BOARDS.md` (Arduino_GFX row, non-existent `scan_all.py`, which C3 rides), `BENCH-FLASH.md` header, the un-logged 2026-09-16 Guition bring-up in `decisions.md` | handed to the hardware session (its files) |
| Dead modules, duplicate decoders, unguarded generated files, stale code references, version strings | recorded here as debt; ROADMAP Later |
| `code/findings/screenshots/zeekr-charger/` (unrelated content) | owner decision |
| Branch protection (3.14 not required; admins not enforced) | owner decision |

## 12. Evidence boundaries and maintenance

Everything here was read from the tree, the git history and GitHub at the stated commits; no
hardware was touched and no test was re-run beyond the doc guards. Counts date this document
precisely and will drift; do not update them in place. To extend: append a dated addendum below, or
write a new dated review and add it to [`README.md`](README.md).
