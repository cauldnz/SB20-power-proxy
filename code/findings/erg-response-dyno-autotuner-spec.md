# Erg-response Dyno and optional Autotuner — product and technical specification

**Status: OWNER-APPROVED PLANNING BASELINE (2026-07-20). IMPLEMENTATION PAUSED.**

This specification is the owner-approved planning baseline produced after primary-source research
and a one-question-at-a-time owner interview. Approval was recorded on 2026-07-20. Implementation
remains paused until a later explicit instruction and must follow the separate
[`erg-response-dyno-autotuner-plan.md`](erg-response-dyno-autotuner-plan.md).

Research basis:
[`erg-response-dyno-autotuner.md`](erg-response-dyno-autotuner.md).

---

## 1. Product contract

The program provides a repeatable way to characterize how an indoor trainer responds to control
commands, then optionally compensate a measured plant without changing the workout prescription.

### Stage 1 — Dyno (mandatory)

The CYD touch head unit runs versioned trainer-control protocols while simultaneously reading an
Assioma reference meter over BLE. It captures command, acknowledgement, trainer telemetry, reference
power, cadence, identity, and operator events on one monotonic clock. Post-run analysis reports:

- command-to-protocol-acknowledgement latency;
- physical response dead time;
- rise and fall time;
- overshoot and undershoot;
- settling time;
- steady-state tracking error;
- oscillation/hunting;
- cadence and gearing sensitivity;
- release behavior; and
- within-run and between-day repeatability.

Dyno is useful on its own for comparing trainer firmware, comparing trainers under equivalent
conditions, and reporting evidence-labeled signatures behind “ERG feels bad.”

### Stage 2 — Autotuner (optional and separately earned)

An exact trainer/profile tuple may become eligible for bounded plant compensation only after Dyno
shows repeatable behavior and a predictive model. Compensation may shape transitions, pre-empt a
known scheduled transition, and reduce authority when oscillation appears. It must never change the
workout shown or prescribed to the rider.

### Initial and future targets

| Target | Program role |
|---|---|
| Stages SB20 | First real-trainer validation after simulator proof |
| Garmin Tacx NEO | Second initial trainer; proves the design is trainer-agnostic |
| Wahoo KICKR | Later capture-gated compatibility target; FTMS first, ANT+ FE-C fallback |

The first release is not complete until both SB20 and Tacx NEO have passed their applicable Dyno
gates. KICKR availability does not block the first release.

### Control-mode scope

The shared architecture includes:

1. absolute target power (ERG);
2. resistance; and
3. simulation/grade.

They are separate protocol families with separate measurements and acceptance gates. ERG ships
first. Concrete resistance and simulation rider protocols are deliberately deferred until ERG Dyno
ships; advertised resistance percentage is not treated as a rider-effort scale.

---

## 2. Non-goals and hard boundaries

- Dyno is not the existing static A/B meter comparison. It may reuse the Assioma and capture
  infrastructure, but not `MeterCompare` analysis.
- Autotuner is not an adaptive workout. The prescribed target, workout clock, interval labels, and
  rider-facing workout remain unchanged.
- No trainer, firmware, control mode, acknowledgement policy, or release policy is supported without
  a capture from that exact path.
- No trainer dynamics or controller algorithm is selected before measured data.
- No single Dyno score or overall trainer/firmware winner is produced.
- No raw capture is silently edited, replaced, or auto-deleted.
- No compensation profile is portable across physical trainer units.
- No compensation extrapolates outside its validated power/cadence/gear envelope.
- No online controller learning occurs during workouts.
- Resistance and simulation results are never pooled with ERG results.

---

## 3. Product principles

1. **Measure before compensate.** Dyno ends at a useful characterization even if Autotuner is never
   eligible.
2. **Exact identity.** A result and profile state what physical unit, firmware/capabilities,
   transport, control mode, protocol, rider profile, and reference meter produced it.
3. **Reference power is mandatory.** Every Dyno run requires the Assioma; trainer-only runs are not a
   Dyno run.
