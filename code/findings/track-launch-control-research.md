# Track cycling launch control - research foundation

**Status:** RESEARCH / REQUIREMENTS INTERVIEW COMPLETE (2026-07-20). No implementation is authorised by
this document. Draft build contract: [`track-launch-control-spec.md`](track-launch-control-spec.md);
delivery sequence: [`track-launch-control-plan.md`](track-launch-control-plan.md).

**Scope:** A coach-controlled start timer and timing board with a visible countdown and audible start
signal, paired to a bike-mounted nRF52840 Sense. The nRF records pre-start and launch IMU data plus the
best power-meter data the source actually exposes. The run is downloaded when the rider returns and is
erased only after the coach device verifies receipt.

This document records facts, a recommended architecture, explicit pushback, and the ordered decisions
needed before a build plan. It extends the standing-start idea in
[`docs/sol-dreaming-17Jul26.md`](../../docs/sol-dreaming-17Jul26.md); it is not an implementation plan.

## 1. Initial owner concept

- A dedicated start/timing board provides the countdown and beep.
- A coach controls it from a mobile app over WiFi or Bluetooth.
- The timing board communicates with the nRF board mounted on the rider's bike.
- At T-10 seconds the nRF begins recording.
- It records for a configurable period after the beep, initially 30 seconds, to cover the launch and
  potentially a lap.
- It records IMU and power-meter data at the highest rate the real devices and transports provide.
- When the rider returns to the coach, the run downloads and nRF storage is freed for the next run.

These are product inputs, not yet frozen requirements.

### User-provided start-gate reference

