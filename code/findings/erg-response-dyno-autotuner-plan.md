# Erg-response Dyno and optional Autotuner — phased research and implementation plan

**Status: OWNER-APPROVED PLANNING BASELINE (2026-07-20). IMPLEMENTATION PAUSED.**

This is the delivery plan for
[`erg-response-dyno-autotuner-spec.md`](erg-response-dyno-autotuner-spec.md). It is intentionally
separate from the research foundation and normative product specification. Every slice is a fresh
short-lived branch/PR from current `origin/main`, with tests and documentation in the same change.
The owner approved this plan and the specification on 2026-07-20. No slice may start until a later
explicit implementation instruction.

---

## 1. Program gates

```text
Research + approved spec/plan
        |
        v
Known software plant + identifier proof
        |
        v
FTMS simulator protocol/recovery proof
        |
        v
SB20 round-trip + commissioning capture
        |
        v
Tacx NEO round-trip + commissioning capture
        |
        v
Two-day Dyno baselines (SB20 + NEO)
        |
        +----------> Dyno product complete
        |
        v
Per-profile eligibility decision
        |
        v
Replay/simulation compensation bake-off
        |
        v
Conservative opt-in hardware proof
        |
        v
Optional sprint-envelope expansion
```

Dyno completion does not imply Autotuner eligibility.

---

## 2. Delivery rules

- One slice = one branch = one PR = one coherent capability.
- Sync with `origin/main` and survey open work before every slice and merge.
- Extend existing codecs, workout/runtime, UI, capture, and twin seams; do not rebuild them.
- Every pure module ships with host tests in the same PR.
- Python/C++ parity uses canonical golden vectors where logic runs in both languages.
- Real capture bytes replace spec vectors where available; immutable captures are never edited.
- Hardware sits behind injected interfaces; only radio/on-bike behavior remains manual.
- Every physical session follows `sessions/PLAYBOOK.md`, updates `sessions/README.md`, records actuals
  in the session doc, promotes findings to canonical docs, and improves the playbook.
- Do not draft the next physical session until the current one is complete.
- No performance claim before G3 and no improvement claim before G6.

---

## 3. Phase D0 — contracts and known-plant proof

### D0.1 — Run bundle and normalized event contract

**Goal:** establish the stable data vocabulary before producers and consumers multiply.

**Deliverables**

- Versioned schemas/models for trainer identity/capabilities, commands, acknowledgements, trainer
  observations, reference observations, operator events, and faults.
- Run manifest/protocol/event/summary layout from the specification.
- Canonical JSON serialization and fixtures in Python.
- C++ value types only for fields required on-device; avoid duplicating desk-only analysis types.
- Explicit absent-versus-zero semantics and raw-byte provenance.

**Reuse**

- `code/src/sb20proxy/ble/ftms.py`
- existing capture JSONL conventions and SQLite analysis layer
- UI schema/codegen patterns where appropriate

**Tests/gate**

- Round-trip/schema validation.
- Unknown/additive field compatibility.
- Interrupted/incomplete run representation.
- Findings index and documentation tests.

### D0.2 — Versioned `DynoProtocol` compiler

**Goal:** compile human-readable suite definitions into one deterministic execution format.

**Deliverables**

- Pure protocol model: phases, target expressions, cadence preconditions, duration, repeats,
  recovery, suite prerequisites, semantic version, content hash.
- Validation for missing recovery, consecutive >100% blocks, cap violations, impossible cadence
  prerequisites, and non-deterministic pseudo-random definitions.
- Initial protocol fixtures:
  - low-power simulator commissioning;
  - Quick Diagnostic;
  - dense ERG step grid;
  - sparse gear subset;
  - ramp triangle; and
  - fixed pseudo-random sequence.

**Tests/gate**

- Stable content hashes and Python/C++ golden-vector parity for the executable subset.
- Same injected time produces identical state transitions.
- Invalid protocols fail explicitly.

### D0.3 — Parameterized trainer plant

**Goal:** replace the ideal instantaneous twin with a known test plant without disturbing existing
tests that depend on ideal behavior.

**Deliverables**

- Pure configurable plant supporting delay, first/second-order lag, saturation, quantization, noise,
  cadence-linked disturbance, and optional overshoot.
