# 02 — Technical Context

> **Status note (2026-06-15):** Phase 0 capture is largely complete. Several
> **[HYPOTHESIS]/[UNKNOWN]** items below are now **confirmed** and not re-tagged
> inline — for the resolved values (manufacturer_id **69**, emitted pages
> **0x10/0x12/0x13 + commons**, channel period **8182**, calibration response
> **page 0x01 / 0xAC / offset**, single-sided Unified-channel-L source, BLE
> topology), read **[`code/findings/phase-0-report.md`](code/findings/phase-0-report.md)**
> §1–§4. This doc is retained as the original background.

This document collects what we currently believe about the systems involved. Items marked **[CONFIRMED]** are from official documentation or direct inspection. Items marked **[HYPOTHESIS]** are inferences requiring validation in Phase 0. Items marked **[UNKNOWN]** are gaps to fill.

## SB20 architecture

### The SB20 plays two roles simultaneously

Important to keep these straight, because they live on different protocols and we touch one but not the other:

| Role | What it broadcasts | Used for | Our project's relationship |
|------|--------------------|----------|---------------------------|
| **Power Meter** | ANT+ Bike Power (0x0B), BLE Cycling Power Service (0x1818), BLE CSCS (0x1816) | Apps reading "current power" | We don't touch this. The bike's PM broadcast just re-emits whatever its internal-computer thinks is current power. By feeding the bike different power numbers via spoofed cranks, we change what it re-broadcasts. |
| **Trainer** | ANT+ FE-C (0x11), BLE FTMS (0x1826) | Apps controlling resistance/erg/sim | We don't touch this either. External apps (Zwift, TrainerRoad, Stages app) send resistance commands to this. The bike's internal control loop closes the loop using the power numbers it thinks are current — which become our spoofed numbers. |

What we *do* touch is **the channel between the L crank and the bike's internal computer**. That's the layer below both of the above. By replacing the L crank's broadcast with our spoofed broadcast, we control what the bike thinks current power is, and the existing FE-C/FTMS/CPS/Bike-Power broadcasts continue unchanged from the outside world's perspective.

### Hardware
- Smart spin bike with electronically-controlled flywheel resistance.
- Two onboard Stages crank power meters (L and R), each independently powered by a CR2032 coin cell.
- Internal computer housed in the phone-holder unit; this is the brain that:
  - Pairs with the L and R cranks
  - Combines power data
  - Re-broadcasts as a single ANT+/BLE power source
  - Acts as an FE-C controllable trainer for external apps
  - Runs erg/level/sim resistance control loops

### Firmware version sensitivity

Stages firmware 3.7.0+ moved workout scaling out of the bike firmware into the Stages Cycling app. We don't know yet whether other 3.7.0+ changes affect the crank-pairing protocol, so during Phase 0 record the bike's firmware version (visible in the Stages app) alongside every capture. If results look weird, "what firmware are you on?" is the first diagnostic question to ask.

### Power-meter pairing flow [CONFIRMED]
From the official Stages documentation (`Pair a replacement power meter to a Stages SB20 smart bike`):

1. User opens the Stages Cycling app, connects to the bike.
2. App shows a "Power meters" tab; user enters L and R crank ANT+ IDs (5-digit numbers from stickers).
3. App initiates pairing — bike disconnects/reconnects from the app during this (≥ 30 s).
4. Once paired, user performs a **zero reset**: cranks vertical, tap zero-reset button.
5. Bike returns zero-offset values from L and R cranks. *This is the important step* — it's a calibration request/response, not passive listening.
6. User pedals; power and cadence display in the Data tab.

### Data flow at runtime [CONFIRMED]
> "The left power meter measures the power from your left leg, combines it with the right side's power, and sends that as the total power over ANT+ and Bluetooth … The smart bike pairs with the power meters and rebroadcasts the same information as the left power meter."

So: R crank → (ANT+) → L crank → combines → broadcasts as combined → (ANT+) → bike's internal computer → re-broadcasts. The R crank also broadcasts independently but Stages explicitly say "Do not ever pair to this sensor. It will only send half power" — meaning the R crank's standalone broadcast is half-power-only, used by Zwift etc. only as a backup/diagnostic source.

This implies that **for our purposes the spoofing target is the L crank's combined broadcast.** The R crank's broadcast may not need to be spoofed at all if the bike will accept "L only" as combined.

