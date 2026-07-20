# Erg-response Dyno and optional Autotuner

**Status: RESEARCH (2026-07-20).** This is the cited research foundation for a trainer-agnostic
dynamic-response program. **Dyno** is the mandatory measurement stage. The optional **Autotuner**
is strictly evidence-gated plant compensation. This document creates no product code and makes no
trainer accuracy, response, or improvement claim: no such claim is justified before a repeatable
measured baseline.

Initial hardware targets are the **Stages SB20** and the owner's **Garmin Tacx NEO**. A
**Wahoo KICKR** is a future compatibility target. No trainer is supported merely because its
manufacturer or a standard says it should be; its exact model, firmware, capabilities, control
handshake, telemetry, and release behavior must first be captured.

This is **controlled-system dynamic response, not static A/B meter bias**. The existing
[`meter-compare-visualization.md`](meter-compare-visualization.md) surfaces answer where two meters
disagree. Dyno asks how a trainer plant responds over time after a target-power command. They may
share a reference meter and capture infrastructure, but they are different analyses and modules.

Companion canonical docs:

- [`ftms-protocol.md`](ftms-protocol.md) — the FTMS wire surface and uncaptured SB20 erg round-trip.
- [`sb20-power-topology.md`](sb20-power-topology.md) — which SB20 power path is being observed.
- [`ride-director.md`](ride-director.md) and
  [`on-device-workout-engine.md`](on-device-workout-engine.md) — prescription and execution.
- [`traffic-observability.md`](traffic-observability.md) — paired, time-aligned capture.
- [`perf-coex-plan.md`](perf-coex-plan.md) — firmware radio/load constraints.

---

## 1. Scope and invariants

A **trainer plant** is a controllable resistance device with:

- a command interface, initially target power over Bluetooth FTMS or ANT+ FE-C; and
- an observation interface, including timestamped power and cadence where available.

The program has two stages:

1. **Dyno:** safely excite the plant and estimate command-to-response delay, rise time, overshoot,
   settling time, steady-state error, oscillation, release behavior, cadence sensitivity, and
   repeatability.
2. **Autotuner, optional:** use only eligible Dyno evidence to shape commands downstream of the
   workout prescription, with bounded authority and instant bypass.

The invariants are:

- Dyno precedes and gates Autotuner.
- The workout prescription remains unchanged. Compensation may alter only the command sent to the
  trainer plant.
- Captures are immutable JSONL and retain raw protocol bytes.
- Captured behavior wins over specifications, vendor claims, and model assumptions.
- An independent reference meter, initially the Assioma, anchors delivered-power measurements.
- Pure identification, simulation, replay, and compensation logic precede hardware seams and are
  host-tested.
- No Autotuner profile is portable across trainer models or firmware revisions.
- Faults fail open to raw target pass-through or fail safe to stop/release, according to the
  explicitly tested fault class. They never leave hidden compensation active.

**Non-goals**

- Static meter calibration or A/B bias analysis.
- Adaptive workouts or changing the watts prescribed to the rider.
- A universal trainer driver ahead of device captures.
- Reverse-engineering a proprietary control surface before standard FTMS/FE-C capability is tested.
- Inferring SB20, NEO, or KICKR dynamics from reputation, community reports, or another trainer.

---

## 2. Current capability inventory

The repository already has most transport, orchestration, capture, and UI foundations. The new work
is dynamic identification and bounded plant compensation, not another workout or meter-compare
engine.

| Capability already built | Canonical implementation | Program use |
|---|---|---|
| FTMS codecs in Python and C++ | `code/src/sb20proxy/ble/ftms.py`; `firmware/lib/proxy/Ftms.h` | Standard command/telemetry vocabulary |
| FTMS erg command sequencers | `code/src/sb20proxy/ble/ftms_erg.py`; `firmware/src/ble/FtmsErgClient.*` | Existing command seam |
| FTMS software/on-air servers | `InProcessFtmsServer`; `firmware/src/ble/FtmsTrainerServer.h` | First safe protocol gate |
| ANT+ FE-C trainer twin | `code/src/sb20proxy/twins/trainer.py` | Existing second transport seam |
| Structured workouts and Ride Director | `code/src/sb20proxy/workout/`; `code/src/sb20proxy/ride/` | Prescription source; must stay outside compensation |
| On-device workout runtime | `firmware/lib/proxy/WorkoutRuntime.h` and related UI/runtime seams | Future head-unit driver |
| Paired meter capture and time alignment | `code/scripts/07_capture_multi.py`; `capture_ble_multi.py`; `13_build_sqlite.py` | Reference-power observation |
| Guarded FTMS capture | `code/scripts/capture_ftms.py` | Identity, capabilities, telemetry, round-trip gate |
| Live/offline meter comparison | `MeterCompare.h`; `compare.py`; Compare UI | Explicitly not the dynamics module |
| Controller-hosted web/LVGL patterns | `firmware/src/ui/`; `web/`; CYD/S3 findings | Future operator surfaces |