- Adapter into `InProcessFtmsServer` and `TrainerTwin`, with ideal defaults retained where needed.
- Deterministic seeded disturbance fixtures.

**Tests/gate**

- Exact behavior at zero delay/lag.
- Known step/ramp responses.
- Saturation and cadence disturbance.
- No radio/network dependency.

### D0.4 — Identifier and canonical metrics

**Goal:** prove analysis on known plants before any real trainer.

**Deliverables**

- Pure metric extraction.
- Constrained FOPDT and second-order-plus-dead-time fits.
- Held-out validation, uncertainty, residual checks, and model-inadequate verdict.
- Settling, oscillation, cadence validity, repeatability, and release metrics.
- Python reference implementation and portable golden result fixtures for on-device summary parity.

**Tests/gate**

- Recover injected parameters within predeclared tolerances.
- Reject deliberately mismatched plants.
- Exact settling definition and latency/dead-time separation.
- Repeatability/CI behavior on seeded fixtures.

**Gate G0a:** known-plant identifier proof complete.

---

## 4. Phase D1 — trainer adapter and capture safety

### D1.1 — Pure trainer-adapter state machine

**Goal:** separate generic Dyno execution from FTMS/FE-C and trainer quirks.

**Deliverables**

- Transport-neutral `TrainerAdapter` interface.
- Explicit control/ack/recovery/release states and timeouts.
- Separate transport ACK and protocol-result events.
- Fault-injection fake adapter.
- Exact-unit identity and capability fingerprint builder.

**Tests/gate**

- Request/Start/target/recovery/END sequences.
- Refusal, timeout, permission loss, disconnect, and failed release.
- No brand checks in Dyno core.

### D1.2 — FTMS adapter over existing codecs

**Goal:** wrap existing Python and firmware clients rather than fork a new command path.

**Deliverables**

- Python `FtmsTrainerAdapter` around `ftms.py`/`ftms_erg.py`.
- Firmware twin around `Ftms.h`/`FtmsErgClient`.
- Capability discovery and range clamping.
- Capture-configured acknowledgement policy.
- Explicit recovery and Stop/Reset release.
- Preserve existing behavior outside Dyno through flags/adapters.

**Tests/gate**

- Existing FTMS tests remain green.
- Python/C++ command/state parity.
- Optimistic write-ACK behavior is explicit policy, not default truth.
- Missing optional characteristics remain supported.

### D1.3 — Assioma reference source

**Goal:** supply mandatory independent power/cadence on the CYD run clock.

**Deliverables**

- Reuse existing CPS/meter central and dual-meter plumbing behind `ReferenceSource`.
- Exact meter identity and calibration-state metadata.
- Telemetry-loss events and cadence validity feed.

**Tests/gate**

- Fake CPS source end-to-end with Dyno runner.
- Missing cadence/power and reconnect behavior.
- No duplicate meter parser.

### D1.4 — Append-only SD run-bundle sink

**Goal:** make every simulator/hardware run durable before UI work.

**Deliverables**

- On-device append-only writer and atomic manifest/finalization.
- Recovery/finalization of interrupted runs as incomplete.
- Capacity warnings and explicit deletion API.
- Python bundle reader/rebuilder.

**Tests/gate**

- Power-loss/interruption fixtures.
- Low-space/write-error handling.
- Raw bundles never auto-pruned.
- Python reconstructs summaries from canonical events.

---

## 5. Phase D2 — deterministic runner and simulator proof

### D2.1 — Pure `DynoRunner`

**Goal:** execute a compiled protocol on injected time, adapter, reference source, and sink.

**Deliverables**

- Full run state machine.
- Cadence stabilization (±5 rpm for 10 s), invalid-window labeling, repeats, recovery scheduling,
  STOP, END, and telemetry-loss recovery.
- Five-minute warm-up and rider-profile cap resolution.

**Tests/gate**

- Full Quick Diagnostic and advanced fixture traces.
- No consecutive >100% blocks and required recovery.
- STOP→40% recovery and END→release.
- All faults produce explicit terminal/incomplete states.

### D2.2 — In-process protocol proof

**Goal:** run every suite against known software plants.

**Deliverables**