[Sven Boekhoven, "starting gate sound and counter" (YouTube, 2011)](https://www.youtube.com/watch?v=DqJms8JVHIc)
was created for practising track-cycling standing starts. It is a behavioural reference, not an official
timing-system specification.

Audio analysis of the 22-second reference found this repeatable pattern:

| Relative event | Observed cue |
|---|---|
| T-15 | about 0.67 s low tone |
| T-10 | about 0.67 s low tone |
| T-5 through T-1 | about 0.23 s low tone once per second |
| T0 / GO | about 0.27 s distinct higher tone |

The low cues have a dominant component around 155-156 Hz in this encoded video; the GO cue is around
462 Hz. Those frequencies are properties of this reference recording, not yet hardware requirements.
The important design feature is the spectrally distinct GO cue. It gives the bike microphone detector
a stronger discriminator than amplitude alone and directly supports starting IMU recording at T-10.

## 1.1 Interview decisions

1. **Reaction metrics - DECIDED:** report both:
   - headline: authoritative start T0 to first bike movement;
   - secondary: the same T0 to first crank/torque event.

   Use acoustic onset detected at the bike as the v1 authoritative T0 because it shares the nRF clock with
   IMU and power arrival markers. Preserve an installed system's controller-side pulse as metadata, not a
   directly subtractable timestamp. The run record must preserve the T0 source so results from different
   timing arrangements are not silently mixed.

2. **Start sequence - DECIDED:** ARM schedules the full fixed T-15 gate profile from the user-provided
   reference. The nRF begins pre-roll recording at T-10 and stops on its own configured post-start
   deadline. T-5 through T-1 use short cues and T0 uses a distinct GO tone. The schedule wire format
   remains extensible, but randomized and manually fired starts are not v1 modes.

3. **Installed timing integration - DECIDED:** include a generic opto-isolated trigger input in the v1
   timing-board hardware for dry contact/open collector and 5-24 V active pulses. Keep this a non-gating
   compatibility seam; venue-specific connectors and protocols wait for inspection of a named system.

4. **Missing trustworthy T0 - DECIDED:** retain the run and display a BLE-derived reaction estimate with
   an explicit low-confidence label. Preserve the T0 source and uncertainty class in the run record.
   Low-confidence values must not be silently pooled with acoustic or validated timing-system results.

5. **Acoustic T0 - DECIDED:** microphone-based acoustic onset at the bike is a required v1 capability and
   the primary standalone timing source. HB2/TM2 must prove reliable detection at intended mounting
   positions, distances and noise levels before the design proceeds to velodrome use.

6. **Run retention - DECIDED:** retain exactly one completed run in RAM. A new run is blocked until the
   coach device downloads, verifies and ACKs the prior run, after which the nRF erases it.

7. **Power transport - DECIDED:** v1 uses the existing BLE CPS source path and preserves raw frames plus
   crank-event timing. A simultaneous BLE/ANT standing-start capture is an evidence gate; ANT receive is
   added only if the actual meter demonstrates materially better launch information.

8. **First bike movement - DECIDED:** derive it from the captured IMU stream in v1 and validate against
   high-frame-rate video. Add a Hall/optical wheel sensor only if real starts show that body preload,
   frame flex and actual bike movement cannot be separated reliably.

9. **Timing-board display - DECIDED:** v1 uses a coach-sized display; the rider relies on the audible
   gate cadence. The coach display shows readiness, countdown, run state and transfer status. A large
   rider-visible numeric display and lights are not v1 requirements.

10. **Prototype controller - DECIDED:** use the standard Waveshare ESP32-S3-Touch-LCD-3.5 rather than the
    existing 1.47-inch board or a custom PCB.

11. **Controller form factor - DECIDED:** handheld 3-4 inch touch controller with dedicated external
    physical ARM and ABORT controls. It is coach-readable, not a rider-facing scoreboard.

12. **Prototype power - DECIDED:** external USB-C power bank. This keeps power swappable and avoids
    coupling the prototype to an onboard charger or custom battery design.

## 2. Research sources

### Repository sources

| Source | What it establishes |
|---|---|
| [`PROJECT-MAP.md`](../../PROJECT-MAP.md) | Shipped capability inventory |
| [`nrf52-sense.md`](nrf52-sense.md) | nRF hardware, IMU, S340 and live bench findings |
| [`nrf-roadmap.md`](nrf-roadmap.md) | nRF gaps and ANT roadmap |
| [`firmware-nrf/GATT.md`](../../firmware-nrf/GATT.md) | Current recording and download contract |
| [`ImuCapture.h`](../../firmware-nrf/lib/bridge/ImuCapture.h) | Actual sample format, capacity semantics and CRC |
| [`Proto.h`](../../firmware-nrf/lib/bridge/Proto.h) | Current RecCtl commands and accepted sample rates |
| [`firmware-nrf/src/main.cpp`](../../firmware-nrf/src/main.cpp) | Actual 8,192-sample buffer and polling loop |
| [`AntMasterChannel.h`](../../firmware-nrf/src/ant/AntMasterChannel.h) | ANT master exists; no ANT source/slave exists |
| [`BridgeConfigStore.h`](../../firmware-nrf/src/BridgeConfigStore.h) | Current LittleFS use is small config blobs |
| [`G-assioma17039-ble-20260615-065730.jsonl`](captures/G-assioma17039-ble-20260615-065730.jsonl) | Real Assioma CPS notifications at about 1 Hz |
| [`ASSIOMA-ble-cps-20260622.jsonl`](captures/ASSIOMA-ble-cps-20260622.jsonl) | Real `0x0023` frames with crank event timing |
| [`docs/architecture.md`](../../docs/architecture.md) | Host-test and real-data-first constraints |
| [`web/README.md`](../../web/README.md) | Existing SPA `BleTransport` / `HttpTransport` seam |

### Primary external sources

- Bluetooth SIG, [Cycling Power Service 1.1](https://www.bluetooth.com/specifications/specs/cycling-power-service-1-1/)
  and [Cycling Power Profile 1.1](https://www.bluetooth.com/specifications/specs/cycling-power-profile-1-1/).
  CPS defines fields and procedures, but not the real notification rate of a particular meter.
- Espressif, [ESP32-S3 RF coexistence guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/coexist.html).
  WiFi and BLE share one radio and receive scheduled time slices; latency must be measured under the
  intended WiFi/BLE workload.
- Chrome, [Web Bluetooth](https://developer.chrome.com/docs/capabilities/bluetooth). It is available on
  Chrome for Android and selected desktop platforms, requires a secure context and a user gesture, and
  makes the browser the BLE central.
- littlefs project, [official repository](https://github.com/littlefs-project/littlefs). littlefs provides
  copy-on-write power-loss resilience and dynamic wear levelling, but the usable `InternalFS` capacity
  on this board/build is not defined in this repository and must be measured.
- STMicroelectronics, [LSM6DS3TR-C product page](https://www.st.com/en/mems-and-sensors/lsm6ds3tr-c.html).
  The sensor can run faster than the rates currently exposed by this firmware; that does not prove the
  present polling loop can capture those rates reliably.

## 3. Current nRF baseline

### 3.1 IMU recording

The current implementation is a linear in-RAM capture:

- `ImuCapture<8192>` in `firmware-nrf/src/main.cpp`.
- Six `int16_t` axes per sample: accelerometer XYZ and gyroscope XYZ.
- 12 bytes per sample.
- Timestamps are implicit: capture start time plus sample index divided by configured rate.
- Accepted rates are 13, 26, 52 and 104 Hz. `Proto.h` rejects other rates.
- The loop polls the sensor using `micros()` pacing; it is not interrupt/FIFO driven.
- Capture stops when full and preserves the samples.
- Download frames already have a header, sequence, typed data frames and a CRC32 trailer.

`firmware-nrf/GATT.md` says 10,240 samples, but the code has 8,192. The code is authoritative.

### 3.2 Forty-second storage math

The proposed default is 10 seconds before the beep plus 30 seconds after it.

| IMU rate | Samples | Raw IMU bytes | Fits current 8,192-sample buffer? |
|---|---:|---:|---|
| 52 Hz | 2,080 | 24,960 | Yes |
| 104 Hz | 4,160 | 49,920 | Yes |
| 208 Hz | 8,320 | 99,840 | No; also not supported by the current protocol or loop |

**Research recommendation:** start at 104 Hz. It fits with substantial room for power events and run
metadata. Do not add 208 Hz until a sensor-FIFO or interrupt-driven spike proves loss-free acquisition
while BLE and ANT activity are present.

### 3.3 Current recording-state limitations

The current state enum has only `Idle`, `Recording` and `Downloading`. It has no concept of:

- an armed run;
- pre-roll versus post-beep recording;
- a start marker;
- automatic duration stop;
- completed-but-not-acknowledged data;
- run identity;
- abort versus successful completion; or
- more than one retained run.

`Stop` returns to Idle while retaining samples, and `Erase` is a separate command. That is a useful
starting point, but launch control needs explicit lifecycle states and an acknowledgement invariant.

### 3.4 Persistence

LittleFS is already used for config, correction curve, trainer name and button mappings. The repository
does **not** define or prove the usable `InternalFS` size for this build. The S340 app flash headroom is
not automatically equivalent to LittleFS capacity.

**Research recommendation:** v1 keeps exactly one run in RAM and requires verified download before
another arm. This already matches the proposed rider-return workflow and avoids speculative flash layout,
wear and recovery work. Multi-run flash queuing is a later requirement only if the interview establishes
that the coach cannot download between runs.

## 4. Power acquisition: what "highest possible rate" really means

### 4.1 BLE CPS

The real Assioma captures in this repository show Cycling Power Measurement notifications at roughly
one per second. The frames include flags `0x0023`, instantaneous power, pedal balance, cumulative crank
revolutions and last crank event time in 1/1024-second units.

The event-time field is important: notification arrival is about 1 Hz, but the meter also reports the
timestamp of its latest crank revolution. This can give a much better first-crank marker than treating
the BLE arrival time as the measurement time. It still does not produce high-rate instantaneous watts.

The Bluetooth CPS specification does not guarantee a notification rate. A different meter, firmware
version or connection may behave differently. The actual track meter must be captured during standing
starts before the storage schema or analysis claims are frozen.

### 4.2 ANT+

The S340 build currently implements an ANT Bike Power **master/output**. It does not implement the
ANT Bike Power **slave/input** needed to read a power meter over ANT+.

ANT Bike Power traffic uses a roughly 4 Hz channel cadence, but useful temporal fields depend on which
pages the actual meter transmits. Receiving BLE and ANT+ from the same meter does not necessarily create
independent sensor samples; it may only deliver the same internal estimate through two transports.

**Research recommendation:**

1. v1 records BLE CPS raw frames and local arrival timestamps because that path already works;
2. run a paired BLE+ANT capture of the actual track meter during starts;
3. add ANT receive only if the capture demonstrates material extra timing information or if redundancy
   is worth the radio and implementation cost.

### 4.3 Raw power-event record

Preserve enough information to re-interpret data later:

```text
PowerEvent
  local_arrival_us
  transport                  BLE_CPS | ANT_PAGE_10 | ANT_PAGE_12 | ...
  payload_length
  raw_payload[variable/max]
  decoded_instantaneous_w
  decoded_balance
  decoded_crank_revolutions
  decoded_last_crank_event
  decoded_event_time_units
```

Do not hard-code an eight-byte raw field for all transports: ANT pages are eight bytes, but CPS
measurements are variable length and the real Assioma frame is nine bytes.

## 5. Critical timing pushback

The research agent's initial proposal placed the beep timestamp in the timing board's clock and the IMU
samples in the nRF's clock, then subtracted one from the other. That is invalid without an explicit clock
offset and drift model.

Sending a BLE `START` message after the beep is also not a precision reaction-time clock:

- it includes connection scheduling and RF coexistence latency;
- the latency varies per run;
- it timestamps message receipt, not acoustic arrival at the rider; and
- sound propagation matters if the buzzer is not next to the start line.

### Recommended timing model

The timing board owns the countdown and physically emits the beep. The bike nRF owns the measurement
timeline. Its onboard PDM microphone detects the beep at the bike and records `acoustic_t0_us` in the
same local clock domain as the IMU and power-event arrival timestamps.

This yields:

```text
reaction_to_motion = first_motion_local_us - acoustic_t0_local_us
reaction_to_crank  = first_crank_event_local_us - acoustic_t0_local_us
```

BLE still carries arm/configuration commands and a redundant radio start marker, but it is not the
headline stopwatch.

This is a hypothesis, not a proven capability. The first gate is an acoustic capture with the board
mounted in its intended bike location, at realistic start-board distance and track noise. If the onboard
microphone cannot detect the beep reliably, the fallback choices are:

1. explicit board/nRF clock synchronisation plus measured radio latency;
2. a wired or optical start input;
3. a wheel or crank sensor for motion plus a local acoustic sensor; or
4. UWB/photogate infrastructure for a later precision tier.

No reaction-time accuracy should be advertised before comparison with high-frame-rate video or a logic-
level/acoustic bench reference.

## 6. Recommended system architecture

```text
Coach phone
  |
  | WiFi HTTP (setup, arm, abort, status, download)
  v
ESP32-S3 timing board
  - owns countdown schedule
  - drives display, lights and buzzer/horn
  - has physical ARM/ABORT controls
  - BLE central to bike nRF
  |
  | BLE GATT: arm + full autonomous schedule + status + run download relay
  v
Bike nRF52840 Sense
  - BLE central to power meter
  - BLE peripheral to timing board
  - records IMU at 104 Hz
  - detects local acoustic start marker
  - records raw power events
  - auto-stops on its own timer even if radio link is lost
  - retains one run until verified ACK
```

### Why WiFi from phone to timing board

- The phone is control/UI, not the real-time clock.
- The timing board serves the existing self-contained SPA over HTTP, so the phone does not need Web
  Bluetooth support.
- `HttpTransport` already exists as an architectural pattern.
- The timing board, not the phone browser, maintains the BLE connection to the bike.
- iOS/Android choice therefore does not gate v1.

The ESP32-S3 radio is shared by WiFi and BLE. Espressif's coexistence scheduler makes this feasible, not
deterministic. The arm/start/status path must be bench-measured with the intended SoftAP traffic. Because
local acoustic T0 owns the measurement, coexistence jitter affects control visibility rather than the
reaction-time calculation.

### Autonomous arm packet

The timing board should send the whole schedule before the rider leaves:

```text
ArmRun
  protocol_version
  session_id
  run_id
  rider_id
  profile_id                  TRACK_GATE_15 for v1
  recording_start_offset_ms   T-10 within the profile
  post_start_ms
  imu_rate_hz
  countdown_pattern_id
  expected_beep_signature
```

Once accepted, the nRF records and stops autonomously. Losing BLE after arm must not lose the run.

## 7. Proposed v1 run lifecycle

```text
IDLE
  -> ARMING
  -> ARMED_SCHEDULED          T-15 through T-10
  -> PREROLL                  T-10 through T0
  -> WAITING_FOR_ACOUSTIC_T0
  -> RECORDING_POST_START
  -> COMPLETE_UNACKNOWLEDGED
  -> DOWNLOADING
  -> COMPLETE_UNACKNOWLEDGED   on disconnect, CRC failure or NACK
  -> ACKNOWLEDGED
  -> ERASED
  -> IDLE

Any pre-beep abort:
  -> ABORTED_PRESTART -> erase with explicit coach confirmation

Any post-beep abort:
  -> ABORTED_RETAINED -> download/ACK like a normal run
```

**Data-safety invariant:** the nRF never erases the only copy merely because it finished sending. The
coach device verifies framing, sequence and CRC, persists its copy, then sends ACK with the matching run
ID. A timeout or disconnect leaves the run downloadable again.

## 8. Proposed v1 run record

```text
RunHeader
  schema_version
  session_id
  run_id
  rider_id
  firmware_version
  configured_pre_roll_ms
  configured_post_start_ms
  configured_imu_rate_hz
  imu_start_local_us
  acoustic_t0_local_us        optional/unset if not detected
  radio_start_local_us        optional diagnostic fallback
  recording_stop_local_us
  start_detection_confidence
  completion_reason
  imu_sample_count
  power_event_count
  flags                       aborted, meter_dropout, mic_missing, buffer_full, etc.

ImuStream
  fixed-rate accel/gyro samples

PowerEventStream
  variable-rate raw + decoded events

Integrity
  section lengths
  per-section or whole-record CRC
```

The eventual wire format must be generated or parity-locked across C++, the SPA and any Garmin/native
consumer, following the existing Bridge schema discipline.

## 9. Timing board v1

### Prototype direction

Use an off-the-shelf ESP32-S3 touch board to prove the system before designing a PCB. The rider does not
need to see the display in v1.

### Controller board comparison

Five nominal candidates were researched from manufacturer product pages, wikis and source repositories.
Two were rejected: LilyGO T-HMI has an awkward magnet/reed-switch power design and older toolchain
constraint; Guition JC3248W535 lacks sufficiently verified first-party expansion documentation.

The research agent initially ranked Elecrow using GPIO22-25 as "free" pins. ESP32-S3 does not expose
those GPIO numbers, so that score was discarded. The corrected comparison counts only documented
connectors and treats an external I2C GPIO expander as the honest way to attach non-time-critical
buttons. The timing-system trigger keeps a direct interrupt-capable GPIO.

| Candidate | Strengths | Wiring reality | Main risks |
|---|---|---|---|
| **[Elecrow CrowPanel Advance 3.5](https://www.elecrow.com/crowpanel-advance-3-5-hmi-esp32-ai-display-480x320-artificial-intelligent-ips-touch-screen.html)** | 3.5-inch 480x320, 400 cd/m2, ESP32-S3 N16R8, USB-C, onboard buzzer, I2S amplifier/speaker port, documented UART1 and I2C connectors | Use one UART1 pin as direct external-trigger input; ARM/ABORT can use an I2C expander; speaker path drives the cadence through an external transducer/amp as required | Exact LCD SPI pins are only in a schematic image; optional wireless/mic circuitry adds complexity; IO45 mode/strap behaviour needs bench verification |
| **[Waveshare ESP32-S3-Touch-LCD-3.5](https://www.waveshare.com/esp32-s3-touch-lcd-3.5.htm)** | 3.5-inch 320x480, ESP32-S3R8, 8 MB PSRAM, 16 MB flash, USB-C, ES8311 audio codec, TCA9554 I/O expander, official Apache-2.0 source | UART/I2C plus the onboard expander should cover controls; a direct trigger pin must be confirmed from the schematic/header map before purchase | 220 cd/m2 panel is dimmer; camera-FPC-routed pins and AXP2101 USB-only startup need verification; one USB-C port |
| **[Makerfabs ESP32-S3 Parallel TFT 3.5](https://www.makerfabs.com/esp32-s3-parallel-tft-with-touch-ili9488.html)** | Compact 66x84.3x12 mm, 480x320, N16R8, fast 16-bit display, dual USB-C including CP2104, official PlatformIO examples | One documented GPIO connector plus I2C; buttons can use an expander | No audio path, so cadence generation needs another module; parallel LCD consumes many pins; official examples target Arduino 2.0.16/LovyanGFX 0.4.8 rather than the repo's current stack |

**Provisional ranking:**

1. **Elecrow** - best trackside readability and the clearest audio/connector story.
2. **Waveshare** - strongest integrated audio and documentation, with unresolved direct-pin and PMIC details.
3. **Makerfabs** - best mechanical/development USB arrangement, but audio and pin pressure make it a less
   direct fit for a start controller.

The paper ranking is not enough to settle RF coexistence, actual speaker/horn drive, touch usability or
toolchain friction. A two-board Elecrow/Waveshare bench bake-off is lower risk than choosing from marketing
specifications alone.

Expected peripherals:

- loud, locally driven piezo/horn with a transistor or MOSFET driver;
- optional coach-facing status LEDs;
- physical ARM and ABORT controls;
- external USB-C power bank plus low-power indication;
- required opto-isolated external trigger/gate connector;
- enclosure and tripod/start-line mounting;
- optional later rider-visible display if field use establishes a need.

The actual acoustic transducer and display must be selected around measured distance, ambient noise,
venue rules and coach workflow. Sound level and audibility must not be guessed from a small bench buzzer.

### Candidate "track gate 15" start profile

The linked practice reference suggests a named start profile rather than scattered hard-coded delays:

```text
T-15  long ready cue
T-10  long cue + nRF begins IMU/power recording
T-5   short cue
T-4   short cue
T-3   short cue
T-2   short cue
T-1   short cue
T0    spectrally distinct GO cue
```

The timing board sends the complete profile before it begins. The nRF does not depend on hearing the
T-10 cue to start recording; it starts from the accepted schedule. It uses the distinct T0 cue for the
local reaction-time marker.

### Coach workflow

1. Coach powers the timing board and joins its setup/control WiFi.
2. Board reconnects to the selected bike nRF and displays battery, IMU, meter and storage readiness.
3. Coach selects rider/run settings and presses ARM.
4. nRF accepts the complete schedule before the timing board begins the T-15 sequence.
5. nRF starts recording at scheduled T-10; timing board continues the countdown and emits GO at T0.
6. nRF detects the acoustic signal and records until its local deadline.
7. Rider leaves BLE range without affecting capture.
8. On return, board reconnects and offers the completed run.
9. Coach downloads; phone verifies and saves; ACK permits erase.
10. System returns to ready for the next run.

The timing board needs a physical abort path even when the phone is disconnected or asleep.

## 10. V1 boundary

### Recommended v1

- One ESP32-S3 timing-board prototype.
- WiFi HTTP coach UI using the shared SPA patterns.
- BLE arm/status/download link between timing board and nRF.
- One retained run in nRF RAM.
- Full fixed T-15 gate sequence, with 10-second pre-roll and configurable post-start duration,
  default 30 seconds.
- 104 Hz accel/gyro.
- PDM microphone experiment and, if viable, local acoustic T0.
- Raw BLE CPS events plus decoded crank-event fields.
- Post-hoc acoustic onset, first bike motion and first crank event.
- Resumable, CRC-verified BLE download to controller microSD and explicit ACK-before-erase; this is a
  first-class v1 gate, specified in [`track-launch-ble-transfer.md`](track-launch-ble-transfer.md).
- Host-tested state machine and wire format before hardware code.

### Defer until evidence justifies it

- 208 Hz IMU.
- ANT Bike Power receive.
- Simultaneous BLE+ANT acquisition.
- Flash-backed multi-run queue.
- Hall/optical wheel sensor.
- Photogates, finish beacons or UWB.
- Multi-rider starts.
- Custom PCB/enclosure.
- Claimed reaction-time accuracy.
- Installed timing-system integration beyond the generic isolated input, raw pulse capture and silent
  compatibility mode. It must not gate the first usable v1.

## 11. Validation gates

### R0 - capture the real sources before designing analysis

- Measure BLE CPS notification timing from the intended track meter during standing starts.
- Capture its ANT pages concurrently if possible.
- Confirm which event timestamps and torque/power pages it actually emits.
- Record intended board mounting orientation and realistic vibration.

### R1 - pure host model

- Launch lifecycle and timeout model.
- ARM schedule validation.
- ACK-before-erase invariant.
- Run-ID manifest, offset/window resume, gap repair and idempotent durable staging.
- Run record pack/unpack and corruption cases.
- Counter wrap and missing-T0 handling.
- Synthetic IMU/audio/power timelines for analysis prototypes.

### R2 - bench timing

- Measure timing-board GPIO assertion versus acoustic onset.
- Capture the beep with the bike nRF microphone at realistic distances and noise.
- Measure IMU sample interval/jitter at 104 Hz while BLE CPS is active.
- Run the payload/link/WiFi/fault matrix from
  [`track-launch-ble-transfer.md`](track-launch-ble-transfer.md): explicit MTU, DLE, frame sizing,
  1M/2M, coexistence, 20/40/70-second payloads and repeated interruption/resume.
- Set a measured download-time acceptance target only after the baseline.

### R3 - static-bike launch simulation

- Full coach flow without track pressure.
- Compare microphone T0, IMU first motion and power crank events with high-frame-rate video.
- Exercise abort, missed beep, meter dropout, low battery and radio-loss paths.

### R4 - velodrome capture session

- Several starts with video ground truth.
- No threshold tuning before these captures.
- Compare candidate first-motion algorithms and quantify uncertainty.
- Record rider/coach usability and whether download-on-return fits the session rhythm.

Only after R4 should the project freeze reaction definitions, thresholds or accuracy claims.

## 12. Ordered owner decisions

The grilling interview should resolve these one at a time:

1. **Reaction definition - DECIDED:** capture both; headline first bike motion and show first crank/torque
   as a second phase. Local acoustic T0 is the v1 primary marker; an installed system's controller-side
   pulse is useful metadata but is not directly subtractable from nRF-local motion timestamps.
2. **Start sequence - DECIDED:** full fixed T-15 gate profile; nRF recording starts at T-10, short cues
   run from T-5 through T-1, and a distinct GO tone marks T0. Randomized and manual modes are later.
3. **Acoustic T0 - DECIDED:** required in v1. Microphone feasibility remains a hard R2 gate; BLE is the
   explicitly low-confidence fallback.
4. **Timing-board presentation - DECIDED:** coach-sized display; rider relies on the sound cadence. Large
   rider-visible countdown and lights are deferred.
5. **Run retention - DECIDED:** one run in RAM, downloaded and acknowledged between efforts.
6. **Power transport - DECIDED:** BLE CPS for v1; let the real simultaneous BLE/ANT capture decide whether
   ANT receive is promoted later.
7. **First-motion hardware - DECIDED:** IMU analysis first; wheel sensor only if capture/video evidence
   requires it.
8. **Controller board - DECIDED:** Waveshare ESP32-S3-Touch-LCD-3.5, standard version. Buy only this
   candidate rather than running a two-board bench bake-off.
9. **Sound geometry - DECIDED:** all-in-one handheld used within 5 m of the rider. V1 has no detached
   sounder; validate that the integrated amplified transducer is audible and reliably detected by the
   bike microphone at this distance.
10. **Physical controls - DECIDED, RECONFIRMED:** separate momentary START/ARM and ABORT buttons, plus
    equivalent app controls. START begins only after readiness checks pass; ABORT is spatially distinct,
    immediately silences the sound and cancels the run regardless of touchscreen or phone state. The
    coach unit owns sequence timing, so phone command latency cannot alter cue spacing or T0.
11. **External trigger electrical interface - DECIDED:** one protected opto-isolated interface accepts
    passive dry-contact/open-collector closures and 5-24 V active pulses. Trigger edge/polarity is
    configurable; venue-specific serial protocols remain out of scope.
12. **Capture duration - DECIDED:** fixed 10-second pre-roll and 30-second post-GO default; coach may
    select 10-60 seconds after GO in five-second steps. The 70-second maximum is 7,280 IMU samples at
    104 Hz, within the existing 8,192-sample buffer.
13. **Identity - DECIDED:** the coach app owns the rider roster. Each portable nRF has a visible case ID
    (a marker label is sufficient), is assigned to a rider before the run and can be reassigned at any
    time. Select the rider before every run; persist opaque rider, sensor, session and run IDs. Do not
    model bikes in v1 because the sensor moves between bikes.
14. **Coach deliverable - DECIDED:** after download, show a concise launch summary and synchronized
    IMU/power/cadence plots. Lossless export is later scope, but the internal run record remains lossless
    so export can be added without changing capture.
15. **Abort retention - DECIDED:** an abort before GO silences the unit, cancels the schedule and discards
    the partial pre-roll. An abort at or after GO stops capture and retains a truncated run marked
    `aborted` for review and explicit deletion after download.
16. **Pre-GO movement - DECIDED:** complete the sequence and capture; never auto-cancel in v1. A rider
    locked in a start gate is expected to show some preload/gate movement. Show the pre-GO trace and add
    a warning only if capture-grounded analysis later establishes a meaningful threshold.
17. **Multiple sensors - DECIDED:** remember multiple nRF boards and their current rider assignments, but
    select and arm exactly one rider/board per run. On selection, run a preflight challenge/readiness
    handshake that verifies identity, compatible protocol/firmware, battery, IMU, microphone, preferred
    power-meter identity/connection/live CPS freshness, empty run slot and ability to accept the complete
    schedule. Simultaneous starts are later scope.
18. **Coach app delivery - DECIDED:** an offline web app/PWA served directly by the Waveshare controller
    over its local WiFi. It must work without venue internet, cloud services or app-store installation;
    the onboard touchscreen retains local status/control.
19. **Run custody - DECIDED:** the Waveshare controller automatically receives the lossless record,
    validates framing and CRC, writes it durably to microSD and verifies the stored object before sending
    the erase ACK. The browser reviews controller-served data and is not the durability boundary.
20. **Return transfer - DECIDED:** when the selected board returns to BLE range with a completed run, the
    controller reconnects and downloads automatically with visible progress and retry. The coach may
    pause automatic transfer for troubleshooting; no physical action is normally required.
21. **Installed timing-system mode - DECIDED:** the handheld remains silent and the venue system owns all
    countdown/GO cues when a complete mode is eventually enabled. V1 records the isolated trigger pulse as
    controller metadata and proves the input seam only. Rolling nRF pre-roll, venue workflow and mirroring
    are deferred; this low-priority path does not gate the first usable v1.
22. **Delivery priority - DECIDED:** focus v1 engineering on the self-contained timer/cues, capture and
    fast, reliable BLE return download. Do not spend disproportionate v1 effort on installed timing
    systems beyond the generic isolated input and minimal silent compatibility mode.
23. **Download-time target - DECIDED:** measure the complete hardware baseline before choosing a wall-clock
    budget. Correct resume, durable CRC verification and ACK-before-erase are mandatory regardless of
    speed; record the eventual target in the benchmark results rather than guessing it in the spec.
24. **Transfer scheduling - DECIDED:** block preparation/arming of the next rider until the selected
    board's completed run has finished durable download. V1 does not pause a transfer to switch boards and
    does not maintain simultaneous download and next-rider BLE connections.
25. **Phone independence - DECIDED:** after rider/settings selection and a successful readiness handshake,
    the controller screen and physical buttons can complete or abort the launch without a connected phone.
    The controller remains authoritative; a reconnecting PWA reloads current state.
26. **Local access security - DECIDED:** reuse the existing per-device WPA2 SoftAP PIN, setup QR and
    captive-portal patterns. Add an explicit sensor-enrolment mode that bonds and remembers nRF boards;
    normal operation accepts only remembered sensors and authenticated local PWA clients. The repository
    has the WiFi prior art but no current nRF bonding implementation.
27. **Controller retention - DECIDED:** retain every verified run on microSD until the coach explicitly
    deletes it. Do not automatically evict by age or per-rider count; storage pressure is surfaced and
    blocks a new run safely rather than deleting history.
28. **Training sessions - DECIDED:** the coach explicitly creates/selects an active session with an
    editable default name and date; location and notes are optional. New runs attach to that session until
    it is closed or another session is selected.
29. **Immediate summary - DECIDED:** show reaction markers, early power/cadence build and explicit
    confidence/data-quality flags. Do not report IMU-derived speed or distance until real track captures
    establish that integration is trustworthy. Freeze exact power/cadence windows after source captures.
30. **Repository boundary - DECIDED:** implement v1 in this repository behind a clear `track-launch/`
    product boundary so it can reuse nRF firmware, GATT schemas, captures and tests. Reconsider extracting
    an independent repository after the R3 static-bike gate, when its release lifecycle and shared-code
    boundary are evidence rather than guesses.
31. **Sensor mounting - DECIDED:** require a standardized frame zone and orientation for comparable IMU
    traces. The owner will make a 3D-printed case with visible orientation marks and replaceable rubber-band
    retention; readiness records/verifies the expected orientation. R2 starts on the top tube near the
    head tube, with the case arrow forward and microphone opening unobstructed; captures may still refute
    that position.
32. **Sensor runtime - DECIDED:** target at least three hours of active nRF operation between charges,
    including setup, armed waiting and return downloads. Add real battery telemetry and reserve status;
    the current protocol field remains `unknown` because firmware does not populate it.
33. **Power-meter identity - DECIDED:** each rider profile stores a preferred BLE power-meter identity.
    When the coach assigns any nRF board to that rider, readiness applies the preferred meter filter; the
    coach can override it for the run without adding a bike entity.
34. **Missing power meter - DECIDED:** readiness blocks by default when the selected meter is unavailable,
    stale or not the rider's preferred identity. This is a preflight gate before ARMED. The coach may
    explicitly override and capture IMU only; mark the run degraded, exclude it from comparable power
    summaries and never silently substitute a nearby meter.
35. **Missing microphone - DECIDED:** do not block or require a coach override when microphone self-test or
    acoustic detection is unavailable. Proceed automatically using the BLE schedule marker, preserve the
    failure reason and label all reaction values low-confidence. Acoustic T0 remains the preferred source.
36. **Critical sensor battery - DECIDED:** warn early, then block new starts at a threshold established by
    discharge testing. Do not use an arbitrary percentage. The block protects the only RAM-backed run from
    power loss before return download.
37. **Arm/start separation - DECIDED:** a successful preflight enters a clearly displayed `ARMED` state but
    does not begin the cadence. A subsequent green physical START press or confirmed app START begins the
    autonomous T-15 sequence. Selection and preflight cannot accidentally launch a rider.
38. **Faults after START - DECIDED:** a late meter or non-critical sensor dropout does not alter the
    deterministic cadence or auto-abort. Continue IMU capture, preserve dropout intervals and mark the run
    degraded. Explicit physical/app ABORT remains the normal stop path.
39. **Run comparison - DECIDED:** the coach PWA can overlay the current run with one selected prior run on
    the same T0-aligned summary and synchronized plots. Multi-run overlays, best-of analytics, cohorts and
    automatic coaching recommendations are later scope.
40. **Controller UI boundary - DECIDED:** the 3.5-inch screen supports rider/session selection, preflight,
    ARMED/countdown, abort, transfer progress and the last run's headline summary. Synchronized plots and
    prior-run comparison remain in the phone PWA.
41. **Cue fidelity - DECIDED:** lock the reference profile's T-15 cadence, long/short durations and
    spectrally distinct GO structure. Tune actual frequencies and gain against the selected transducer and
    bike microphone; compressed-video pitch is not an exact requirement. No tone editor in v1.
42. **Acoustic preflight - DECIDED:** play a short end-to-end GO test when a sensor is first selected in
    each training session and after remounting. Record nRF-detected onset/SNR and reuse the result for later
    runs until retested. Failure does not block; it automatically selects visibly low-confidence BLE timing.
43. **microSD failure - DECIDED:** allow one run to proceed and remain in the nRF RAM slot if controller
    storage is missing, full or fails its write test. Show the run as at-risk and block every subsequent
    start until storage is repaired, verified and the retained run completes durable download. Do not erase
    or overwrite it and do not silently fall back to controller internal flash.
44. **Reboot recovery - DECIDED:** restore the last active session, roster assignments, settings, completed
    run staging and any retained-run risk state after controller restart. Always return unarmed and require
    a fresh sensor selection/preflight before START; never restore an ARMED or countdown state.

Additional decision recorded during interview: v1 timing-board hardware includes a generic opto-isolated
external trigger input, while venue-specific electrical/protocol adapters remain later research.

## 13. Open research risks

- The PDM microphone may be unusable in the intended bike mounting or track noise.
- The present polled IMU loop may jitter or miss samples at 104 Hz under full radio load.
- BLE CPS instantaneous power is only about 1 Hz on the captured Assioma; "high-rate power" may require
  ANT pages or a vendor-specific mode that has not been observed.
- The bike nRF may leave timing-board BLE range before all desired metadata is exchanged; the arm packet
  therefore must be complete and autonomous.
- The start board's WiFi SoftAP and BLE central share one ESP32-S3 radio.
- The current GATT path can fall to one IMU sample per notification at MTU 23, has no resume/gap repair,
  and shares the ESP32-S3 radio with WiFi. The v1 benchmark and protocol are specified in
  [`track-launch-ble-transfer.md`](track-launch-ble-transfer.md).
- The current GATT contract and recording state are mirrored across several consumers without the
  parity harness needed for a safe protocol revision.
- No reaction-time threshold or precision claim is yet grounded in a real standing-start capture.

## 14. Research conclusion

The concept is feasible without replacing the existing nRF recording foundation. The key architectural
decision is to avoid using BLE delivery as the stopwatch. Arm over BLE, run the countdown and beep on
dedicated hardware, detect acoustic T0 on the bike nRF, and compute motion and crank events in that same
local timeline.

The cheapest credible v1 is one 40-second, 104 Hz RAM capture with BLE CPS events, transferred by a
measured, resumable BLE path and acknowledged only after durable microSD verification when the rider
returns. ANT receive, flash queues, wheel sensors and deeper installed timing-system integration remain
valuable extensions, but none should block the first evidence-producing prototype.