Important current behavior and gaps:

- `ErgController` and `FtmsErgClient` sequence Request Control, Start, and Set Target Power. They are
  not dynamics controllers and contain no plant model, slew policy, dead-time compensation, or
  anti-windup.
- `TrainerTwin` reports target power immediately. It has no delay, lag, overshoot, cadence
  disturbance, or noise, so it cannot yet prove an identifier or compensator.
- `capture_ftms.py --erg` already uses a guarded command sequence and sends Reset in cleanup. The
  long-running Python and firmware erg clients do not currently issue Stop/Reset when shutting down;
  release behavior therefore needs an explicit future safety seam.
- The firmware client can progress after an ATT write acknowledgement when a control-point
  indication is lost. That behavior was built for a constrained existing radio path; it is not
  evidence that SB20, NEO, or KICKR accepted a command. Acknowledgement policy must be capability-
  and capture-specific.
- Runtime trainer selection is presently a name substring. That is adequate for discovery, not for
  binding safety-critical dynamic profiles.
- The SB20 FTMS service, Feature, Supported Power Range, Control Point, and roughly 1 Hz Indoor Bike
  Data were passively captured. A complete command-to-resistance round-trip from this project is
  still not captured. No real trainer has a repository-grounded step response.
- The ESP32-C3 shares one core/radio across BLE roles, Wi-Fi, and display work. Dyno collection and
  any eventual compensation must remain inside the measured coexistence budget. The CYD/S3
  controller architectures offer more suitable operator surfaces, but selection remains an owner
  decision.

---

## 3. What the standards establish — and do not

Bluetooth SIG FTMS defines a standard client/server surface for fitness-machine capabilities,
telemetry, and control. FTMS 1.0 requires Errata 23224 for a compliance claim. The protocol exposes
capability bits, Supported Power Range, a Control Point with result indications, machine status,
target power, Stop/Pause, and Reset procedures. These are protocol semantics, not proof of a
particular trainer's control behavior. [S1]

ANT+ FE-C defines interoperable fitness-equipment control modes including Target Power, Basic
Resistance, and Simulation Parameters, plus capabilities, user configuration, and calibration
exchange. These modes are distinct experiments; only absolute target-power mode belongs in the
initial erg Dyno. [S2]

The official Garmin/Tacx and Wahoo product/support pages examined do not provide protocol-level
control handshakes, timing, loss-of-controller behavior, or dynamic-response guarantees for the
specific NEO and KICKR firmware under test. [S3][S4] Consequently:

- capability discovery is mandatory before issuing a command;
- hardware capture is the gate for every trainer/firmware/transport tuple; and
- control ownership, acknowledgement, Start requirements, last-target behavior, Stop, Reset, and
  disconnect release must be observed rather than assumed.

For FTMS, preflight should read and retain:

- Device Information Service manufacturer, model, serial, firmware, hardware, and software fields
  where exposed;
- Fitness Machine Feature;
- Supported Power Range and any relevant resistance/inclination ranges;
- Control Point properties;
- available telemetry/status characteristics; and
- a raw capability fingerprint.

The SB20 capture proves that optional characteristics cannot be assumed: its captured surface omits
Training Status. The same conservative rule applies to NEO and future KICKR devices.

---

## 4. Control-system research implications

A step response is a standard way to observe a system's transient and steady-state behavior. A
first-order-plus-dead-time (FOPDT) model is a useful candidate summary when a dominant lag and delay
fit the data, but it is not an assumption the trainer must satisfy. Residuals, repeated runs, and
alternative models decide whether it is adequate. [S5]

Transport delay limits achievable closed-loop bandwidth. More aggressive compensation is not
automatically better: delay, actuator saturation, quantized telemetry, and an existing hidden
trainer controller can create overshoot or oscillation. [S5]

Classical step/relay identification and automatic-regulator tuning provide useful experiment-design
precedents, but their original assumptions do not directly hold here: the trainer already contains
an unknown closed loop and a human rider supplies fluctuating power. The program may borrow bounded
excitation and conservative identification methods, not copy tuning constants uncritically.
[S6][S7]

Actuator limits and integral action can produce windup and overshoot. Any future stateful
compensator needs explicit saturation handling, anti-windup where relevant, bounded slew/authority,
and a stability argument grounded in the identified plant. [S5]

