# SOL dreaming, 17 July 2026: 21 new device ideas

**Status:** DREAMING, not a roadmap or decision. These are hypotheses to test using the repository's
capture-before-code discipline.

This pass is deliberately about the two emerging target devices:

1. the battery-powered nRF52840 unit carried on a track bike or in a jersey, acting as a power-meter
   corrector and track-science instrument; and
2. the ESP32 touch head unit, often paired with an SB20, acting as a standalone indoor training console.

## What already exists

The ideas below assume, rather than rebuild, the current launchpad:

- The nRF device already does BLE correction/rebroadcast, on-device two-meter calibration, BLE and ANT+
  output, SB20 spoof mode, FTMS workouts, shifter bias, IMU recording, LittleFS persistence, BLE DFU, and
  control through the shared Web Bluetooth SPA and Garmin Connect IQ app.
- The ESP32 head units already provide colour touch UI, source/trainer picking, the same correction and
  calibration model, CPS rebroadcast/SB20 spoofing, an on-device workout engine, FTMS erg control, WiFi
  setup, diagnostics, and the shared SPA.
- The reusable seams are unusually strong: pure host-tested codecs and state machines, typed `UiAction`s,
  shared view models, portable correction profiles, digital twins, and a real-data pipeline.

## Deliberate non-duplication

