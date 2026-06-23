# Domain primer — smart bikes, power meters & the fitness-device wireless stack

**Status: general-knowledge orientation doc (2026-06-23).** A from-scratch primer on the smart-bike /
power-meter / fitness-device domain, so a new session or contributor can get grounded **without relying on
model-inherent knowledge that may be wrong or dated**. It is deliberately *general*: vendor-neutral
concepts + spec facts, cross-checked against public sources (cited inline + §Sources). The project's own
**captured bytes and decisions** live elsewhere and are authoritative on conflict — this doc points at them:

- [`phase-0-report.md`](phase-0-report.md) — the spoof spec + state of knowledge (the project's CPS/ANT+ contract).
- [`ftms-protocol.md`](ftms-protocol.md) — the FTMS byte layout **as captured/built here** (erg).
- [`shifter-ble-protocol.md`](shifter-ble-protocol.md) — the SB20 shifter, capture-derived.
- [`supported-meters.md`](supported-meters.md) — which meters work + the single-sided ×2 question, grounded in real frames.
- [`meter-to-meter-proxy.md`](meter-to-meter-proxy.md) — the corrector variant.
- [`zwift-controls-research.md`](zwift-controls-research.md) — Zwift Click/Play controller surface.
- [`decisions.md`](decisions.md) — the append-only log; **every numeric value the project actually captured.**

> **Read this for *concepts*; read those for *this project's measured facts*.** Where a number appears in
> both, the findings doc + capture win. Anything I could not verify against a public source is marked
> **(unverified)**.

---

## 1. Smart bikes & smart trainers

A **smart trainer** is a device the rear wheel (or a direct-drive cassette) couples to, with an
electronically controllable resistance unit (usually an electromagnetic brake / motor) and a power
measurement. A **smart bike** is the same idea built into a whole stationary bike — cranks, bars, a
flywheel, and a resistance unit — so there is no real bike attached. Both expose the *same* wireless
control surface (below); the difference is form factor.

### The two control modes (the distinction everything hinges on)

- **ERG mode (target-power mode).** An app commands a **target wattage**; the device's control loop
  adjusts resistance to make you produce exactly that power *regardless of your cadence*. Slow down and
  resistance rises; speed up and it falls. Used for structured workouts ("hold 250 W for 5 min"). The app
  writes the target; the device closes the loop. (How this looks on the wire: §3, FTMS *Set Target Power*
  / ANT+ FE-C.)
- **SIM / resistance / slope mode.** The app sends a **simulated road gradient** (and rider/bike mass, wind,
  rolling resistance — the "physics" parameters) or a raw resistance %; the device sets resistance to
  match, and your **power is whatever you push** at your chosen gear/cadence. Used for virtual-world riding
  (Zwift courses). Contrast with erg: in sim mode *you* control power via effort + gearing; in erg the
  *target* controls it and you can't escape it by shifting.

A device can usually do both; the app picks the mode per activity (a workout block = erg; free-riding a
course = sim).

### The major smart bikes (vendor-neutral sketch)

| Bike | Notable trait | Power source |
|---|---|---|
| **Stages SB20** (this project's target) | Power comes from **its own dual Stages crank-arm meters**; the bike's erg loop reads *that* crank and **only** that crank. | Built-in Stages L+R cranks |
| **Wahoo KICKR Bike** | Tilts (incline/decline), multiple crank lengths, configurable gearing; up to ~20% simulated gradient. ([DC Rainmaker shootout](https://www.dcrainmaker.com/2019/10/wahookickrbike-wattbikeatom-tacxneobike.html)) | Internal |
| **Tacx / Garmin NEO Bike** | Rigid (no tilt on classic), heavy electromagnetic flywheel emulation, built-in fans, up to ~25% gradient. | Internal |
| **Wattbike Atom** | Single crank length, near-shipped-assembled; long pedigree in gym/testing use. | Internal |
| **Zwift Ride / Wahoo KICKR Core combo, "Zwift Ride"** | A frame + controllers (Zwift Play-family) bolted to a KICKR Core trainer; emphasises in-game control. | The attached trainer |

**Why the SB20 is special here — the crux of this whole project.** Most smart bikes/trainers happily report
their own power *and* accept power from any external meter you pair. The **SB20's erg loop is hardwired to
its native Stages crank** — it will not take a third-party meter as its power input. So if the built-in
Stages crank is inaccurate, dying, or dead, erg training is mis-calibrated or broken and there is no
supported fix. **This project's answer:** read a trusted external meter (power-meter pedals), correct it, and
**re-broadcast it impersonating the Stages crank** so the SB20 accepts it as its own. See §6.

### How a smart bike presents itself wirelessly

Typically it advertises **both** radios at once:
- **ANT+** as an **FE-C** trainer (controllable) and often a separate **Bike Power** sensor.
- **Bluetooth LE** exposing **FTMS** (the standard trainer-control service, §3) and frequently **CPS**
  (power) and **CSC** (speed/cadence) too.

The SB20 specifically advertises (project capture, [`shifter-ble-protocol.md`](shifter-ble-protocol.md)) as
`Stages Bike 0105` with **FTMS `0x1826`**, **CSC `0x1816`**, **DIS `0x180A`**, plus **vendor services** that
carry the shifter buttons. Its crank meters separately advertise as Stages CPS devices (`Stages 62144` L,
`Stages 4963` R).

---

## 2. Power meters

A cycling **power meter** measures mechanical output, almost always by sensing **torque** (via strain
gauges) and **angular velocity** (cadence), since *power = torque × angular velocity* (`P = τ·ω`). Where the
strain gauges sit defines the type:

| Type | Where it measures | Trade-offs |
|---|---|---|
| **Pedal-based** (axle/body) | In the pedal spindle/body, one or both pedals | Easily moved bike-to-bike; exposed to crash/strike; can measure each leg independently. **This project's source meter.** |
| **Crank-arm** | Bonded to the (usually left) crank arm | Cheap, common; left-arm-only versions double (below); doesn't move easily between bikes with different cranksets. |
| **Spider / chainring** | The spider (where chainrings bolt to the crank) | Measures **total** power directly (both legs sum through the spider); accurate; crankset-specific. |
| **Hub** | Rear wheel hub | Measures total power downstream of the drivetrain (small drivetrain losses); wheel-specific. |

### Single-sided vs dual-sided, and the ×2 doubling convention

- A **dual-sided** meter measures both legs and reports **total** power (and a real **L/R balance**).
- A **single-sided** meter (e.g. a left crank-arm or a left-only pedal) measures **one leg** and, by
  convention, the head unit/app **doubles** it to estimate whole-bike power — assuming a **50/50** split.
  If your real balance isn't 50/50, a doubled single-sided reading is biased: a left leg producing <50% of
  total under-reports, >50% over-reports; asymmetry of 2–10%+ is common.
  ([Power Meter City](https://powermetercity.com/2016/06/24/total-vs-left-only-power-meters/),
  [4iiii](https://4iiii.com/blog/precision-3-pro-plus-dual-vs-single/))

**This matters operationally in this project:** the proxy reads a source meter and must know whether it's
already-total or needs ×2 — get it wrong and watts read half or double. This is the **single-sided ×2
toggle** in [`supported-meters.md`](supported-meters.md). On the SB20 side, the project's own finding is that
the **Stages L crank rebroadcasts a combined L+R figure on its own channel** (so the spoof needs only one
source → one master mapping; see Phase 0 §1.4) — i.e. it is *not* a naive ×2 of the left leg in that path.

### Left/right balance

A dual meter reports **pedal power balance** as a percentage (e.g. 48/52). On the BLE CPS wire this is one
optional field gated by a flag bit (§3). A doubled single-sided meter typically reports a synthetic flat
50/50 — which is itself a useful tell when verifying a source (the project's dashboard uses exactly this).

### Accuracy claims & why calibration matters

Vendors quote accuracy like "±1%" or "±2%". Real agreement between two *different* meters on the same bike
is often worse and **shape-dependent** — not a constant offset. The project measured the Stages-vs-Assioma
discrepancy as **torque/cadence-shaped** (~13% high at 60 rpm vs ~5% at 100 rpm at fixed power), exactly the
signature of a `P = τ·ω` slope error, **not** a flat scale ([`decisions.md`](decisions.md),
[`phase-0-report.md`](phase-0-report.md) §1.5). That's why a *fitted* correction can beat a single offset.

### Calibration: zero-offset, static torque, spin-down

Strain gauges drift with temperature and time, so meters support recalibration:
- **Zero-offset / "calibration" / manual zero.** With **no load** on the pedals, the meter records its
  baseline strain reading. This is the everyday calibration before a ride. On the wire it's an
  **offset-compensation** command on the CPS Control Point (`0x2A66`) or an ANT+ calibration page — the
  device replies with its measured offset value (§3, §6).
- **Static torque test.** Hang a **known weight** at a known crank length; the meter should report the
  expected torque. Confirms the gauges read true, not just zero.
- **Spin-down** (trainers, not pedal meters). Spin up to speed, stop pedalling, and the device times how
  long the flywheel takes to coast down — characterising drivetrain/belt friction so its internal
  power estimate stays accurate.

**Why it matters here:** the SB20's erg loop trusts its crank's calibration; and the spoof must **answer the
calibration handshake** the SB20 expects (a believable offset reply), or the bike rejects the crank. The
project captured the exact handshake — ANT+ broadcast page `01 AC …` with offset `903`/`−950`; BLE
zero-reset `200c01…` with offset **`0`** (note: BLE offset is `0`, not the ANT+ `903` — see
[`decisions.md`](decisions.md) / `[[old-branch-superseded]]`).

### Major brands per type — **pedal meters called out** (this project's source class)

- **Pedal-based (the product's supported source):** **Favero Assioma** (Uno = single, Duo = dual; and the
  Duo-Shi axle version) — the project's **reference meter**; **Garmin Rally** (RS/RK/XC families, single or
  dual); **Wahoo Powrlink Zero** (single or dual); **SRM EXAKT** (dual); **Magene PES P505**; (historically
  Garmin Vector / PowerTap P1). These expose standard BLE CPS and are exactly what a tester pairs as the
  source — see the Verified/Expected tiers in [`supported-meters.md`](supported-meters.md).
- **Crank-arm:** Stages, 4iiii, Shimano (left-arm versions common → ×2). *Out of scope as a SB20 source*
  (the product is pedals→crank), but the firmware *can* read them in the meter-to-meter corrector mode.
- **Spider / chainring:** SRAM/Quarq, Power2Max, Rotor, **XCadey** (the corrector use case). Report total.
- **Hub:** PowerTap (legacy), and various.

---

## 3. The wireless protocols (the core of the project)

Two ecosystems coexist; most modern devices speak both.

### ANT+ vs Bluetooth LE — the one-paragraph orientation

- **ANT+** — a low-power 2.4 GHz protocol (managed by the ANT+ Alliance / Garmin) built around standardized
  **device profiles**. A profile fixes the page/byte layout. Relevant here: **Bike Power** (device type
  **11 / 0x0B**) — a meter *broadcasts* power, it can't be controlled; and **FE-C** (Fitness Equipment
  Control, device type **17 / 0x11**) — **bidirectional** trainer control (an app sets resistance/target,
  the trainer streams data back). Setting a trainer's target power is an FE-C command (page `0x31` Target
  Power), **not** something the Bike Power profile can do.
  ([DC Rainmaker FE-C](https://www.dcrainmaker.com/2016/07/everything-wanted-trainers.html),
  [thisisant.com profiles](https://www.thisisant.com/developer/ant-plus/device-profiles))
- **Bluetooth LE (BLE) GATT** — devices expose **services** (16-bit UUIDs for SIG-standard ones), each
  holding **characteristics** you can **read**, **write**, or **subscribe** to (notify/indicate). The
  fitness-relevant SIG services are below.

**Key practical asymmetry:** **ANT+ supports unlimited concurrent listeners** (it's broadcast — a watch and
a head unit can both read one meter). Classic **BLE is point-to-point per connection** (a meter typically
talks to one central at a time), which is why proxies/bridges exist and why pairing conflicts happen. Many
meters therefore broadcast ANT+ *and* BLE simultaneously so a watch can take ANT+ while an app takes BLE.

### The BLE services that matter

All UUIDs below are SIG-assigned 16-bit (the full 128-bit form is
`0000xxxx-0000-1000-8000-00805f9b34fb`). Confirmed against SIG assigned-numbers / the
[bluetooth-gatt-parser](https://github.com/sputnikdev/bluetooth-gatt-parser) characteristic XML and
[the Nordic CPS spec PDF](https://devzone.nordicsemi.com/cfs-file/__key/communityserver-discussions-components-files/4/CPS.TS.1.1.2.pdf).

#### Cycling Power Service — CPS `0x1818`

The "I am a power meter" service. The project **reads** this from the source meter and **spoofs** it as the
Stages crank.

- **Cycling Power Measurement `0x2A63`** (notify). Layout: **`Flags` (uint16 LE)**, then
  **Instantaneous Power (sint16, watts, 1 W resolution)**, then each optional field present **in flag-bit
  order**. The flag bits (SIG, verified):

  | Bit | Meaning | Field it gates |
  |---|---|---|
  | 0 | Pedal Power Balance Present | balance (uint8, 1/2 %) |
  | 1 | Pedal Power Balance Reference | (metadata for bit 0: left vs unknown reference) |
  | 2 | Accumulated Torque Present | accumulated torque (uint16, 1/32 N·m) |
  | 3 | Accumulated Torque Source | (metadata for bit 2: wheel- vs crank-based) |
  | 4 | Wheel Revolution Data Present | cumulative wheel revs (uint32) + last-event-time (uint16) |
  | 5 | Crank Revolution Data Present | cumulative crank revs (uint16) + last-event-time (uint16) → cadence |
  | 6 | Extreme Force Magnitudes | max/min force pair |
  | 7 | Extreme Torque Magnitudes | max/min torque pair |
  | 8 | Extreme Angles | max/min angle pair (uint12 each) |
  | 9 | Top Dead Spot Angle | angle |
  | 10 | Bottom Dead Spot Angle | angle |
  | 11 | Accumulated Energy | energy (kJ) |
  | 12 | Offset Compensation Indicator | (informational) |
  | 13–15 | Reserved | — |

  The project's codec decodes **bits 0–5** (all any pedal/crank meter has been seen to set); 6–12 are
  extend-on-demand. Real examples: Assioma flags `0x0023`, Stages crank `0x002F` — see
  [`supported-meters.md`](supported-meters.md) for the exact frames + golden vectors.
- **Cycling Power Feature `0x2A65`** (read). A uint32 bitfield declaring *which* of the above the meter
  supports (balance, torque, wheel/crank revs, etc.). A central reads it once to know how to parse
  Measurement. (Project's spoofed Feature value: `525067`.)
- **Cycling Power Control Point `0x2A66`** (write + indicate). Commands to the meter. The relevant one is
  **Start Offset Compensation (op `0x0C`)** = the **zero-reset / calibration**: the central writes the
  request; the meter indicates a response carrying its measured offset (sint16). On the wire here:
  `20 0C 01 <offset_sint16_LE>` (the `20` = Response Code, `0C` echoes the op, `01` = success). The project
  found **no bonding is required** to do this on either meter (Phase 0 §1.6) — which simplifies the spoof,
  because the SB20 issues this and the impersonator must answer it.
- (Also a **Sensor Location `0x2A5D`** read, e.g. "left crank"/"rear wheel".)

#### Cycling Speed & Cadence — CSC `0x1816`

- **CSC Measurement `0x2A5B`** (notify): wheel-rev and/or crank-rev cumulative counts + event times — the
  consumer differentiates them into speed/cadence. The SB20 exposes this (idle crank-rev heartbeat seen in
  [`shifter-ble-protocol.md`](shifter-ble-protocol.md)). Power meters often fold cadence into CPS instead
  (bit 5 above), so a standalone CSC isn't always present.

#### Fitness Machine Service — FTMS `0x1826`

The standardized **trainer / smart-bike** service — telemetry *out* and control *in*. This is how erg works
over BLE. Project byte-level detail (built + partly captured) is in [`ftms-protocol.md`](ftms-protocol.md);
the spec-level summary:

- **Indoor Bike Data `0x2AD2`** (notify): live telemetry. `Flags (uint16 LE)` then each present field in
  flag order — instantaneous speed, cadence, distance, resistance, **instantaneous power (sint16 W)**,
  average power, HR, etc. **Trap:** bit 0 is *"More Data"* and **inverts** — instantaneous speed is present
  when bit 0 is **clear** (see [`ftms-protocol.md`](ftms-protocol.md)).
- **Fitness Machine Control Point `0x2AD9`** (write + indicate): the command channel. The machine replies
  with `0x80 <req-op> <result>` (`0x01` success … `0x05` control-not-permitted). **ERG mode is exactly
  this sequence:** **Request Control (`0x00`) → Start/Resume (`0x07`) → Set Target Power (`0x05` + sint16 LE
  watts)**. Other ops: Reset `0x01`, Stop/Pause `0x08`, Set Indoor Bike Simulation `0x11` (the sim-mode
  grade/wind/mass command), Set Targeted Cadence `0x14`.
- **Fitness Machine Feature `0x2ACC`** (read): two uint32 LE bitfields — machine features, then
  target-setting features. **Erg capability = Target-Setting bit 3 "Power Target Setting Supported".**
- **Supported Power Range `0x2AD8`** (read): min/max/increment watts the machine accepts.
- **Fitness Machine Status `0x2ADA`** (notify): event stream, e.g. *Target Power Changed* (op `0x08` +
  watts) confirming an erg set landed.
- (Often **Training Status `0x2AD3`**, read+notify.)

#### Device Information `0x180A` (DIS) and Battery `0x180F`

- **DIS `0x180A`** (read): manufacturer name, model, serial, hardware/firmware revision strings. **The
  spoof must reproduce these** (the SB20 checks the Stages identity, not just any CPS device — Phase 0 §1.3).
- **Battery `0x180F` → Battery Level `0x2A19`** (read/notify): 0–100%.

### How they compose in practice (the surface this project spoofs/reads)

```
  Power-meter pedals ──CPS 0x1818 (notify 0x2A63)──▶ head unit / app   (it reads watts)
  Smart bike / trainer ──FTMS 0x1826──▶ app reads Indoor Bike Data 0x2AD2 (telemetry)
                       ◀──FTMS 0x2AD9── app writes Set Target Power      (ERG: closes the loop)
```

A meter *advertises* CPS and the head unit subscribes; a trainer *exposes* FTMS and the app writes a target
power, which the trainer holds by adjusting resistance — that write-and-hold **is** the erg loop. The SB20's
twist (§1) is that its erg loop reads power **only** from its native Stages crank's CPS/ANT+ stream, so to
feed it external power you replace *that* stream — which is the spoof.

---

## 4. Training apps & ecosystems

- **Zwift** — a virtual-world game; the dominant indoor-training app. Pairs to a **power source** (CPS or
  FTMS or ANT+ Bike Power) and a **controllable trainer** (FTMS / FE-C) — often the same device, but it can
  read power from a meter while controlling a separate trainer ("power-match", below). Adds **virtual
  shifting** and in-game **controllers** (Zwift Click / Play / Ride — §below).
- **TrainerRoad** — structured-workout focused (erg-heavy); pairs the same way, emphasises the workout/erg
  loop over a game world.
- **Others:** Wahoo SYSTM, Rouvy, Kinomap, MyWhoosh, Garmin Tacx Training, plus device-specific apps (the
  **Stages app** configures the SB20). All consume the same CPS/FTMS/FE-C surfaces.

**How pairing works:** the app scans BLE (and/or ANT+) and lists devices by the service they advertise — a
CPS device under "Power", an FTMS/FE-C device under "Controllable". You can mix vendors freely **except**
where a device locks its inputs (the SB20's crank dependency is exactly such a lock). **Power-match:** read
watts from a trusted external meter but send erg *control* to the trainer, so the trainer holds *the meter's*
number rather than its own — conceptually what this project does in hardware for the SB20.

### In-game controllers (Zwift Click / Play / Ride) — on the roadmap

Zwift's controllers are **BLE peripherals Zwift connects to** (they talk to *Zwift*, not the trainer):
- **Zwift Click** — a 2-button (+/−) clicker for **virtual shifting** (with the Zwift Cog). Stateless
  up/down; Zwift owns the gear index.
- **Zwift Play / Ride** — handlebar controllers: steering, braking, Ride-Ons, Power-Ups, menu, plus
  shifting; richer protocol.

Protocol shape (clean-room, from public reverse-engineering — **do not copy GPL prior art**): a custom
"Race Controller" GATT service (`00000001-19ca-4651-86e5-fa29dcdd09d1` with notify/write/indicate
characteristics), a **`RideOn`** ASCII handshake, **Protocol Buffers** button messages, and for Play/Ride an
**ECDH (secp256r1) + HKDF-SHA256 + AES-256-CCM** encrypted channel. Full detail + sources in
[`zwift-controls-research.md`](zwift-controls-research.md). Relevance: the project has the **SB20 shifter
fully decoded** ([`shifter-ble-protocol.md`](shifter-ble-protocol.md)) and a roadmap item to re-present
those button presses to Zwift as a Click/Play — the SB20's stateless one-hot buttons map cleanly onto the
Click's stateless model.

---

## 5. Core concepts glossary

- **Erg mode** — app sets a **target watts**; device holds it by varying resistance, independent of cadence.
- **Sim / resistance / slope mode** — app sets a **gradient / resistance %**; your power is whatever you push.
- **FTP (Functional Threshold Power)** — the ~1-hour max sustainable power; the personal anchor for
  intensity. Often estimated from a 20-min test (~95% of it).
- **%FTP / power zones** — training intensities expressed as a fraction of FTP (e.g. Z2 endurance ~56–75%,
  threshold ~91–105%, VO2max ~106–120%). Workouts are scripted in zones; the app translates to watts via FTP.
- **Power balance (L/R)** — split of total power between legs; only a dual-sided meter measures it for real.
- **Cadence** — pedalling rate in rpm; with torque it determines power (`P = τ·ω`).
- **NP (Normalized Power)** — a power average weighted to penalise variability, approximating the
  physiological cost of a surgy ride better than a plain average. *(Coggan/TrainingPeaks terminology.)*
- **IF (Intensity Factor)** — NP ÷ FTP; how hard a ride was relative to threshold (~1.0 = at threshold).
- **TSS (Training Stress Score)** — a single number combining duration and intensity (≈100 = one hour at
  FTP); used to track training load. *(TrainingPeaks/Coggan; exact weighting is proprietary — the concept,
  not the formula, is what matters here.)*
- **Calibration / zero-offset** — recording the no-load strain baseline so power reads true (§2).
- **Trainer difficulty** — a Zwift slider that **scales the felt gradient** (e.g. 50% halves how steep
  climbs feel) without changing the simulated speed/physics — purely a comfort/realism knob, distinct from
  erg.
- **Power-match** — read watts from an external meter while controlling the trainer, so erg/sim hold the
  *meter's* power instead of the trainer's internal estimate.

---

## 6. How it all ties to THIS project

The proxy is one idea — **read a power meter → correct it → re-broadcast it so a consumer accepts it as its
own** — in two product modes (same `ProxyCore`, different *identity* + *correction*):

1. **SB20 crank spoof (the primary product).** The SB20's erg loop reads power **only** from its native
   Stages crank (§1). So: read trusted **power-meter pedals** over **BLE CPS `0x1818`** (the §3 surface),
   apply a **correction**, and **re-broadcast as the Stages L crank** — same CPS Measurement framing
   (flags `0x2F`), the Stages DIS identity, and crucially **answering the calibration handshake** on the
   Control Point (`0x2A66` / ANT+ cal page) the SB20 demands. The SB20 then accepts it as its own crank and
   its **erg loop closes on the pedals' watts**. Phase 0 proved the SB20 passes crank power through *without
   internal rescaling*, so feeding true pedal watts makes erg targets land on true pedal power — **no
   correction model is even required for the core goal** ([`phase-0-report.md`](phase-0-report.md)).
   *Why a third-party meter can't just be paired instead:* ID-matching alone failed — the bike checks the
   full Stages identity/contract, not any CPS device (Phase 0 §1.3).
2. **Meter-to-meter corrector (a variant).** Read a non-pedal meter (e.g. an XCadey spider) over BLE CPS,
   apply a **fitted correction** (so it agrees with a reference like the Assioma), and re-broadcast as a
   standard CPS meter **under our own identity — no spoof**, because head units accept *any* CPS meter.
   Built on-device with a calibration wizard. See [`meter-to-meter-proxy.md`](meter-to-meter-proxy.md).

**FTMS for erg control** is the *other* half: where the spoof feeds the bike's *input* (power), **FTMS
`0x1826`** (§3) lets the project *drive* the SB20's erg target — *Request Control → Start → Set Target
Power* — which is how the Ride Director and the "shifter nudges erg watts" feature command wattage. See
[`ftms-protocol.md`](ftms-protocol.md) and [`shifter-erg-control.md`](shifter-erg-control.md).

**The shifter / Zwift-controller** roadmap (§4) reads the SB20's decoded button presses
([`shifter-ble-protocol.md`](shifter-ble-protocol.md)) and re-presents them — to Zwift as a Click/Play, or
to the SB20's own FTMS erg target — reusing the same read→re-present spine.

So every §3 service maps to a concrete project role: **CPS** = what we read (source) and spoof (target);
**FTMS** = how we set erg; **DIS/Battery** = identity we must reproduce; **vendor GATT** = the shifter we
read. The general concepts here; the **measured bytes** in the findings docs and
[`decisions.md`](decisions.md).

---

## Sources / further reading

**Bluetooth SIG / GATT (verified the UUIDs + CPS flag layout against these):**
- SIG-style Cycling Power Measurement characteristic XML (flag bits + field order):
  <https://github.com/sputnikdev/bluetooth-gatt-parser/blob/master/src/main/resources/gatt/characteristic/org.bluetooth.characteristic.cycling_power_measurement.xml>
- Cycling Power Service spec (Nordic-hosted SIG PDF): <https://devzone.nordicsemi.com/cfs-file/__key/communityserver-discussions-components-files/4/CPS.TS.1.1.2.pdf>
- FTMS / fitness-data field reference (community gist): <https://gist.github.com/marcelrv/6e8f75b2aa6b3967b8159bc6a8617a47>
- "Take control of your Fitness machines" (FTMS walkthrough): <https://medium.com/decathlondigital/take-control-of-your-fitness-machines-6588439aeeda>

**ANT+:**
- ANT+ device profiles (Bike Power = type 11, FE-C = type 17): <https://www.thisisant.com/developer/ant-plus/device-profiles>
- DC Rainmaker — "Everything you ever wanted to know about ANT+ FE-C and bike trainers": <https://www.dcrainmaker.com/2016/07/everything-wanted-trainers.html>

**Smart bikes & power meters (domain):**
- DC Rainmaker smart-bike shootout (KICKR Bike / NEO Bike / Wattbike Atom): <https://www.dcrainmaker.com/2019/10/wahookickrbike-wattbikeatom-tacxneobike.html>
- Cyclingnews — best smart bikes (gradient %, traits): <https://www.cyclingnews.com/features/best-smart-bikes/>
- Power Meter City — total vs left-only power meters (×2 doubling): <https://powermetercity.com/2016/06/24/total-vs-left-only-power-meters/>
- 4iiii — single- vs dual-sided power: <https://4iiii.com/blog/precision-3-pro-plus-dual-vs-single/>

**Zwift controllers (clean-room, read-don't-copy):** see the full cited list in
[`zwift-controls-research.md`](zwift-controls-research.md) (Zwift Insider, MAKINOLO RC1 write-ups, etc.).

### Unverified / flagged
- **NP / IF / TSS exact formulas** are proprietary (TrainingPeaks/Coggan); only the *concepts* are stated
  here — treat the numbers (e.g. zone boundaries, "20-min × 0.95" FTP) as **commonly-cited conventions,
  not spec**. **(unverified against a primary source.)**
- **Per-bike gradient %, crank-length counts, tilt** for specific smart bikes are from review sites and may
  change by model year/firmware — confirm against the vendor before quoting. **(vendor-specific, unverified.)**
- **CPS field sub-units** (e.g. balance 1/2 %, torque 1/32 N·m, energy kJ) are the SIG-conventional values;
  cross-checked against the parser XML but **not** re-derived from the paid SIG spec — if a decode ever
  disagrees, the **captured frame + the project codec win** (real-data-first).
- The §3 **CSC/erg op-codes and the spoof offsets** are stated at spec level here; the **authoritative
  values for this project are the captured ones** in [`ftms-protocol.md`](ftms-protocol.md),
  [`supported-meters.md`](supported-meters.md), and [`decisions.md`](decisions.md).