4. **Cadence is observed, coached, and qualified.** It is not assumed constant.
5. **Passive-machine risk model.** The trainer applies resistance but does not drive the rider. The
   rider can always stop pedalling. Controls therefore prioritize comfort, clean evidence, and
   predictable release rather than autonomous-machine hazard machinery.
6. **Raw evidence first.** Canonical captures are immutable, lossless, local, and exportable.
7. **Visible authority.** RAW/TUNED, target, compensated command, profile identity, and faults are
   never hidden.
8. **Fail out of compensation.** Every Autotuner fault selects RAW prescription pass-through.
9. **No claim before evidence.** Baseline claims require G3; improvement claims require G6.

---

## 4. Terms and identities

### Trainer plant

The resistance device and its internal controller, observed from a target command to delivered
reference power. The plant includes device-side filtering and actuation; transport latency is
measured separately.

### Run identity

Every run records:

```text
trainer physical identity
trainer manufacturer/model
firmware/software revisions, when exposed
capability fingerprint and declared ranges
transport and control mode
Dyno protocol semantic version and content hash
CYD firmware/build identity
reference-meter identity/firmware
rider-profile identity, FTP, and absolute caps
gear/flywheel condition
timestamps and calibration/warm-up state
```

If trainer firmware/software is unavailable, exact serial/device identity plus capability
fingerprint may key an Autotuner profile. The run must explicitly record that firmware is unknown;
this accepts the residual risk that an unreported update may not change capabilities.

### Autotuner profile identity

A profile is bound to:

```text
exact trainer unit
available firmware/software revisions
capability fingerprint
transport
control mode
Dyno protocol version and hash
validated power/cadence/gear envelope
profile algorithm/version
```

Any known firmware/software or capability change disables and archives the profile and requires a
fresh baseline. Archived profiles remain read-only for comparison.

---

## 5. Rider-facing workflows

### 5.1 Guided baseline wizard

The CYD provides a versioned wizard:

1. select/discover trainer and Assioma;
2. display and confirm exact identities and capabilities;
3. prove live trainer, reference-power, and cadence telemetry;
4. choose the applicable standardized suite;
5. load the local rider profile and confirm FTP, absolute ceiling, and 40% recovery target;
6. complete a five-minute standardized progressive warm-up ending at recovery target;
7. coach cadence and wait for each transition precondition;
8. run the protocol, showing live target/reference/trainer traces and status;
9. STOP to abort excitation into recovery while retaining control;
10. END to run the captured Stop/Reset release, verify the observed result, and close the bundle;
11. show a compact provisional summary; and
12. expose the complete run bundle and canonical post-run report through the local PWA.

Baseline protocol parameters cannot be edited ad hoc. Every protocol has a semantic version and
content hash. Only identical hashes are pooled directly.

### 5.2 Quick Diagnostic

Quick Diagnostic is a versioned, provisional “ERG feels bad” workflow:

- the same five-minute warm-up;
- mid gear;
- 80 rpm target;
- three 30-second 40%↔100% FTP step pairs;
- recovery; and
- END/release.

It uses the normal metric pipeline but cannot qualify or update an Autotuner profile.

### 5.3 STOP, recovery, END

- The always-visible one-tap **STOP** immediately ends protocol excitation, commands the configured
  recovery target (default 40% FTP, rider-adjustable and absolutely capped), and retains trainer
  control.
- Optional trainer-specific shortcuts may invoke STOP, but the CYD control is mandatory.
- **END** is separate: it issues the adapter’s capture-validated Stop/Reset release, observes the
  result, and closes the capture bundle.
- If required telemetry disappears during an active target, command recovery immediately. If the
  recovery command is not protocol-acknowledged within the adapter’s bounded timeout, issue
  Stop/Reset and show a blocking “verify resistance” warning.
- The rider stopping pedalling remains the physical fallback.

---

## 6. Standard ERG protocols

Every condition uses the rider-confirmed FTP and independent absolute ceiling. A command is the
lower of its FTP-derived target, rider ceiling, and trainer-declared range.