This was compared with the earlier dreaming document in open
[PR #274](https://github.com/cauldnz/SB20-power-proxy/pull/274), including its bonus moonshots, and with
PR #275, which has already started the live A/B meter comparison and ANT shifter work.

Consequently, this document does **not** propose more games or an arcade, virtual terrain, force-feedback
games, smoothness/L-R coaching, readiness-adaptive workouts, one-tap calibration or passive drift watch,
FIT recording, ride ghosts, a rolling black box, live A/B meter comparison, ANT Stages spoofing,
electronic-shifter bridging, a generic multi-sensor rebroadcast hub, productising the existing corrector,
sonification, a web arcade, multiplayer, a Trainer OS, a games SDK, or a workout marketplace.

Some ideas below use similar foundations, but solve a different problem. In particular, static torque
calibration is not another two-meter calibration UX; trainer dynamics are not meter A/B comparison; and
erg-loop compensation does not change the workout prescription.

**Effort:** S = a focused software/hardware spike; M = several slices plus bench proof; L = a real
hardware programme with repeated track or rider sessions.

## What the selected ideas taught us

The two directions chosen for detailed planning — standing-start launch control (#5) and the
Dyno→evidence-gated Autotuner programme (#9/#10) — share a useful pattern:

- build a purpose-specific instrument and workflow, not another passive dashboard;
- run controlled, repeatable experiments with raw evidence and visible data quality;
- keep the real-time clock/control loop local and autonomous;
- make the measurement product useful even if optimisation never follows; and
- permit automation only after the exact device/setup demonstrates stable, predictable behaviour, with
  bounded authority and an immediate bypass.

The five additions in section C deliberately explore that same pattern in new directions.

---

## A. nRF track monitor and calibrator

### 1. UWB velodrome lap and sector transponder

Add a small UWB module to the jersey unit and place two or more anchors around the velodrome. The device
would detect virtual timing gates, attach power/cadence/IMU data to each lap and sector, and show immediate
splits on the Garmin or web app. Unlike GNSS, the system is designed for an indoor oval and can distinguish
where in the lap time was gained or lost.

- **Additional hardware:** DWM3001C-class UWB module, trackside anchor modules, batteries/enclosures.
- **Reuses:** IMU timestamps, power input, bounded recordings, GATT download, Garmin UI.
- **First gate:** establish repeatable gate-crossing timing under body shadowing and on the bank before
  choosing an accuracy claim or building lap analytics.
- **Effort:** L.

### 2. Team-pursuit formation coach

Give each rider an nRF/UWB pod and measure wheel-to-wheel gaps and rotation timing. A vibration motor could
quietly signal "close 20 cm", "gap opening", or "rotation due" without requiring a rider to look down.
After the effort, the system could align each rider's power with formation position and quantify the cost
of poor spacing or a late change.

- **Additional hardware:** one UWB pod per rider, coin vibration motor/driver, optional coach-side anchor.
- **Reuses:** low-power nRF form factor, synchronized power stream, IMU, peer status, Garmin control surface.
- **First gate:** prove stable peer ranging in a paceline at track speed without compromising concurrent
  BLE/ANT operation.
- **Effort:** L.

### 3. Portable static-torque calibration cradle

Turn the nRF device into a field calibration instrument that does not need a second power meter. A load
cell applies or measures force at a known crank radius while the onboard IMU verifies crank angle. The
wizard calculates applied torque, guides zero and span checks, and produces a calibration report or a
candidate correction profile. This could expose slope errors that a zero-offset command cannot fix.

- **Additional hardware:** compact crank fixture/strap, calibrated load cell, NAU7802/ADS1232-class
  load-cell ADC, known geometry or a measured lever arm.
- **Reuses:** calibration state machine, profile storage/export, GATT/SPA wizard, correction model.
- **First gate:** build an uncertainty budget for force, lever length, angle, fixture flex, and hysteresis;
  the mechanics must be more trustworthy than the meter being checked.
- **Effort:** M-L.

### 4. Velodrome aero and CdA field lab

Add differential air pressure plus temperature, humidity, and barometric pressure. Combined with lap
speed, power, and position, the pod could compare helmets, skinsuits, hand positions, and tyre choices
lap-by-lap. The first useful product need not promise an absolute wind-tunnel CdA: repeatable relative
"setup B cost 7 W at this speed" results would already be valuable.

- **Additional hardware:** SDP3x-class differential pressure sensor and pitot probe, SHT4x-class
  temperature/humidity sensor, barometer, carefully designed probe mount.
- **Reuses:** power correction, IMU, lap timing from idea 1, Web/Garmin summaries, capture analysis.
- **First gate:** demonstrate repeatable zeroing and relative ranking over repeated controlled laps before
  attempting an absolute CdA model.
- **Effort:** L.

### 5. Standing-start and launch analyser

Automatically split a standing start into reaction, first movement, first crank motion, first complete
revolution, peak acceleration, and 5/10/15-second power. An optical or UWB start trigger would provide a
real time zero; a wheel magnet or optical pickup would give an independent speed truth source. The result
is a coaching instrument for gear choice and technique, not a ghost race or game.

- **Additional hardware:** UWB/optical start beacon, hall or optical wheel pickup, optional vibration cue.
- **Reuses:** high-rate IMU, power/cadence data, Garmin display, event-oriented recording.
- **First gate:** align all clocks tightly enough that repeated starts produce stable phase boundaries.
- **Effort:** M.

**Planning status (20 July 2026):** the owner approved a detailed
[specification](../code/findings/track-launch-control-spec.md) and
[phased plan](../code/findings/track-launch-control-plan.md) as the baseline; implementation remains
explicitly paused.

### 6. Aero-position and head-tuck coach

Use the jersey pod as the torso reference and a second tiny IMU on the helmet or upper back. Relative
orientation can identify a rising head, opening shoulders, or a torso angle that drifts late in an effort.
A subtle haptic cue could tell the rider when they leave a personally learned aero envelope.

- **Additional hardware:** second BLE/ANT IMU tag, optional vibration motor in the jersey pod.
- **Reuses:** onboard IMU processing, radio plumbing, Garmin live status, recording/download.
- **First gate:** collect labelled laps for several positions and prove the classifier is rider-specific,
  stable, and useful despite track banking and whole-bike lean.
- **Effort:** M-L.

### 7. Drivetrain vibration fingerprint

Mount a contact accelerometer or piezo sensor near the bottom bracket and learn a baseline spectrum by
power and cadence. Changes could flag chain rub, poor tension, wheel imbalance, bearing roughness, or a
setup that became noisier after transport. The valuable output is a repeatable "mechanically different
from baseline" score; naming a specific fault should wait for grounded examples.

- **Additional hardware:** contact piezo or high-bandwidth accelerometer, analogue front end, repeatable
  mounting point.
- **Reuses:** IMU-style sampling/ring buffers, on-device feature extraction, capture download, power bins.
- **First gate:** show that maintenance changes produce a larger repeatable signature than rider,
  cadence, surface, and mounting variation.
- **Effort:** M-L.

### 8. Private breathing and thermal-strain sentinel

Experiment with the XIAO Sense's existing PDM microphone inside the jersey to estimate breathing cadence,
then add skin temperature and local humidity. The pod could mark the point where breathing pattern or
thermal strain changes sharply relative to power, and provide a discreet haptic warning during maximal
track efforts. Raw audio should be processed in RAM and discarded; this is a training signal, not a
medical diagnosis.

- **Additional hardware:** skin-temperature probe and humidity sensor; the microphone already exists.
- **Reuses:** nRF DSP/IMU sampling patterns, power timeline, Garmin telemetry, haptic output from idea 2.
- **First gate:** determine whether fabric and wind noise permit reliable breath-event detection in a
  jersey. If not, test a chest-band stretch sensor rather than storing audio.
- **Effort:** M.

---

## B. General-purpose indoor head unit

### 9. Trainer dynamics "dyno"

The head unit would run a standardized set of FTMS steps, ramps, and short pseudo-random target changes
while reading an independent reference meter. It would report command-to-response delay, rise time,
overshoot, settling time, oscillation, cadence sensitivity, and release behaviour. Riders could compare
trainer firmware versions or diagnose "erg feels bad" with a repeatable test rather than impressions.

- **Additional hardware:** none if an independent meter is available; an Assioma is an ideal reference.
- **Reuses:** FTMS control, workout engine, dual-meter plumbing, captures, plots and diagnostics.
- **First gate:** run a safe low-power protocol against the FTMS simulator, then the real SB20, and define
  metrics that remain meaningful when rider cadence is not perfectly controlled.
- **Not A/B compare:** this measures a controlled system's dynamic response, not static meter bias.
- **Effort:** M.

### 10. Per-trainer erg response autotuner

Use the dynamics learned by idea 9 to compensate the trainer rather than alter the workout. The prescribed
target remains unchanged, but the head unit can shape transitions, pre-empt known lag, limit overshoot,
and back off if oscillation appears. Profiles would be trainer- and firmware-specific, optional, bounded,
and instantly bypassable.

- **Additional hardware:** none.
- **Reuses:** deterministic workout clock, FTMS target writes, correction profiles, trainer simulator.
- **First gate:** prove in replay/simulation that compensation improves tracking without amplifying rider
  cadence changes, then require a conservative on-bike opt-in test.
- **Not adaptive workouts:** it compensates the plant; it does not decide that the rider should do an
  easier or harder session.
- **Effort:** M-L.

**Planning status (20 July 2026):** #9 and #10 graduated into one approved Dyno→evidence-gated Autotuner
planning programme on its owning branch. The Dyno remains independently useful, the Autotuner remains
optional and evidence-gated, and implementation is explicitly paused.

### 11. Camera-free indoor bike-fit assistant

Use two or three low-resolution multi-zone time-of-flight sensors around the bike, or small wearable IMU
tags, to measure hip rock, lateral knee travel, torso stability, and repeatability. This avoids filming the
rider or uploading video. The head unit could guide saddle-height and cleat experiments and compare each
change against a saved baseline.

- **Additional hardware:** VL53L5CX-class multi-zone ToF modules on adjustable mounts, or small IMU tags.
- **Reuses:** touch UI, synchronized cadence/power, session comparisons, local-only data handling.
- **First gate:** prove that the chosen sensor geometry can see the relevant body landmarks through a
  complete pedal cycle without frequent occlusion.
- **Effort:** L.

### 12. Heat-acclimation and climate controller

Add room and skin-temperature sensing, then control one or more fans through IR, BLE, or a local smart
relay. In normal mode it keeps cooling consistent as workload changes. In heat-acclimation mode it follows
a bounded protocol, displays thermal and cardiovascular drift, and always offers a physical/manual
override. This changes the training environment, not trainer resistance.

- **Additional hardware:** SHT4x/BME280-class environmental sensor, skin-temperature probe, controllable
  fan or IR transmitter/smart relay.
- **Reuses:** workout timeline, HR/power inputs, WiFi, UI alerts, local automation.
- **First gate:** integrate a fan with a fail-safe off/manual path and verify sensor placement before
  inventing any heat-strain model.
- **Effort:** M.

### 13. Instrumented hydration and fuelling station

Put the indoor bottle or nutrition tray on a small load cell. The head unit can measure actual fluid
removed, distinguish a reminder from a completed drink, and relate intake to workout load and room
conditions. It could learn a rider's normal consumption and warn when a long session is materially behind
plan without requiring taps during an interval.

- **Additional hardware:** bottle platform or cage load cell, load-cell ADC, optional NFC tags for bottle
  size or mix.
- **Reuses:** workout clock, environmental data from idea 12, touch UI, per-rider settings.
- **First gate:** solve slosh, bottle replacement, and hand-contact artefacts on a stationary-bike mount.
- **Effort:** S-M.

### 14. Independent dead-man stop

Add a deliberately separate safety channel that detects an empty bike or a pressed physical stop button.
If the head unit still owns FTMS control, it sends Stop/Pause and a minimum/resistance-release command,
then visibly latches the event until the rider resets it. Presence sensing might use seat pressure or
short-range radar; the physical button remains the authoritative path. This is an extra safeguard, not a
safety guarantee.

- **Additional hardware:** wired mushroom/large stop button, optional seat-pressure sensor or
  LD2410-class short-range presence sensor.
- **Reuses:** FTMS state machine, watchdog thinking, touch/LED status, trainer simulator.
- **First gate:** specify and test every disconnect/power-loss state so a failed sensor cannot create a
  success-shaped "safe" indication.
- **Effort:** M.

### 15. Tap-to-load household rider profiles

An NFC tap could select the rider before the first pedal stroke and load the correct FTP, meter, trainer,
correction curve, workout preferences, display layout, and safety limits. This prevents a shared SB20 from
silently using another person's meter or calibration. The NFC tag should contain only an opaque profile
ID; the actual profile stays on the head unit.

- **Additional hardware:** PN532/ST25-class NFC reader and inexpensive tags, or phone NFC if browser/device
  support proves sufficient.
- **Reuses:** NVS/LittleFS config, portable calibration profiles, source picker, workout settings.
- **First gate:** define an atomic profile switch and rollback path so a partial load cannot mix two
  riders' settings.
- **Effort:** S-M.

### 16. Sixty-second RF ride-readiness survey

Before a workout, the head unit actively checks every required link: advertised identity, battery,
notification rate, RSSI trend, duplicate names/IDs, reconnect time, packet gaps, trainer control grant,
and WiFi/BLE coexistence. It then gives concrete advice such as "move head unit closer to left pedal",
"two Stages 62144 devices are present", or "disable ride-mode WiFi". This is proactive site diagnosis,
not another after-the-fact black box.

- **Additional hardware:** none; an optional movable BLE beacon could help map dead spots.
- **Reuses:** scan list, `/diag`, performance counters, QA acceptance patterns, FTMS simulator.
- **First gate:** capture healthy and deliberately degraded bench runs and ensure each recommendation is
  tied to a measured condition rather than a generic warning.
- **Effort:** S-M.

---

## C. Third-pass product concepts

### 17. Sting — the bike-local velodrome timekeeper

A fixed-gear track bike has one known ratio and no freewheel, so crank revolutions provide a direct
distance measurement. Sting adds a high-rate downward optical line sensor to anchor the official line
crossing without GPS, a wheel magnet, a timing gate or anything installed on the track. V1 is a portable
flying-200 instrument with entry speed, time, peak speed and synchronized power/cadence. Once accuracy is
proven, the same distance engine could provide local haptic schedule cues for kilo and individual-pursuit
training.

- **Additional hardware:** bike-mounted high-rate optical/colour line sensor with a safe rigid mount;
  optional vibration motor for the later pacing workflow.
- **Reuses:** CPS crank revolutions, nRF event recording, Garmin/Web arm and review, launch-style local
  control loops, twins and golden vectors.
- **First gate:** prove reliable line recognition and bounded crank-derived distance/time error against
  official timing across tracks, gears and seated/standing riding before showing an absolute result.
- **Not UWB timing or Launch Control:** all sensing travels on the bike, nothing is installed trackside, and
  it measures a flying/timed effort rather than a standing-start sequence.
- **Effort:** M-L.

### 18. Sprintforce — whole-body sprint-force instrument

A standing sprint is not only leg power: the rider pulls and pushes the bars counter-phased to the pedal
stroke, and a power meter cannot see that force. A purpose-built strain-gauged track handlebar would let the
nRF pod record bar force, crank power and bike motion together, exposing force phase, left/right asymmetry
and how upper-body contribution changes from the jump to top speed.

- **Additional hardware:** structurally proof-tested strain-gauged handlebar, load-cell ADC and calibrated
  mechanical test fixture.
- **Reuses:** calibration/zero-span patterns, synchronized CPS/IMU capture, bounded recording/download and
  local plots.
- **First gate:** complete structural proof testing and a full force-uncertainty budget covering
  calibration, temperature, flex, hand position and hysteresis before any rider or coaching interpretation.
- **Not bike fit, vibration or meter calibration:** it measures a new rider-force axis, not posture,
  spectrum or power-meter accuracy.
- **Effort:** L.

### 19. Rider force-velocity profiler

Use the indoor head unit as the protocol director and analyzer for a certified isokinetic or mechanically
bounded cycle ergometer. It would measure the rider's peak crank torque across controlled cadences to build
a force-velocity profile for sprint and strength coaching. This is deliberately **not** implemented by
commanding an SB20 or consumer FTMS trainer to absorb maximal sprint power.

- **Additional hardware:** access to a documented professional braked ergometer, initially through a lab or
  equipment partner; a custom brake rig is optional future hardware, not V1.
- **Reuses:** sensor/protocol adapters, deterministic protocol execution, calibration models, recording,
  fit analysis and local review.
- **First gate:** passive-first testing on the chosen ergometer must demonstrate an inherent mechanical
  torque/speed safety envelope before active velocity control is considered. Otherwise ship only a passive
  multi-load force-velocity test director.
- **Not the Trainer Dyno:** the measured plant is the rider's force capability, not trainer command
  response, and the SB20 is explicitly outside the control path.
- **Effort:** M with a partner ergometer; L for custom hardware.

### 20. Record-attempt data notary

An independently powered, read-only witness device subscribes directly to the attempt's power meter and
cryptographically chains and signs each captured frame batch as it arrives. It produces a verifier-friendly
evidence bundle for record attempts, sponsored challenges or disputes. It proves that the captured stream
was not altered after collection; it does **not** prove that the meter was accurate, honestly configured or
attached to the claimed rider.

- **Additional hardware:** none beyond a separately powered nRF/ESP32 witness device.
- **Reuses:** lossless JSONL capture, CPS/ANT codecs, run identity, integrity checks and the repository's
  signing/verification patterns.
- **First gate:** mutate, remove, duplicate and reorder frames in bench captures and prove every change is
  detected before making any live evidence claim.
- **Not ride recording or the Black Box:** its only job is independent, tamper-evident third-party custody,
  not athlete-owned training history or troubleshooting.
- **Effort:** S-M.

### 21. SmO2 steady-state test director

Pair the indoor head unit with a muscle-oxygen sensor and replace arbitrary fixed-duration test stages with
measured steady-state detection. V1 captures SmO2, power and cadence during a manually stepped protocol and
shows whether each stage actually plateaued. Only after repeatability is proven may the head unit hold a
bounded aerobic ERG stage until the plateau rule is met and then advance automatically.

- **Additional hardware:** Moxy or comparable SmO2 sensor with a documented local radio profile.
- **Reuses:** sensor-source seam, workout clock, FTMS aerobic step control, capture/replay and local plots.
- **First gate:** capture real manual tests and validate the plateau detector offline across repeated days.
  Do not claim lactate threshold or physiological diagnosis without independent validation.
- **Not adaptive workouts or the Trainer Dyno:** it directs one standardized physiological test; it does
  not adjust everyday training difficulty or characterize trainer response.
- **Effort:** M.

---

## Hardware bundles that unlock several ideas

| Bundle | Candidate contents | Ideas unlocked |
|---|---|---|
| **Track ranging kit** | 3-4 UWB modules, batteries, anchor tripods/enclosures, vibration motors | 1, 2, 5 |
| **Calibration mechanics kit** | load cell, precision ADC, crank fixture, measured lever arm | 3 |
| **Aero/environment pod** | differential-pressure sensor, pitot probe, temperature/humidity/barometer | 4, 8, 12 |
| **Motion tags** | 2-4 small nRF/IMU tags with known mounting clips | 5, 6, 11 |
| **Indoor fit kit** | multi-zone ToF modules and adjustable mounts | 11 |
| **Indoor utility kit** | fan control, bottle load cell, NFC reader, physical stop button/presence sensor | 12-15 |
| **Bike-local timing kit** | high-rate optical line sensor, rigid mount, optional haptic | 17 |
| **Sprintforce mechanics kit** | proof-tested instrumented bar, load-cell ADC, calibration fixture | 18 |
| **Sports-science ergometer access** | documented certified braked ergometer or laboratory partner | 19 |
| **Independent witness** | separately powered nRF/ESP32 capture device | 20 |
| **Muscle-oxygen kit** | Moxy-class SmO2 sensor with documented local radio profile | 21 |

## My five strongest bets from the original 16

1. **Portable static-torque calibration cradle (#3):** closest to the project's calibration mission and
   creates a new source of truth rather than another display.
2. **UWB lap/sector timing (#1):** makes the jersey unit uniquely useful on a velodrome where ordinary
   GPS head units are weak.
3. **Trainer dynamics dyno (#9):** turns the existing FTMS and reference-meter stack into a genuinely
   differentiated indoor diagnostic tool.
4. **RF ride-readiness survey (#16):** likely the fastest useful head-unit feature and directly attacks
   the intermittent-radio failures that waste real sessions.
5. **Aero-position coach (#6):** a strong bridge between the existing IMU work and a track-specific
   coaching outcome.

The most economical exploration sequence is #16 first (software-only), then a load-cell spike for #3,
then purchase one UWB kit and test timing repeatability before committing to either #1 or #2. Ideas #9
and #10 have since graduated into one approved-but-paused planning programme; its first implementation
still begins with exact-unit SB20 measurement rather than compensation code.

## Strongest of the five additions

1. **Sting (#17):** the strongest concept in the independent bake-off: a track-specific instrument using a
   physical property unique to fixed-gear bikes, with no track installation.
2. **Record-attempt data notary (#20):** the cheapest evidence gate and a genuinely new artifact, although
   adoption is organisational rather than technical.
3. **Rider force-velocity profiler (#19):** a substantial indoor product if professional ergometer access
   is practical; consumer-trainer control is explicitly ruled out.
4. **Sprintforce (#18):** the most novel new sensing system, but mechanical safety and uncertainty make it
   a real hardware programme.
5. **SmO2 test director (#21):** the most conservative concept here; useful capture and analysis precede
   any live-control claim.

The economical sequence is a bench optical-line/distance experiment for #17 and a capture-tamper spike for
#20. The first action for #19 is partner discovery, not code. #18 begins with mechanical proof engineering;
#21 begins by borrowing or purchasing one sensor and capturing a manual test.
