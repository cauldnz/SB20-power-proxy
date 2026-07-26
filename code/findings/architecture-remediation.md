# Architecture remediation plan — the living checklist

**Status:** IN PROGRESS (opened 2026-07-10). Tick boxes here as each slice ships; this doc is the
source of truth for the structural cleanup, so we don't carry it in a chat context window.

**Origin:** the 3-agent architecture audit logged in [`decisions.md`](decisions.md) **2026-07-10
("Architecture audit …")**. Read that entry for the evidence; this doc is the *plan* derived from it.

**Guiding principle — fix the *accidental* fragmentation, leave the *essential*.** Three serializers /
storage backends (NVS `|`-line, one-MTU GATT binary, HTTP JSON) and the scalar-vs-curve correction
*model* are justified by genuinely different transports and are already unified where it matters (the
shared `Correction` math). Don't collapse those. Target the duplication and the missing seams.

**How we execute (per [`DEV-PLAYBOOK.md`](../../DEV-PLAYBOOK.md)):** one slice → prove (host tests +
compile) → ship as its own PR → tick the box here in the same change. Each sub-item below is sized to be
one short-lived branch. Behaviour-preserving refactors must keep the existing tests green (that's the
proof they didn't change behaviour); new seams get their logic host-tested where it was testable before.

---

## ✅ Already shipped (this arc) — the cheap, safe wins

- [x] **Wire-format parity tests** — `code/tests/test_wire_format_parity.py`: `pages.py` ↔ C++
  `AntBikePower.h` golden bytes (ANT), and SPA `OBC_ACTIONS` order ↔ firmware `sb20ActionOptions`.
  (decisions.md 2026-07-10; commit `d58426e`)
- [x] **SPA scale/offset "lying control"** fixed — `caps.scalarCorrection` gates the scalar inputs off on
  curve-only (ESP32) devices. (commit `d58426e`)
- [x] **OBC label drift** (`"−10W"` U+2212 → `"-10W"` ASCII) fixed + locked by the parity test.
- [x] **`forEachFormField` dedup** — the urlencoded-form loop was copy-pasted 5×; one iterator now.
  (commit `e84d189`)
- [x] **`/setup/save` merge fix** — was the only config writer that rebuilt fresh (wiped mode/curve);
  now merges via `mergeSetupForm`. (commit `e84d189`)

---

## R1 — nRF `main.cpp` seam extraction (the real liability)  🔴 highest value

**Problem (audit):** `firmware-nrf/src/main.cpp` is ~1495 lines with **zero seam classes** — it
re-implements *inline* the four seams the ESP32 already has as classes
(`BleCrankPeripheral`/`BleMeterClient`/`FtmsErgClient`/`BleShifterClient`), carries a ~350-line Bridge GATT
service, config persistence, and IMU recording all in one file, and **bypasses the shared `ProxyCore`**.
Every active nRF task (spoof, ANT) edits this file. Goal: convert it to the ESP32's proven shape — a thin
wiring file over seam classes in `firmware-nrf/src/`.

**Order is deliberate — shrink the surface with the low-risk lifts first, do the entangled radio seam
last.** Extracting R1a–R1c removes ~700 lines and is the precondition that makes R1d tractable.

- [x] **R1a — `BridgeConfigStore`** ✅ done (2026-07-10). New `firmware-nrf/src/BridgeConfigStore.h`
  owns the LittleFS I/O for all four blobs (config/curve/trainer/buttons) against the pure Proto.h/
  Correction.h codecs; main.cpp keeps the globals + the apply-policy (`applyCorrectionFromCfg`/
  `applyButtons`) and the `cfg/curve/trainer/buttons Save/Load` wrappers now one-line delegate to
  `bridgestore::` (no call-site changes). Behaviour-preserving (Serial breadcrumbs verbatim); main.cpp
  **1495 → 1418 lines**; both nRF envs compile, native **28/28**. On-hardware persistence round-trip is
  R-gated (LittleFS glue isn't host-testable), consistent with the other nRF seams.
- [~] **R1b — `BridgeService`** — **part 1 done (2026-07-11, commit on `r1b-bridge-service`).** New
  `firmware-nrf/src/BridgeService.h` owns the GATT *table*: the `53423230-XXXX` UUIDs, the 10
  characteristic objects, and the `begin()` char-setup block (order-faithful — each 128-bit char burns a
  vendor-UUID slot, see `configUuid128Count`). main.cpp holds one `g_bridge` and reaches chars via
  `g_bridge.chX`; main.cpp 1345→1283. Both nRF envs compile, native 28/28, on-hardware GATT round-trip
  (config/curve/buttons) verified byte-identical.
  **Part 2 (deferred): migrate the write-callback *bodies*** (`configWriteCb`/`curveWriteCb`/`calWriteCb`/
  `recCtlWriteCb`/`wkWriteCb`/`buttonsWriteCb` + the notify/publish helpers) into the class. On-hardware
  this session revealed the coupling is **wider than the audit's "bounded set of 5"**: the six callbacks
  touch **~30 globals** across config/correction/calibration/recording/erg/scanning/shifter and call
  Bluefruit's scanner/central directly (74 char-references total). A wholesale move needs an explicit
  `BridgeContext` (references + action hooks) so the coupling is explicit not implicit — and, critically,
  **3 of the 6 callbacks (cal/rec/wk) can only be *behaviour*-verified with a reference meter / IMU
  activity / an FTMS trainer connected.** So part 2 is best done with the bike-side hardware present.
  Part 1 deliberately reroutes only char *access* (no callback logic changed) so it's behaviour-identical
  by construction and fully bench-verifiable now.
- [ ] **R1c — absorbed by Track Launch LC2 `BikeRun`; do not implement separately.** The launch plan's
  behavior-preserving LC2 extraction is the execution vehicle for this same ~200-line region
  (`pumpDownload`, `imuSelfTest`, `recCtlWriteCb`, IMU globals, loop pacing), then LC3 deepens it with the
  one-run lifecycle. Buffer is already pure (`lib/bridge/ImuCapture.h`). **Prove:** compile; existing
  framing tests; `IMUTEST` serial self-test once on hardware.
- [ ] **R1d — `IRadioSource`/`IRadioSink` seam** (~560 lines: `scanCb`, `measNotifyCb`, the erg state
  machine, `centralConnect/Disconnect`, `cpWriteCb`, the advertising build). Split into
  `ble/BleCpsSource` + `ble/BleCpsPeripheral` (mirror `BleCrankPeripheral`) + `ble/FtmsErgClient` (mirror
  the ESP32 class name). **Biggest win + the roadmap's actual target, but riskiest** — `scanCb`/
  `centralConnectCb` multiplex four centrals (source/ref/trainer/SB20) by peer-name; pull the routing into
  a small dispatcher first. **Payoff:** the nRF can then adopt the shared `ProxyCore` and delete the
  hand-rolled read→correct→relay in `measNotifyCb`. **Do last.** Ties into the nRF roadmap P4 radio seam.
  - [x] **R1d.1 — the peer-name dispatcher** is done: `lib/bridge/PeerRole.h` is the single pure
    statement of the four-role ladder, `scanCb`/`centralConnectCb` are now thin adapters over it, and
    `test/test_peerrole/` (23 host tests) pins it. The extraction surfaced two latent routing bugs
    and one host/device toolchain gap; all three are fixed and recorded in `decisions.md`
    (2026-07-26). The scan-time and connect-time ladders are now one function, and a host test
    asserts they agree across the whole state space — a property that was not expressible before.
  - [x] **R1d.2 — the read→correct→re-frame relay is out**, as the pure `lib/bridge/SourceRelay.h`
    with all its carried-over crank state, plus `test/test_sourcerelay/` (18 host tests). The whole
    protocol path — CPS decode, cadence-from-crank-delta, the single-sided ×2, the correction, and
    both output framings (standard CPS / Stages 0x2F with its accumulated-torque integrator) — was
    inside a Bluefruit notify callback and therefore host-testable nowhere; it now runs with no
    radio. `measNotifyCb` is 10 lines of seam. The `native` env gained `lib_extra_dirs =
    ../firmware/lib` so nRF host tests can reach the shared pure headers at all.
    **The extraction caught a live regression in itself**: a first cut had `reset()` zero the
    accumulated-torque total, which the pre-extraction code deliberately did *not* do — that field
    is a free-running cumulative counter the consumer differences, so zeroing it mid-stream reads as
    a huge wrapping delta. Both behaviours are now pinned by tests. Verified on hardware end-to-end
    (`fake_meter.py` → XIAO → `crank_reader.py`): 200 W/90 rpm relayed in corrector mode, ×2 with
    `SINGLE1`, and the Stages 0x2F frame byte-checked in spoof mode with its torque accumulating at
    the predicted 679 units/rev. See `decisions.md` (2026-07-27).
  - [ ] **R1d.3 — the connection adapters** (`BleCpsSource` / `BleCpsPeripheral` / `FtmsErgClient`)
    remain: `scanCb`, `centralConnect/Disconnect`, the erg state machine and the advertising build
    are still inline. R1d.2 removed the protocol logic from that surface, so what is left is genuine
    Bluefruit wiring — lower value per line, and the part that cannot be host-tested without a fake
    Bluefruit. Do it only if it buys something concrete.
- [ ] **R1e — ESP32 `LcdController`** (~590 lines: `firmware/src/main.cpp:240–828` — the LCD head-unit
  controller: `buildLcdViews`, `lcdExecute`, `lcdTask`, the touch-cal ritual, `lcdSerialConsole`). The
  ESP32's *one* fat tenant; everything else there is legitimate wiring. Start with the zero-risk
  **touch-cal ritual** (`279–378`) as a standalone `disp/TouchCalRitual`. **Prove:** the LCD envs
  (`esp32s3-pio`, `esp32cyd`) compile; screens sweep on hardware (R-gated).
  - [x] **R1e.1 — the touch-cal ritual is out**, as the pure `lib/proxy/TouchCalRitual.h` rather than
    `disp/` (it needs no hardware at all, so it belongs with the other pure cores). `tick()` returns an
    action; `main.cpp` keeps only the film read, NVS, `Serial` and the raw→screen map. 10 host tests in
    `test/test_touchcal/` cover press averaging, dropout ride-through, blip rejection, fit rejection +
    restart, and the success-screen timing — none of which could be exercised without a screen before.
    `esp32cyd` compiles (RAM −8 B, flash +212 B); `pio test -e native` 262/262.
  - [ ] **R1e.2 — `buildLcdViews` / `lcdExecute` / `lcdTask`** — the remaining ~500 lines.

---

## R2 — Bridge GATT JS + Monkey-C parity harness  🟠 highest silent-drift blast radius

**Problem (audit #2):** the SPA's JS `parse*/pack*` (`web/index.html`) and the Garmin `BridgeBle.mc`
hand-code `DataView`/byte offsets for all 9 Bridge packet types with **no test against `Proto.h`** — a
single offset change silently corrupts the *primary user control surface* with zero CI signal
(`test_spa_sync` only checks the embedded copy is in sync, not that the bytes are right). The idiom to fix
it exists (`test_wire_format_parity.py`); the blocker is that the JS codec isn't executable in isolation.

**Resolved — and the answer went further than this doc's recommendation.** The owner-approved design is
[`design/ui-schema-design.md`](../../design/ui-schema-design.md) (U2): don't hand-extract the codec,
**generate** it from a single schema (`ui-schema/bridge.json`) and lock every mirror to committed golden
vectors.

- [x] **R2-decision** ✅ — chose (a)+(c) as recommended, implemented as full codegen:
  `code/scripts/gen_bridge.py` emits `ui-schema/bridge-golden.json` + `web/bridge-codec.js` +
  `firmware-nrf/test/.../bridge_golden_gen.h`. Node added to CI. (commit `6194d06`)
- [x] **R2a — the SPA runs the generated codec** ✅ (2026-07-24). `web/index.html` is now
  `<script type="module">` and `import * as BC from "./bridge-codec.js"`; its `parse*/pack*` are pure
  **name adapters** (wire shape → the view's normalized shape) with **zero byte offsets left**. Curve +
  Buttons moved over too. `web/gen_spa_header.py` inlines the module into `WebSpa.h` so a board still
  serves exactly one self-contained file. Verified: Node parity ✅, `gen_bridge --check` ✅,
  `test_spa_sync` ✅, SPA loaded over http in a real browser with an in-page codec round-trip ✅,
  `esp32c3-wifi` links (Flash 62.3%) ✅.
  **Caveat:** opening `web/index.html` straight off disk (`file://`) no longer works — a module import
  needs http. Use the Pages copy or the board's `/app`.
- [x] **R2b — shared golden vectors + Node parity test** ✅ — `bridge-parity` job in `tests.yml`
  (`gen_bridge.py --check`, `node web/test/bridge-codec.test.mjs`, `gen_webjson.py --check`); the C++ side
  is locked by `firmware-nrf` native (`bridge_golden_gen.h`). (commit `6194d06`)
- [ ] **R2c — Monkey-C** (`BridgeBle.mc`): still untouched — it remains the one Bridge mirror with no
  golden-vector lock (no CI build today; needs the Garmin SDK). At minimum, a doc note + the shared golden
  JSON as the reference; a `monkeyc` CI job is a stretch.

> **R2 was previously mis-stated in both directions** — this doc had all four boxes unticked while
> `ui-unification.md` marked U2 "done"; in fact R2b had shipped and R2a had not. The gap that mattered:
> CI was guarding `bridge-codec.js`, a file the shipping SPA never imported. That is now closed.

---

## R3 — `RuntimeConfig` `|`-line: version it  🟡 contained but a real trap

**Problem (audit #3):** the NVS line is 16 positional fields, **untagged (no version byte** — unlike the
nRF's `PROTO_VER`), append-only *by convention only*, and delimiter-injectable (mitigated solely by the
easily-forgotten `stripConfigDelims`). A middle insert or a missed strip corrupts every later field
silently. Contained (one production round-trip, `ConfigStore`) — so this is a maintenance-hazard fix, not
an active bug.

- [x] **R3a — add a leading version tag** ✅ done (2026-07-26). `toLine()` emits `v2|…`; `fromLine()`
  consumes a leading `v<digits>` field and otherwise parses the untagged legacy line as v1, count-based
  exactly as before. The discriminator is safe because slot 0 is `meterAddress` — empty, or a
  `':'`-separated BLE address, never `v2`. A tag *newer* than the build still parses positionally
  (append-only), so an OTA rollback keeps the rider's pairing instead of silently reverting to defaults.
  6 host tests, incl. field-by-field equivalence of the tagged and untagged encodings.
- [x] **the delimiter injection is closed at the serialiser** ✅ (same change, and the more valuable
  half). `stripConfigDelims` was applied by hand at six form-parsing sites, and `meterAddress` /
  `refMeterAddress` had no strip at all — one forgotten call silently shifted every later field. It now
  lives in `RuntimeConfig.h` beside the delimiter it protects, and `toLine()` applies it to every field
  itself, so no caller can forget. Mutation-verified: removing one strip fails
  `test_to_line_strips_delimiters_from_every_field_itself` and nothing else.
- [ ] **R3b (optional)** — a tiny `field(name)` accessor / named-offset map so future fields can't be
  read from the wrong slot; or leave positional but documented. Decide after R3a. *Deferred: with the
  version tag in place and the append-only rule now written into the header (and locked by a test),
  the remaining risk is a middle insert — which is a deliberate act, not an accident.*

---

## R4 — Config field-name vocabulary  🟢 friction, low risk

**Problem (audit #4):** the same concept is spelled three ways —
`meterNameFilter`/`srcFilter`/`src_filter`; `spoofName`/`outName`/`out_name`; mode as enum/bool/string —
forcing a translation layer (`renderConfigJson`/`getConfig`) and a mental map on every reader.

- [ ] **R4a — pick a canonical vocabulary** (recommend the SPA's normalized names as canonical, since
  they're the user-facing surface) and document the field matrix in one place (extend the table in
  decisions.md 2026-07-10 or `web/HTTP-API.md`).
- [ ] **R4b — align names where cheap** (rename within a single language where it doesn't cross a wire
  contract; the *wire* field names stay per-transport but the internal struct fields converge). Do not
  churn the wire formats. Low priority; do opportunistically alongside other work in each file.

---

## R5 — Spoof-target bytes inside the general codec  🟢 clarity, low risk

**Problem (5-model review, F3):** `firmware/lib/proxy/Cps.h` — the most-included pure header — mixed the
general Cycling Power Service codec with values captured from **one** spoof target. A reader could not
tell spec from observation, and the meter-to-meter corrector (which impersonates nothing) compiled the
Stages crank's bytes in regardless.

- [x] **R5a — split the captured Stages values into `lib/proxy/spoofs/StagesSpm2.h`** — `CPM_STAGES_FLAGS`,
  `CP_FEATURE_STAGES`, `SENSOR_LOCATION_OTHER`, `encodeStagesCpsMeasurement`. The generic flag *bits*
  stay in `Cps.h` (they are spec); only the captured *combination* moves. Dependency is one-way
  (`spoofs/` → `Cps.h`), so a second spoof target is a new file rather than more `Cps.h`.
  *done — PR #304.*
- [ ] **R5b — give `handleControlPoint` a per-target policy** so `encodeRequestCrankLengthResponse` can
  move too. It emits the Stages crank's non-standard `20 05 <len>` (no success byte) to *every*
  consumer, including head units in meter-to-meter mode that expect the standard `20 05 01 <len>`.
  Behavioural, so it needs head-unit validation — not a desk refactor. Tracked as its own issue.

---

## R6 — The Arduino macro workaround was an anonymous five-liner  🟢 clarity, low risk

**Problem (5-model review, F2 honourable mention):** the Adafruit nRF core defines `abs`/`round`/`min`/
`max`/`constrain` as macros that break the `std::` calls in the shared pure headers. The fix lived as
five bare `#undef`s in `firmware-nrf/src/main.cpp`. Trivial to re-copy into the next TU, and worthless
without the paragraph saying why the alternative fix (drop `std::` from the pure header) is wrong.

- [x] **R6a — promote to `firmware-nrf/src/arduino_compat.h`** with the rationale, plus a hermetic CI
  guard (`code/tests/test_arduino_compat_guard.py`) that fails if the `#undef`s reappear loose in any
  firmware source. Mutation-verified. *done — PR #304.*

---

## R7 — Desk-tooling duplication: one real fork, one false positive  🟢 clarity, low risk

**Problem (5-model review, item 10):** three "duplications" were named. Measuring each first mattered —
implementing the second as written would have cost a lossless-capture guarantee.

- [x] **R7a — `ant/pages.py` ↔ `01_capture_stages.py::decode_page` was a real 129-line fork.** An
  AST-normalised diff showed they differed *only in the docstring*, and `pages.py` had said for months
  that the capture script should import it. The script is now a thin wrapper (458 → ~340 lines) and
  imports the shared `PAGE_*` constants. **Equivalence proven over 49,050 real capture records:
  byte-identical.** Module surface unchanged — three siblings load it by `spec_from_file_location`.
- [x] **R7b — `06_capture_ble.py` ↔ `ble/cps.py` was NOT a duplication; the constants were.** The two
  decoders do different jobs: the capture side reads all 13 optional fields and is tolerant of
  truncation (a capture must never drop a record); the runtime side reads the four we ship and *raises*.
  Merging them would break the canonical-lossless-record invariant. What *was* duplicated is the
  protocol bytes — the script hardcoded the CPS flag bits and control-point op/result codes as bare hex.
  Those now come from the package; the functions stay separate, with the reasoning recorded in both so
  the finding isn't re-filed. Golden-vector test over 234 real frames, mutation-verified.
- [x] **R7c — `ride_wizard.py` copied `CaptureRunner._on_data` to observe traffic.** Two lines, but on
  the ride-day path where a silently dropped log line surfaces only after the ride. `CaptureRunner` now
  calls `_on_decoded(kind, decoded)` (no-op by default) after logging; the wizard overrides only that,
  so an observer can no longer cost you the capture.
- [x] **R7d — codegen scripts brought inside the lint gate.** `gen_bridge.py`, `gen_webjson.py`,
  `gen_spa_header.py`, `gen_tokens.py` produce committed wire-format artifacts but sat outside `ruff`.
  Now in CI's lint scope; generated output verified byte-identical after the fixes.
- [ ] **R7e — truncated optional fields are dropped with no marker in the record** (issue #306). The
  contract is now *pinned as it actually is* rather than as the docstring implied. Changing what the
  canonical capture writer records is an owner decision, not an unsupervised one.

## R8 — The front door described a product that no longer existed  🟠 highest newcomer cost

All five reviews flagged this independently, which is itself the signal: `README.md` said *"Phase 0
substantially complete; **proxy not yet built**"* and described the deliverable as "a small Python
application (Raspberry Pi or laptop with ANT+ USB sticks)" — while two firmware targets shipped, rode,
and had a beta programme. Its repository layout listed neither `firmware/`, `firmware-nrf/`, `web/`,
`sessions/`, `beta/`, `tools/` nor `PROJECT-MAP.md`. Its "Where to start" table sent every newcomer to
`START-HERE.md` and `HANDOFF.md` — the *pre-pivot brief* — as the two top entries.

The cost is asymmetric: an out-of-date internal note wastes minutes, but a front door pointing at a
superseded architecture costs a whole orientation, and it is exactly the failure `PROJECT-MAP.md` was
created to stop.

- [x] **R8a — `README.md` rewritten as a truthful front door.** Leads with the actual product (an
  on-bike dual-role BLE device), states real status, and **defers to `PROJECT-MAP.md` rather than
  duplicating it** — a second inventory would just become a second thing to drift.
- [x] **R8b — staleness banners on the 14 historical root docs that had none**, including
  `START-HERE.md`. The docs **stay at the root on purpose**: append-only `decisions.md` links them, so
  moving them would break the historical record. *(The original plan said "move legacy cards out of
  root"; measuring inbound links showed every one is referenced — banner in place instead.)*
- [x] **R8c — the `sessions/PLAYBOOK.md` pre-flight section still encoded the refuted Npcap/tshark
  sniffer path** that cost session 9 hours, contradicting its own §Passive BLE sniffing 200 lines below
  and `doctor.ps1`'s actual implementation. Ground truth taken from the script, not either passage.
  `doctor.ps1`'s own header comment disagreed with its code nine lines further down; fixed too.
- [x] **R8d — `BOARDS.md` live addresses refreshed** (the LAN had moved off the recorded `192.168.0.x`
  subnet) and the recorded **mDNS hostname collision confirmed FIXED** — each board now answers its own
  name. Also records the `mklink /J` fix for the LVGL `MAX_PATH` build failure on Windows.
- [x] **R8e — a CI guard so this class of drift can't merge again:** `test_doc_links.py` fails on any
  dead relative Markdown link repo-wide (583 links checked; found and fixed 3 real dead ones in
  `session-04`). Mutation-verified. It joins `test_project_map.py` and `test_findings_index.py` — the
  map is complete, the index is complete, and now every link between them resolves.

---

- The three config **serializers / storage backends** (NVS line vs GATT binary vs HTTP JSON) — justified
  by transport constraints; a single wire format would be worse on ≥2 of them.
- The scalar-vs-curve **correction model** difference — a product decision (nRF exposes scalar knobs, the
  ESP32 doesn't), already unified in the shared `Correction::apply` math.
- Collapsing the two config **UIs** (`/setup` page vs the SPA) — gated on the SPA-over-HTTP path being
  hardware-verified first (it's "unverified until U4"); retiring `/setup` before the replacement is proven
  would be the wrong order.

---

*Maintenance: tick a box in the same PR that lands the slice. When an item completes, add a one-line
"done — commit `<sha>`" next to it. Promote any durable finding to `decisions.md`. If we later prefer
GitHub issues, mirror the unchecked R-items there and link back here.*