The rider is a measured disturbance and operating-point constraint. Since power is torque times
angular velocity, cadence changes alter required torque. Dyno must measure cadence, hold it within a
declared band where practical, reject or label excursions, and deliberately sweep cadence rather
than pretending imperfect cadence can be eliminated. Gear, target power, warm-up/temperature,
trainer firmware, and reference-meter state are also candidate covariates.

---

## 5. Measured facts, secondary clues, and hypotheses

### Repository-measured facts

- The captured SB20 FTMS surface advertises target-power capability, a 0-4000 W range with 1 W
  increment, a writable/indicative Control Point, and separate roughly 1 Hz Indoor Bike Data.
- The existing project has completed an on-air FTMS command round-trip against its own ESP32 trainer
  simulator, not against the SB20.
- SB20 and Stages/Assioma topology captures establish a static meter-domain difference. They do not
  establish erg dynamic response.
- Existing FTMS clients are command sequencers and the current trainer twin is instantaneous.
- No committed capture establishes SB20, NEO, or KICKR dead time, rise time, overshoot, settling,
  oscillation, or release behavior.

### Secondary clues — not design inputs

`sb20-hardware-reference.md` records community reports of SB20 filtering/hunting behavior. Those
reports may motivate measurements but cannot supply model parameters, limits, or claims.

### Hypotheses requiring captures

- Whether each trainer accepts and acts on this project's target-power sequence.
- Whether Start is required and which acknowledgement establishes control ownership.
- What happens to resistance after Stop, Reset, disconnect, telemetry loss, or controller crash.
- Whether a FOPDT model adequately describes a given operating region.
- Dependence on target power, step direction, cadence, gear, warm-up, temperature, transport, and
  firmware.
- Whether the trainer closes its erg loop around its reported power, another internal estimate, or
  a paired/external meter.
- Whether a bounded compensator can improve tracking without amplifying cadence disturbance.

---

## 6. Candidate safe experiment protocol

No wattage, step, dwell, cadence tolerance, or session-duration value is fixed by this research.
Those are owner decisions followed by simulator proof and a rider-reviewed run sheet.

### Gate A — known software plant

1. Use a pure, parameterized plant twin with injected delay, lag, saturation, noise, quantization,
   and cadence disturbance.
2. Drive it through the real command seam.
3. Prove that the identifier recovers known parameters with stated uncertainty and flags model
   mismatch.
4. Prove watchdog, abort, stop/release, pass-through bypass, logging, and incomplete-run handling.
5. Repeat through in-process FTMS and the on-air ESP32 trainer simulator before connecting a real
   trainer.

### Gate B — per-trainer capability and release capture

For the exact SB20 or NEO identity/firmware:

1. Ensure other trainer-control applications are disconnected.
2. Capture identity, firmware, capabilities, ranges, characteristic properties, and passive
   telemetry.
3. Confirm live reference-meter and cadence telemetry.
4. Request control with a bounded timeout; do not infer command acceptance from a transport write
   acknowledgement unless that policy was captured for this trainer.
5. Command one conservative target under direct rider/operator supervision.
6. Observe Control Point/status acknowledgement and independent power response.
7. Exercise the planned normal release and abort path, then verify actual resistance behavior.
8. Commit the immutable capture and update the trainer-specific protocol finding before dynamic
   excitation.

The Tacx NEO is the first second-trainer validation target because hardware is available. A KICKR
adapter remains a compatibility objective until exact hardware is available and passes this gate.

### Gate C — repeatable baseline

Candidate sequence:

- warm-up and steady holds to estimate baseline noise and steady-state error;
- bounded step-up and step-down pairs with generous dwell;
- a short, low-amplitude pseudo-random binary or multi-level sequence only after step safety and
  identifiability are proven;
- repeated identical transitions within a run and across runs;
- intentional cadence and target-power strata, with one changed factor at a time where practical;
  and
- identical protocol semantics for cross-trainer comparisons.

Pseudo-random excitation is not automatically safer or more informative. Its amplitude, switching
interval, spectral content, rider burden, abort criteria, and benefit over ordinary steps must be
proven in simulation before rider use.

Every run logs commands, acknowledgements, trainer telemetry, independent power, cadence, trainer
identity/capabilities, protocol transport, operator events, and monotonic timestamps. Raw bytes
remain available beside normalized records.

---

## 7. Candidate metrics and uncertainty

For a command transition from `u0` to `u1` at `t0`, with reference power `y(t)`:

| Metric | Candidate definition |
|---|---|
| Command acknowledgement delay | Control write to protocol-level success/failure indication, reported separately from physical response |
| Response dead time | Command time to first sustained departure from the pre-step noise band |
| Rise/fall time | Time between declared response fractions, reported separately by direction |
| Dominant time constant | Fitted model parameter or 63.2% time after delay when a first-order fit is adequate |
| Overshoot/undershoot | Peak excursion beyond the settled endpoint, in W and normalized to step size |
| Settling time | First time the response enters and remains in an owner-chosen absolute/relative band |
| Steady-state tracking error | Mean reference power minus prescribed target over a declared settled window |
| Oscillation/hunting | Settled residual RMS plus dominant period/frequency when statistically supported |
| Cadence sensitivity | Metric change across cadence strata plus association with cadence excursions |
| Release behavior | Time and trajectory from Stop/Reset/disconnect to the observed safe state |
| Repeatability | Within-run, between-run, and between-condition spread with confidence intervals |

The pre-step noise band, response fractions, settling band, minimum sustained duration, outlier
policy, cadence-validity band, fit method, and confidence method must be fixed before examining a
hardware result. Roughly 1 Hz telemetry places a quantization floor on timing estimates; higher-rate
reference telemetry does not remove uncertainty in the trainer's command/telemetry path.

Cross-trainer comparisons are admissible only when reported with:

`(manufacturer, model, firmware, software, capability fingerprint, transport, control mode,
reference meter, cadence/gear/power condition, protocol version)`.

FTMS target power and FE-C target power are comparable in intent, not automatically identical in
implementation. Resistance-percent and simulation-grade tests are separate programs. Absolute
power-accuracy claims require the same independent reference; trainer self-reported power alone
supports only self-tracking analysis.

---

## 8. Candidate architecture seams

The research supports a deep, transport-neutral core behind a small trainer adapter:

```text
Workout prescription / Dyno excitation
                  |
        optional bounded compensation
                  |
          TrainerAdapter interface
             /             \
      FTMS adapter       FE-C adapter
             \             /
       command + observation records
                  |
       immutable capture / replay
```

The normalized seam needs, at minimum:

- **identity/capabilities:** manufacturer, model, serial/pseudonymous device key, firmware/software,
  capability fingerprint, ranges, transport, control modes;
- **commands:** request control, start, set target power, stop, reset/release, monotonic timestamp,
  requested value, encoded bytes;
- **acknowledgements:** transport write acknowledgement versus protocol result, result code, status
  echo, timeout;
- **observations:** monotonic timestamp, power, cadence, speed where available, target echo,
  control state, provenance, raw bytes; and
- **events:** operator abort, telemetry loss, cadence-invalid interval, disconnect, watchdog,
  incomplete release.

`TrainerAdapter` should own capability discovery, handshake policy, clamping, and safe release.
Dyno identification and Autotuner logic should know no GATT UUID, ANT page, vendor name, or radio.
Vendor-specific behavior is admitted only behind a capture-grounded adapter policy.

An Autotuner profile key must include:

`(manufacturer, model, firmware revision, software revision, capability fingerprint, transport,
control mode, Dyno protocol version)`.

Any mismatch makes the profile stale and ineligible. Name matching remains a discovery convenience,
never profile identity. A stale, missing, corrupt, or ineligible profile means raw pass-through.

The independent reference meter is an observation, not the trainer command source. This preserves
the prescription boundary and avoids turning Dyno into adaptive workout control.

---

## 9. Evidence gates from Dyno to Autotuner

| Gate | Required evidence |
|---|---|
| G0 — protocol safety | Identifier, command, watchdog, bypass, Stop/Reset, and incomplete-run behavior proven on known software and on-air simulator plants |
| G1 — trainer round-trip | Exact trainer/firmware capability, control, one-target response, and release capture committed |
| G2 — reference topology | Independent meter confirms the delivered-power observation and resolves any trainer-specific power-path ambiguity |
| G3 — repeatable baseline | Predeclared metrics and uncertainty from repeated valid runs across the agreed operating conditions |
| G4 — eligibility decision | Owner explicitly approves whether this trainer/firmware has enough stable, repeatable evidence to investigate compensation |
| G5 — replay/simulation improvement | Candidate improves predeclared tracking metrics without violating authority, slew, overshoot, stability, cadence-disturbance, or fault bounds |
| G6 — conservative hardware opt-in | Exact-profile match, visible opt-in, instant bypass, safe release, and same-rig before/after evidence |

**The mandatory program ends at G3.** Autotuner work cannot be scheduled merely because Dyno exists.
Each trainer/firmware independently earns G4. A result for SB20 does not admit NEO; a NEO profile
does not admit another NEO firmware; neither admits KICKR.