### 6.1 Low-power commissioning

Before an exact trainer/firmware may run advanced protocols:

- prove capability discovery and live telemetry;
- capture Request Control, Start where required, one conservative target, acknowledgement, physical
  response, recovery, Stop/Reset, and observed release;
- prove CYD STOP and telemetry-loss recovery; and
- prove the same protocol against the known software plant and on-air FTMS simulator first.

Commissioning is intentionally lower than the advanced 25–200% grid. Its exact conservative targets
are selected in the physical-session run sheet after simulator proof; they do not create a reusable
plant claim.

### 6.2 Accepted step baseline

#### Mid-gear dense grid

- Power: **25%, 50%, 75%, 100%, 150%, and 200% FTP**
- Cadence: **60, 80, 100, and 120 rpm**
- Three identical transitions per condition
- 30-second high-intensity efforts
- No consecutive efforts above 100% FTP
- At least 60 seconds at 40% FTP after every effort above 100%

Cadence must remain within ±5 rpm for 10 continuous seconds before a transition. Brief excursions
remain captured. Intervals outside the declared validity rule are excluded from the primary fit but
retained for disturbance analysis.

#### Low/high gear sparse subset

At low and high gearing:

- Power: **50%, 100%, and 150% FTP**
- Cadence: **80 and 100 rpm**
- Three repeats

SB20 virtual gear and NEO physical gearing are retained in native form. Reports use gear/flywheel
condition, not a false shared gear number.

### 6.3 Ramp suite

After step stability is proven:

- continuous **25%→200%→25% FTP** triangle;
- 60 seconds each direction (120 seconds total);
- at least 120 seconds at 40% FTP recovery; and
- three repeats.

### 6.4 Pseudo-random multilevel suite

After step and ramp suites are clean:

- fixed and versioned four-minute sequence;
- target levels 50%, 100%, and 150% FTP;
- dwell choices 5, 10, and 15 seconds;
- no consecutive 150% blocks; and
- repeat the identical sequence twice.

The sequence is deterministic across trainer/firmware comparisons; it is not regenerated per run.

### 6.5 Between-day baseline

A single complete run is provisional. An accepted comparable baseline requires two complete valid
runs on different days, each with three within-run repeats and the same protocol hash.

### 6.6 Sprint pressure-test and envelope expansion

Hard short sprints are a target use case, not an extrapolation.

The post-baseline sprint goal matrix is:

- 5 seconds at 400% FTP;
- 10 seconds at 300% FTP;
- 15 seconds at 250% FTP;
- three repeats each;
- every target capped by the rider’s absolute ceiling; and
- at least three minutes recovery between efforts.

The exact rolling cadence and command-ramp/launch shape are deliberately **not fixed yet**. A true
track-cyclist sprint may put 400% FTP near maximal power, so an abrupt command from a generic cadence
would not be representative. After ordinary baselines exist, candidate launches are designed and
tested in simulation and submaximal rides before the sprint matrix is attempted.

Sprint support is its own evidence-gated profile-envelope expansion. The ordinary profile remains
RAW in sprint regions until the sprint region independently passes repeatability, model, replay,
hardware, and acceptance gates.

---

## 7. Transport-neutral architecture

### 7.1 Module boundaries

```text
Versioned Dyno protocol ──> DynoRunner ──> TrainerAdapter ──> trainer
                                │                 │
AssiomaReferenceSource ─────────┤                 └─ capability/ack/release policy
                                │
                           RunBundleSink
                                │
                   PlantIdentifier / DynoReport

Workout prescription ──> optional PlantCompensator ──> TrainerAdapter
           │                       │
           └──────── visible unchanged target ────────┘
```

Deep modules:

- **`DynoProtocol`**: immutable, versioned protocol plus validation/compiler. It owns phases,
  transitions, cadence prerequisites, recovery, repetition, and content hash.
- **`DynoRunner`**: deterministic state machine over injected monotonic time. It owns no radio, file
  system, display, or model fitting.