- End-to-end command/reference/capture/report tests.
- Known good, delayed, overshooting, oscillating, nonlinear, and inadequate-model fixtures.
- Simulator-only compensation harness interface, with no algorithm yet.

**Tests/gate**

- Identifier recovers known plants.
- Reports and plot datasets are deterministic.
- Every fault path is exercised without hardware.

### D2.3 — On-air FTMS simulator proof

**Goal:** prove the real BLE/CYD command and release path before a trainer.

**Deliverables**

- Extend existing `FtmsTrainerServer`/hardware loop to telemetry, recovery, Stop/Reset, and release.
- Capture the complete low-power commissioning protocol.
- Verify command/result classification against real radio timing.

**Tests/gate**

- Target compile before flash.
- On-air raw JSONL committed where appropriate.
- CYD STOP and telemetry-loss recovery demonstrated.

**Gate G0:** simulator protocol, capture, recovery, and release proof complete.

---

## 6. Phase D3 — CYD and PWA product surfaces

### D3.1 — Shared Dyno view-model and actions

**Goal:** add product state without creating parallel UI logic.

**Deliverables**

- `DynoView` in shared UI model.
- Typed actions for wizard navigation, STOP, END, export, and delete.
- Status fields for trainer/reference identity, protocol hash, cadence precondition, RAW/TUNED, target,
  outgoing command, faults, storage, and provisional metrics.

**Tests/gate**

- Pure view-model tests.
- Existing UI surfaces unchanged.

### D3.2 — CYD LVGL wizard

**Goal:** ship the rider-facing guided workflow on the existing production renderer.

**Deliverables**

- LVGL preflight, warm-up, run, recovery, END/release, and summary screens.
- Always-visible STOP.
- Cadence coaching and live compact traces.
- Host native-LVGL interaction tests.

**Tests/gate**

- `pio test -e native-lvgl`.
- Touch parity/actions.
- No direct-blit UI exception.

### D3.3 — Local PWA and bundle API

**Goal:** detailed local analysis and export with no cloud dependency.

**Deliverables**

- Bundle list/download/delete endpoints.
- Five canonical plots.
- Firmware/trainer comparison with per-metric uncertainty and practical thresholds.
- Evidence-labeled diagnostic signatures.

**Tests/gate**

- Schema-generated/parity-tested wire fields where the existing UI contract requires it.
- Offline/local rendering.
- Exact reconstruction from fixture bundle.

### D3.4 — CYD coexistence benchmark

**Goal:** prove trainer FTMS + Assioma BLE + Wi-Fi/PWA + LVGL + SD capture fit the device budget.

**Deliverables**

- `/stats`/perf capture under worst representative run.
- Measured loop/radio/storage/display impact.
- Optimization only where measurements identify a bottleneck.

**Gate**

- No command/reference starvation or watchdog regression under the accepted budget.

---

## 7. Phase D4 — physical trainer gates

Physical work begins only after D0–D3 are green and gets one session doc at a time.

### D4.1 — SB20 round-trip and release session

**Goal:** resolve the standing FTMS gate before dynamic excitation.

**Run**

- exact identity/capability capture;
- passive trainer + Assioma telemetry;
- one conservative target;
- protocol acknowledgement versus transport ACK;
- 40% recovery;
- Stop/Reset release and observed result; and
- controller-disconnect behavior if the run sheet explicitly approves it.

**Outputs**

- immutable capture;
- session actuals/retro;
- updated `ftms-protocol.md`, decisions, and capture index;
- explicit SB20 adapter policy.

**Gate G1-SB20:** round-trip and release behavior captured.

### D4.2 — SB20 Quick Diagnostic commissioning

**Goal:** prove the complete guided flow before advanced workloads.

**Gate**

- valid bundle/report;
- three steps captured;
- cadence and STOP/END behavior correct;
- coexistence remains acceptable.

### D4.3 — Tacx NEO round-trip and release session

**Goal:** repeat D4.1 without Dyno-core brand changes.

**Outputs/gate**

- exact NEO capture and adapter policy;
- generic core reused unchanged;
- any quirk isolated behind the adapter and grounded in capture.

**Gate G1-NEO:** round-trip and release behavior captured.

### D4.4 — Tacx NEO Quick Diagnostic commissioning