### Single-sided mode [CONFIRMED]
From Stages docs: there is an explicit "Riding the SB20 with left or right only power" mode, triggered from the app:

> "Use the Stages Cycling app to connect to the functioning power meter in the Devices tab. Once connected to the power meter, select Left/Right pairing. Select Unpair. This will make the power meter into a single-sided power meter. To ensure that the bike does not re-pair the 2 power meters back together, remove the battery from the non-working power meter while waiting for the replacement."

**This is a major architectural simplification if it works for our case.** It means:
- We may only need to spoof one ANT+ device, not two.
- The SB20 has a documented mode for running on one power meter.
- Battery-out on the spoofed-side is the official isolation method.

[HYPOTHESIS] We can use this mode to require only a single spoofed-broadcast from the proxy.

### Public broadcasts from the SB20 itself [CONFIRMED]
- ANT+: appears as `FEC ####` (FE-C trainer, controllable) where #### is the last 4 digits of the bike's serial.
- BLE: appears as `Stages Bike ####`.
- The bike re-broadcasts power "with no smoothing", so external apps see the same numbers as if they paired to the L crank directly.

### Resistance control [CONFIRMED]
- External apps can issue FE-C resistance/erg/sim commands over ANT+ to the bike.
- The bike's internal control loop uses the L crank's reported power to close the erg loop.
- **Therefore, if we can spoof the L crank, we control what the SB20 thinks "current power" is, and erg mode will react to our number.** This is the keystone of the whole project.

## Stages crank protocol [HYPOTHESIS, partly]

### What we know
- Stages cranks are standard ANT+ Bike Power devices (device type 0x0B).
- They support BLE Cycling Power Service (UUID 0x1818) as well, though the SB20 pairs them by default over ANT+.
- The 5-digit ID printed on the sticker is the ANT+ device number.
- L and R cranks pair to each other (so R sends to L, L combines and broadcasts).
- They support manual zero-offset / calibration.

### What we don't know yet [PHASE 0 RESOLVES]
- Channel period (likely 8182 = 4 Hz, but Stages may use higher).
- Transmission type byte (possibly encodes side/pairing info).
- Which data pages are emitted: definitely 0x10 (Power-Only) and Common Pages 0x50/0x51/0x52; possibly also 0x12 (Crank Torque) and 0x13 (Torque Effectiveness/Pedal Smoothness), 0x20 (CTF) is unlikely for strain-gauge cranks.
- What the calibration response (page 0x01) looks like specifically.
- Whether the SB20 issues any non-standard ANT+ requests during pairing.

## ANT+ Bike Power Profile primer

Official spec: D00001086 Rev 5.x ("ANT+ Device Profile — Bicycle Power"), available from `thisisant.com` / linked in `06-prior-art-and-references.md`.

### Channel parameters for a power meter (master)
- Channel type: bidirectional master (0x10) — bidirectional because the meter must accept calibration/control requests from the head unit.
- Network key: ANT+ network key (proprietary; available to ANT+-licensed developers and present in standard libs like openant).
- RF frequency: 57 (2457 MHz).
- Device type: 0x0B (11) — Bicycle Power.
- Device number: 1–65535 (the "ANT+ ID"), 16 bits.
- Transmission type: typically 5 (independent transmission); high nibble can extend device number to 20 bits but rarely used.
- Channel period: 8182 counts (4.00 Hz default). Some meters use 4091 (8 Hz) or 2730 (12 Hz).

### Main data pages
| Page | Name | Content | Notes |
|------|------|---------|-------|
| 0x10 | Standard Power-Only | Event count, pedal power balance, cadence, accumulated power, instantaneous power | The minimum every meter sends |
| 0x11 | Standard Wheel Torque | Wheel-based meters (PowerTap hubs etc.) | Not relevant for cranks |
| 0x12 | Standard Crank Torque | Crank-based torque + cadence | Stages cranks likely send this |
| 0x13 | Torque Effectiveness & Pedal Smoothness | TE/PS metrics | Optional |
| 0x20 | Crank Torque Frequency | CTF meters (SRM-style) | Not for Stages |

### Common pages (cross-profile)
- 0x50 — Manufacturer's Identification (manufacturer ID, model number)
- 0x51 — Product Information (HW/SW revisions, serial number)
- 0x52 — Battery Status
- 0x01 — Calibration Request/Response
  - Request: head unit sends 0x01 to meter to trigger zero offset
  - Response: meter sends 0x01 back with success/failure and offset value