- **`TrainerAdapter`**: capability discovery, command encoding, acknowledgement classification,
  clamping, control state, and captured Stop/Reset release.
- **`AssiomaReferenceSource`**: normalized reference power/cadence samples and identity.
- **`RunBundleSink`**: append-only event/capture writer with finalization and recovery after an
  interrupted run.
- **`PlantIdentifier`**: pure candidate fits, held-out validation, uncertainty, and model-adequacy
  verdict.
- **`DynoReport`**: pure metric/plot dataset builder and evidence-labeled diagnostic signatures.
- **`CompensationProfile`**: immutable identity, validated envelope, algorithm parameters, authority,
  evidence provenance, and acceptance results.
- **`PlantCompensator`**: pure bounded command stage; it cannot own the workout clock or prescription.

Small interfaces prevent trainer/vendor behavior, capture I/O, UI, and control math from collapsing
into one service.

### 7.2 `TrainerAdapter` contract

Conceptual interface:

```text
discover() -> TrainerIdentity + TrainerCapabilities
connect()
requestControl() -> protocol result
start() -> protocol result
setTarget(command) -> command id
stopToRecovery(target) -> protocol result
release() -> observed release result
poll()/events() -> TrainerObservation | CommandAck | AdapterFault
disconnect()
```

Implementations:

- `FtmsTrainerAdapter` wraps existing Python/C++ FTMS codecs and clients.
- `FecTrainerAdapter` is a later ANT+ FE-C path.
- A vendor policy is admitted only for a capture-proven quirk; Dyno never branches on a brand name.

Transport write acknowledgement and protocol command success are different event types. Optimistic
progression may exist only as an explicit, capture-grounded adapter policy.

### 7.3 Normalized records

Each record includes a monotonic timestamp and raw-byte provenance.

```text
TrainerCommand
  id, kind, requested target, clamped target, transport, encoded bytes

CommandAck
  command id, ATT/radio acknowledgement, protocol result, result code, target echo, latency

TrainerObservation
  power, cadence, speed, reported target, control state, source, raw bytes

ReferenceObservation
  power, cadence, balance/torque fields when available, source, raw bytes

OperatorEvent
  wizard action, STOP, END, gear label, cadence-invalid, note

FaultEvent
  identity mismatch, telemetry loss, control loss, timeout, storage fault, watchdog, release failure
```

The capture format must distinguish absent data from zero and preserve the original protocol frame.

---

## 8. Run bundle and retention

Each run is one downloadable directory/archive:

```text
manifest.json             identity, versions, hashes, timestamps, completion state
protocol.json             exact canonical protocol
events.jsonl              normalized append-only records
raw/                      lossless trainer/reference protocol records
summary.json              canonical metric/model results
metrics.csv               flat analysis export
plots/                    optional rebuildable render outputs
notes.json                operator-entered context
```

Requirements:

- write locally to CYD microSD;
- expose list/download/delete through the controller-hosted local PWA;
- no cloud dependency;
- never auto-delete raw bundles;
- warn when capacity is low and require explicit export/delete;
- finalize interrupted runs as incomplete, never success-shaped;
- derived files are rebuildable from raw/events/protocol;
- use one monotonic run clock and retain source receive timestamps;
- canonical metrics finalize after END from the complete bundle;
- live metrics are explicitly provisional.

The Python desk oracle reads the same bundle and must reproduce canonical analysis results.

---

## 9. Metrics, models, and reports

### 9.1 Metric definitions

- **Protocol latency:** command issuance to protocol-level success/failure acknowledgement.
- **Physical dead time:** fitted delay from command to sustained reference-power response.
- **Rise/fall time:** declared response fractions, separated by transition direction.
- **Overshoot/undershoot:** peak reference-power excursion beyond the final level, in W and relative
  to step amplitude.
- **Settling time:** first entry followed by five continuous seconds within
  `±max(5% of target, 10 W)` around the final reference-power level.