Same product flow and acceptance as SB20, with exact NEO identity/capabilities.

**First-release transport gate:** SB20 + NEO commissioning complete.

---

## 8. Phase D5 — accepted Dyno baselines

### D5.1 — Advanced protocol readiness review

Before the advanced grid:

- review commissioning traces and rider workflow;
- lock protocol version/hash;
- confirm FTP/caps and session duration;
- pre-render the run plan and recovery schedule;
- split into practical physical sessions if required without changing condition definitions; and
- ensure the same protocol partition is used on both baseline days.

### D5.2 — SB20 day-1/day-2 baseline

Run, in evidence-gated order:

1. dense mid-gear step grid;
2. sparse low/high gear subset;
3. ramp triangles; and
4. fixed pseudo-random suite.

A later suite runs only if the previous suite is clean. Day 1 is provisional; day 2 creates the
accepted baseline.

### D5.3 — Tacx NEO day-1/day-2 baseline

Repeat the same protocol semantics and report native gear/identity fields. Do not force unavailable
SB20 characteristics onto NEO.

### D5.4 — Baseline report and Dyno release gate

**Deliverables**

- complete per-trainer metric vectors and uncertainty;
- five canonical plots;
- within-run and cross-day repeatability;
- model adequate/inadequate verdict;
- cross-trainer comparison limited to equivalent evidence;
- no composite score.

**Gate G2:** Assioma-grounded topology confirmed.

**Gate G3:** repeatable accepted SB20 and NEO baselines.

**Dyno first-release completion:** G3 for both initial trainers.

---

## 9. Phase D6 — post-ERG control modes

These are in the program but begin only after ERG Dyno ships.

### D6.1 — Resistance-mode research and owner protocol interview

- capture real command semantics and rider-power mapping;
- do not use advertised resistance percentage as effort;
- design a rider/trainer/cadence usable-range commissioning method;
- produce a separate approved protocol revision before implementation.

### D6.2 — Simulation/grade research and owner protocol interview

- capture simulation parameter behavior;
- define appropriate response variables and metrics;
- keep results separate from ERG/resistance.

### D6.3 — FE-C adapter, only when required

Implement the transport-neutral adapter over existing ANT+ code when a target trainer/control mode
requires it. Do not build it merely for symmetry.

---

## 10. Phase A0 — Autotuner eligibility and replay

No A-phase starts for a trainer until its G3 baseline exists.

### A0.1 — Eligibility evaluator

**Deliverables**

- exact profile identity/invalidation;
- two-day repeatability checks;
- held-out NRMSE ≤10%;
- key estimate agreement within 20%;
- validated operating envelope;
- human-readable pass/fail evidence.

**Gate G4:** owner explicitly admits the exact trainer profile to compensation experiments.

### A0.2 — Compensation interface and candidate bake-off

**Goal:** compare bounded algorithms without choosing one in advance.

**Candidates**

May include transition scheduling, feed-forward/pre-emphasis, lead/lag, inverse-model, or other
bounded approaches justified by data. PID is neither required nor excluded.

**Hard constraints**

- prescription unchanged;
- command deviation ≤`min(15% FTP, 50 W)`;
- lead ≤`min(measured dead time, 3 s)`;
- no manual-target prediction;
- no extrapolation;
- saturation/state handling;
- RAW on all compensation faults;
- one oscillation backoff, then RAW latch.

**Tests/gate**

- replay accepted raw bundles;
- known-plant simulation;
- cadence-disturbance injection;
- all fault and bypass paths;
- candidate cannot pass by selectively dropping difficult intervals.

**Gate G5:** replay/simulation improvement without authority, stability, or disturbance regressions.

### A0.3 — Immutable profile artifact

**Deliverables**

- versioned profile schema;
- complete provenance to accepted run bundles;
- archive/disable on known identity change;
- unknown-firmware identity handling agreed in the spec;
- signed/hash-checked local artifact as appropriate to existing config patterns.

---

## 11. Phase A1 — optional on-device compensation

### A1.1 — Pure `PlantCompensator`

- injected time and immutable profile;
- RAW/TUNED/TUNED_REDUCED/RAW_LATCHED state machine;
- scheduled pre-emption only;
- bounded slew/authority/envelope;
- deterministic golden vectors shared with Python.