These are typically interleaved into the broadcast schedule (e.g., 0x10 most messages, with 0x50/0x51/0x52 inserted every 65 messages per the spec's recommendations).

### Calibration flow
This is the part most likely to bite us:

1. Head unit (display, or in our case the SB20) sends a calibration request (page 0x01, ID 0xAA = manual zero) as an acknowledged message.
2. Meter responds with page 0x01 in its broadcast slot, with calibration success ID (0xAC for success, 0xAF for failure) and the offset value as 16-bit signed.

If the SB20 sends 0xAA and waits for an 0xAC response with a sensible offset, our spoofed broadcaster needs to handle this. Assioma's own calibration response uses a different (but spec-compliant) approach. Whether the SB20 cares about specifics beyond "got an 0xAC back" is a Phase 0 question.

## BLE Cycling Power Service primer

UUID 0x1818. Used by Assioma (and Stages) as an alternative to ANT+. Likely path for fallback / dual-protocol support.

The parent `Research_Content` document covers CPS in detail (variable-length flags, timestamp resolution gotchas, Garmin watches requiring CSCS alongside CPS). Read that section rather than re-reading what's below if you have access.

### Key characteristics
| UUID | Name | Direction | Notes |
|------|------|-----------|-------|
| 0x2A63 | Cycling Power Measurement | Notify | Power, cadence, optional pedal balance — broadcast to subscribers |
| 0x2A65 | Cycling Power Feature | Read | Bitfield of supported features |
| 0x2A5D | Sensor Location | Read | Where on the bike (left crank, right pedal etc.) |
| 0x2A66 | Cycling Power Control Point | Write/Indicate | Calibration, sensor reset, etc. |
| 0x2A64 | Cycling Power Vector | Notify (optional) | Per-revolution magnitude/angle data |

The Cycling Power Measurement payload is variable-length depending on the flags. Minimum is 4 bytes: 16-bit flags + 16-bit instantaneous power. Common extensions add cumulative crank revolutions + last crank event time (for cadence).

For the SB20: it has a "Pair with Bluetooth" toggle in the Stages app. Confirmed that BLE pairing exists between the cranks and the bike, but ANT+ is the default. BLE-side spoofing is probably more brittle (single connection at a time, no "wildcard" broadcasts) but worth keeping in mind as an alternative if ANT+ proves resistant.

## Assioma architecture [CONFIRMED]

- Favero Assioma DUO: dual-sided pedal-based power meter.
- Standard ANT+ Bike Power profile (device type 0x0B).
- Each pedal has its own 5-digit ANT+ ID; the **left pedal** transmits combined L+R data over ANT+ (similar to Stages cranks).
- For ANT+, you only pair the left pedal's ID; L sends both sides' data.
- For BLE, dual mode is configurable via the Favero Assioma app — defaults to "unified" (combined transmission via left), with an option for "dual-channel L/R".
- Self-calibrates while pedalling; manual calibration also available.
- Sends Cycling Dynamics (PCO, PP, TE, PS, RP) over ANT+ when paired with capable head units.

### Implication
On the input side of our proxy, we subscribe to the Assioma in its standard "unified left" mode and treat it as a normal slave-receiver subscription. No special configuration needed.

## What "Phase 0 capture" must observe

To inform the architecture, the Phase 0 captures need to record:

For the **Stages L crank** (steady state):
- Channel period (inferred from inter-message timing)
- Device type, transmission type
- All data pages emitted, and at what proportional rate
- Specifically: 0x10 / 0x12 / 0x50 / 0x51 / 0x52 / others
- Field values (manufacturer ID = ?, model = ?, HW/SW versions, serial number format)

For the **Stages L crank** (during pairing + zero-reset):
- Any incoming acknowledged messages from the SB20 (these are the questions the bike asks the meter)
- The crank's responses
- Total duration of the handshake
- Page 0x01 calibration request/response payloads in detail

For the **Stages R crank**:
- Same as above; key question is whether the R crank's "half-power broadcast" looks like a normal power meter or differs in some flagging

For the **Assioma**:
- Same set of measurements, so we can diff them against Stages

The decoded captures in JSONL are then turned into a written report `findings/phase-0-report.md` listing:
- Differences that matter (need spoofing)
- Differences that don't matter (cosmetic / metadata only)
- Open questions for Phase 1