- **Tracking error:** W and normalized residual versus prescribed target over declared windows.
- **Integrated absolute tracking error:** time integral of absolute target/reference residual.
- **Oscillation:** settled residual RMS and statistically supported period/frequency.
- **Cadence sensitivity:** metric change across cadence strata and association with excursions.
- **Release behavior:** trajectory and time from STOP/END/disconnect to observed state.
- **Repeatability:** within-run and between-day spread and confidence intervals.

Protocol latency and physical dead time are always separate.

### 9.2 Candidate plant models

Fit at least:

- constrained first-order-plus-dead-time; and
- constrained second-order-plus-dead-time.

Select using held-out transition error and residual diagnostics. Report **model inadequate** when
neither predicts held-out behavior. A convenient FOPDT fit is not mandatory.

Initial Autotuner eligibility thresholds:

- held-out NRMSE ≤10% of commanded step amplitude; and
- key timing/gain estimates agree within 20% across the two baseline days.

These are versioned experimentation gates, not trainer-performance claims.

### 9.3 Required detailed views

The PWA and Python desk report provide:

1. aligned prescribed target, command, trainer, reference power, and cadence timeline;
2. normalized step-response ensemble with uncertainty;
3. metric distributions by condition;
4. power×cadence heatmaps faceted by gearing; and
5. trainer/firmware delta plots.

The CYD provides live traces, cadence coaching, state/faults, and a compact provisional summary.

### 9.4 Comparisons and diagnostic interpretation

- No composite score.
- Compare only like control semantics and identical protocol hashes, or explicitly shared
  conditions.
- Report the full evidence tuple with every comparison.
- Per-metric firmware verdicts are improved, regressed, or indeterminate only when uncertainty
  clears a predeclared practical threshold.
- Practical thresholds derive from measured repeatability/minimum detectable change plus versioned
  absolute floors.
- Never declare an overall firmware/trainer winner.
- Report evidence-labeled signatures such as transport delay, slow response, overshoot, hunting,
  cadence coupling, static bias, or release anomaly, followed by suggested tests.
- Do not assign a root cause the evidence cannot prove.

---

## 10. Autotuner normative behavior

### 10.1 Eligibility

An exact profile remains Dyno-only unless:

1. two different-day accepted baselines exist;
2. within-run and cross-day repeatability pass;
3. a candidate plant model passes the held-out thresholds;
4. the owner explicitly admits the profile to compensation experiments; and
5. replay/simulation tests show improvement without exceeding authority or disturbance bounds.

### 10.2 Algorithm selection

This specification does not preselect PID, feed-forward, lead/lag, inverse model, or another
controller. Eligible data drives a bounded candidate bake-off in replay and simulation.

Profiles are generated only from accepted Dyno bundles and are immutable during workouts. There is
no online parameter learning.

### 10.3 Authority and prescription boundary

- Explicit opt-in is required every workout session.
- Reboot, reconnect, profile change, or identity mismatch returns to RAW.
- The prescribed/displayed workout target never changes.
- Scheduled transitions may be pre-empted only when the future target is already known.
- Manual target changes are never predicted.
- Command deviation is bounded to `±min(15% FTP, 50 W)`.
- Lead time is bounded to `min(measured physical dead time, 3 seconds)`.
- Trainer range, rider absolute ceiling, profile envelope, and command slew apply in addition.
- Compensation interpolates only inside the validated envelope; outside it is RAW.
- Prescription and actual outgoing command are both visible and captured.

### 10.4 Bypass and faults

- RAW/TUNED is always visible.
- One CYD tap immediately disables compensation state and selects raw prescription.
- The outgoing command reaches raw target under the normal slew bound rather than an artificial
  command jump.
- Every compensation dependency/internal fault selects RAW immediately.
- Trainer-link failures are handled by `TrainerAdapter`, not hidden by compensation.
- Stateful terms cannot accumulate while saturated, bypassed, disconnected, or invalid.

### 10.5 Oscillation backoff

If oscillation exceeds the profile’s validated envelope:

1. reduce compensation authority once;
2. evaluate the next declared window; and
3. if oscillation persists, latch RAW for the rest of the interval and require manual re-enable.

This is bounded authority management, not online controller learning.

### 10.6 Hardware acceptance

On both baseline days, the candidate must achieve:

- integrated absolute tracking error improved by at least 15%;
- settling time improved by at least 10%;
- overshoot no worse by more than 5 W; and
- oscillation and cadence-linked error no worse by more than 10%.

Failure of any gate leaves the profile experimental/Dyno-only. No selective metric reporting.

---

## 11. State machines

### 11.1 Dyno run states

```text
IDLE
  -> PREFLIGHT
  -> WARMUP
  -> CADENCE_STABILIZE
  -> EXCITE
  -> RECOVERY
  -> ... next condition ...
  -> RECOVERY_STOPPED       (STOP)
  -> RELEASING              (END)
  -> FINALIZING
  -> COMPLETE | INCOMPLETE
```

Telemetry loss from an active state attempts `RECOVERY`; failed acknowledgement proceeds to
`RELEASING` and a blocking warning. Storage failure marks the bundle incomplete and surfaces the
fault; it does not fabricate metrics.

### 11.2 Compensation states

```text
RAW_DEFAULT
  -> TUNED_ACTIVE           (explicit per-session opt-in + exact profile match)
  -> TUNED_REDUCED          (first oscillation response)
  -> RAW_LATCHED            (persistent oscillation or any compensation fault)
```

Leaving the validated envelope selects RAW without invalidating the profile.

---

## 12. Verification and acceptance

### Simulator/desk acceptance

- Known plant parameters are recovered within declared test tolerance.
- Model inadequacy is detected, not forced into success.
- Protocol version/hash is deterministic across Python and firmware.
- Command/observation records and analysis metrics have golden-vector parity.
- Fault injection covers lost acknowledgement, lost trainer telemetry, lost Assioma telemetry,
  cadence invalidity, disconnect, storage exhaustion, STOP, END, failed release, stale profile,
  saturation, and oscillation.
- Compensation candidates cannot exceed authority or leave RAW/TUNED ambiguous.

### CYD/bench acceptance

- Real LVGL flow and touch actions are host-tested using the existing native LVGL harness.
- SD bundles survive reboot/interruption and remain rebuildable.
- CYD concurrently controls FTMS and reads Assioma BLE without violating measured loop/radio budgets.
- On-air simulator proves Request Control, Start, target, acknowledgements, recovery, Stop/Reset, and
  release before real trainer use.

### Hardware acceptance

- SB20 passes commissioning before advanced protocols.
- Tacx NEO repeats the same generic flow with no Dyno-core brand branch.
- Every real-trainer session has a session plan/actual, ledger entry, immutable captures, and
  promoted durable findings.
- A single day is provisional; two days are required for an accepted baseline.
- No Autotuner hardware experiment occurs before its exact profile passes eligibility and replay.

---

## 13. Open evidence questions, not owner decisions

These are deliberately answered by captures/experiments:

- SB20 and NEO exact control handshake and acknowledgement policy;
- Stop/Reset/disconnect release behavior;
- actual telemetry rate and timing uncertainty;
- adequate model family by operating region;
- exact timeouts and cadence-invalid evaluation windows;
- compensation algorithm;
- sprint rolling cadence and launch/ramp shape;
- whether sprint regions can become compensation-eligible;
- resistance and simulation protocol definitions after ERG ships; and
- exact KICKR transport behavior when hardware becomes available.

They must not be filled with plausible constants during implementation.

---

## 14. Approval gate

Approval of this document means:

- it becomes the product/technical baseline;
- the separate phased plan may be executed one PR at a time;
- physical-session protocols remain capture-gated;
- resistance/simulation and sprint launch details remain evidence decisions; and
- implementation is still subject to normal per-PR review and CI.

Both documents were explicitly approved on 2026-07-20. Implementation remains paused until a later
owner instruction.