### A1.2 — Workout-runtime insertion

Insert one stage:

```text
prescribed target -> PlantCompensator -> existing FTMS client
```

Do not move or duplicate the workout clock. Manual target changes remain raw/non-predictive.

### A1.3 — CYD/PWA controls

- explicit per-session opt-in;
- continuously visible RAW/TUNED and outgoing command;
- one-tap bypass;
- archived/stale profile UX;
- no automatic re-enable after fault or reconnect.

### A1.4 — Same-rig hardware proof

Run RAW and TUNED against the same exact protocol/profile on both accepted baseline days.

**Gate G6 requires all:**

- integrated absolute tracking error ≥15% better;
- settling time ≥10% better;
- overshoot regression ≤5 W;
- oscillation/cadence-linked regression ≤10%;
- bypass/fault/release behavior proven.

Failure leaves the profile Dyno-only.

---

## 12. Phase A2 — sprint pressure-test and envelope expansion

This phase begins only after ordinary Autotuner evidence exists; it is not required for initial Dyno
completion.

### A2.1 — Sprint launch research

- use baseline plant models to design candidate rolling starts and command ramps;
- test candidates on software plants;
- run submaximal rider sessions before near-maximal targets;
- preserve the 5 s/400%, 10 s/300%, 15 s/250% goal matrix;
- use rider absolute ceilings and at least three minutes recovery.

The exact launch is a capture-derived decision, not a constant in an early PR.

### A2.2 — RAW sprint baseline

- three repeats of each accepted launch/effort on two days;
- capture cadence freely during the sprint after the launch precondition;
- assess whether existing model families remain adequate.

### A2.3 — Sprint compensation eligibility

Sprint regions independently repeat G4/G5/G6. Until then, the ordinary profile automatically uses
RAW there.

---

## 13. Phase F — future trainer expansion

### F1 — Wahoo KICKR capture

- exact generation/firmware identity;
- attempt standard BLE FTMS capability/round-trip first;
- use ANT+ FE-C only if capture shows no usable FTMS target-power path;
- add no KICKR branch to Dyno core.

### F2 — additional trainers

Each trainer earns support through:

1. identity/capability/passive telemetry capture;
2. conservative round-trip/release capture;
3. commissioning;
4. optional accepted baseline; and
5. separately earned Autotuner eligibility.

---

## 14. Validation matrix

| Surface | Minimum validation per relevant PR |
|---|---|
| Python pure core | targeted `pytest`, then affected suite; `ruff check src tests` |
| Firmware pure core | `pio test -e native` |
| LVGL UI | `pio test -e native-lvgl` |
| Python/C++ parity | golden-vector pytest invoking native fixtures/builds |
| CYD firmware | `pio run -e esp32cyd-live` or the exact affected env before flash |
| Web/PWA contract | existing schema/codegen checks and fixture rendering |
| Documentation/index | `tests/test_findings_index.py` and relevant doc invariants |
| Hardware | approved session run sheet, immutable capture, actuals, retro, promoted findings |

Start targeted; escalate only when the affected seam requires it. Hardware never substitutes for
host tests, and host tests never substitute for real trainer proof.

---

## 15. Program completion definitions

### Dyno v1 complete

- approved spec/protocol contract;
- known-plant and on-air simulator gates;
- standalone CYD + Assioma workflow;
- immutable SD bundles and local PWA;
- SB20 and Tacx NEO commissioning;
- two-day accepted ERG baselines for both;
- canonical metrics/plots/comparisons;
- resistance/simulation architecture preserved but protocols deferred.

### Autotuner complete for one profile

- exact profile passes G4;
- selected algorithm passes G5;
- on-device opt-in/bypass/fault behavior passes;
- same-rig two-day before/after passes every G6 threshold;
- claim is limited to the validated profile envelope.

### Sprint support complete for one profile

- evidence-designed launch;
- two-day RAW sprint baseline;
- sprint model/eligibility;
- sprint-specific G5/G6;
- validated sprint envelope recorded explicitly.

---

## 16. Approval gate

Approval establishes the planning baseline; it does not start implementation. A later explicit
instruction may authorize only the first unblocked PR slice, not the whole program as one change.
Every later phase remains dependent on the preceding measured gate.