No performance or accuracy claim is allowed before G3. No improvement claim is allowed before G6.

---

## 10. Safety and fault principles for owner decision

- The rider and operator can abort at any time from an agreed local control.
- Every run has a hard command envelope narrower than the trainer's advertised range.
- Target, step, slew, dwell, cadence, duration, and accumulated-work limits are explicit.
- Missing telemetry, lost control permission, disconnect, invalid identity, profile mismatch,
  cadence outside the validity band, watchdog expiry, or implausible response causes the declared
  safe action.
- A normal shutdown explicitly stops and releases control, then observes the resulting state.
- Controller disappearance and failed release are separate captured fault cases.
- Compensation authority and state are visible; bypass is immediate and does not require cloud,
  Wi-Fi, or a phone.
- The controller records whether an acknowledgement was only a transport ACK or an FTMS/FE-C
  procedure success.
- No stateful term continues integrating while saturated, bypassed, disconnected, or cadence-invalid.
- On-device load must not compromise the meter proxy, trainer link, watchdog, or operator control.

The precise safe action cannot be universal until release behavior is captured. Depending on
trainer behavior and fault class, pass-through may be safer than Stop; for other faults Stop/Reset
may be mandatory. The final specification must define this as a tested state machine, not a broad
exception handler.

---

## 11. Decisions reserved for one-question-at-a-time grilling

The research intentionally does not decide:

- product boundary and first release target;
- SB20 versus Tacx NEO sequencing after simulator proof;
- safe rider power/step/slew/dwell/session envelopes;
- cadence coaching, validity bands, gear treatment, repeats, and warm-up;
- exact metrics, plots, uncertainty, and acceptance thresholds;
- trainer identity, firmware update, and profile invalidation UX;
- Dyno storage/export and capture retention;
- head-unit, local web/PWA, and desk-analysis responsibilities;
- normal release, emergency abort, telemetry-loss, disconnect, and controller-crash actions;
- whether or when a trainer becomes Autotuner-eligible;
- compensation authority, transition shaping, pre-emphasis, oscillation backoff, and bypass;
- cross-trainer comparison claims; and
- later KICKR transport priority.

These decisions will be interviewed one at a time, with a recommendation and explicit rationale for
each. The resulting specification and phased plan require owner approval before implementation.

---

## 12. Sources

All external evidence below is primary or authoritative. Product pages establish only what they
actually publish; absence of protocol detail is not treated as proof of behavior.

| Ref | Source | Use | Accessed |
|---|---|---|---|
| S1 | Bluetooth SIG, [Fitness Machine Service 1.0](https://www.bluetooth.com/specifications/specs/fitness-machine-service-1-0/) | Official FTMS service and mandatory Errata 23224 notice | 2026-07-20 |
| S2 | ANT+ Alliance, [Fitness Equipment device profile](https://www.thisisant.com/developer/ant-plus/device-profiles/) | Official FE-C control modes and capabilities | 2026-07-20 |
| S3 | Garmin, [Tacx NEO product/support entry](https://www.garmin.com/en-US/p/929231/) and [Garmin Support](https://support.garmin.com/) | Official product/support material; no relied-upon dynamic or handshake claim | 2026-07-20 |
| S4 | Wahoo, [KICKR product entry](https://au.wahoofitness.com/devices/indoor-cycling/bike-trainers/kickr-buy) | Official product material; no relied-upon dynamic or handshake claim | 2026-07-20 |
| S5 | Åström and Murray, [Feedback Systems](https://fbswiki.org/wiki/index.php/Feedback_Systems:_An_Introduction_for_Scientists_and_Engineers) | Authoritative feedback, delay, stability, saturation, and step-response treatment | 2026-07-20 |
| S6 | Ziegler and Nichols, ["Optimum Settings for Automatic Controllers"](https://doi.org/10.1115/1.2899060), *Transactions of the ASME* 64 (1942) | Primary classical reaction-curve/closed-loop tuning precedent | 2026-07-20 |
| S7 | Åström and Hägglund, ["Automatic tuning of simple regulators with specifications on phase and amplitude margins"](https://doi.org/10.1016/0005-1098(84)90014-1), *Automatica* 20(5) (1984) | Primary bounded relay-identification/autotuning precedent | 2026-07-20 |

## No claim before baseline

There is no measured trainer erg step response in this repository. The SB20 round-trip is still a
standing capture gate; the current software plant is idealized; NEO and KICKR behavior is uncaptured.
Therefore no statement about trainer speed, accuracy, overshoot, stability, comparative quality, or
compensation benefit is warranted until the relevant evidence gate is satisfied. Every eventual
dynamic figure must trace to a committed capture.

