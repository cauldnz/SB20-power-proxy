# 03 — Central Hypothesis and Phase 0

This is the most important document in the package. Every architectural choice depends on Phase 0 confirming or refuting what's below.

## The central hypothesis

> Entering an Assioma's ANT+ ID into the SB20's "Power meters" field fails not because of the ID itself, but because the SB20's pairing flow exchanges more than passive broadcast packets — it issues calibration and metadata requests that an Assioma either does not answer in the way the SB20 expects, or answers with field values (manufacturer ID, model number, etc.) that the SB20 rejects.

Three sub-hypotheses, in order of likelihood:

### H1 — Calibration handshake mismatch (most likely)
The SB20's pairing finishes with a mandatory zero-reset that issues a page 0x01 calibration request and waits for a specific 0x01 response. Whether the Assioma responds at all depends on its own firmware behaviour with non-Favero-app calibration commands. Even if it does respond, the response payload format may differ enough (e.g., offset value range, success-ID byte) that the SB20 errors out and treats pairing as failed.

### H2 — Common Pages metadata validation
The SB20 reads pages 0x50 (Manufacturer ID) and 0x51 (Product Information) and validates that the manufacturer ID is the Stages value (`0x45` / decimal 69 — to be confirmed). An Assioma will report the Favero manufacturer ID and the SB20 rejects it.

### H3 — Channel parameter mismatch
The SB20 expects a channel period or transmission type byte that the Assioma doesn't use, and silently fails to establish a stable channel. (Less likely because the device IDs would also fail to be found at all in this case, and the user reports that the Assioma is at least visible.)

H1 is most likely because the Stages docs explicitly require the zero-reset step *as part of pairing* and describe it as "you should receive a zero offset value from the left and right power meters" — strongly suggesting the bike is actively soliciting a response.

The hypothesis is testable. Phase 0 tests it.

## Why we cannot skip Phase 0

The shape of the proxy depends on the answer:

- If H1 is correct, the proxy must handle calibration requests faithfully, but otherwise we have a lot of flexibility — we can use any ANT+ ID, any device, any input source. This is the *easy* outcome.
- If H2 is correct, we must spoof the manufacturer ID to Stages's value. This is straightforward but raises a small ethical/legal flag worth flagging in the README (we are misrepresenting manufacturer to a device whose maker is bankrupt — low risk, but worth being explicit).
- If H3 is correct, we have to match channel parameters exactly. Probably also fine but requires more care.
- If **none** of the above is the actual cause — for instance, if the SB20 has a hardcoded list of Stages-issued IDs, or runs a proprietary handshake we haven't anticipated — the project may need to fall back to a different approach (BLE-side spoofing, or modifying the Stages app's behaviour). Better to find that out in Phase 0 than after writing the proxy.

The cost of Phase 0 is small — a few hours of capture and analysis. The cost of skipping it is potentially weeks of misdirected coding.

## Phase 0 plan

### Goals

1. Confirm or refute H1, H2, H3.
2. Produce a complete protocol model of "what does the SB20 require from a thing pretending to be a Stages crank?"
3. Produce a complete protocol model of "what does the Assioma actually emit?"
4. Diff them. Identify the spoofing surface.

### Setup

- One ANT+ USB stick (Garmin/Dynastream ANTUSB-m or ANTUSB2; CYCPLUS is also reported to work).
- Linux laptop or Raspberry Pi with `openant` installed (`pip install openant`; udev rules via `sudo python -m openant.udev_rules` on Linux).
- The SB20 fully assembled, paired, and working with its native cranks.
- The Assioma DUO charged and woken (rotate cranks to wake).

A second ANT+ stick is *very* useful for capturing pairing handshakes — see the next section.

### Capture sessions

#### Session A — Stages L crank, steady state (15 min)
- Pair the Assioma's ANT+ ID is *not* relevant here. We're snooping the existing, working SB20 setup.
- Start `01_capture_stages.py` with the Stages L crank's ANT+ ID.
- Pedal at varied power levels (sit on bike, ride 5 minutes covering low/medium/high power).
- Stop the script. Output: `findings/captures/stages-L-steady-NNNN.jsonl`.

#### Session B — Stages R crank, steady state (10 min)
- Same as A, but capturing the R crank's independent broadcast (its "half power" mode).
- Important for understanding what the R crank looks like on-air, even though we may not need to spoof it.

#### Session C — Stages L crank, full pairing + zero-reset (CRITICAL)
- This is the highest-information capture. Before starting:
  - In the Stages app, *unpair* the L crank (clear its ANT+ ID).
  - Have `01_capture_stages.py` running in promiscuous/broadcast-snoop mode if openant supports it. If not, run two parallel captures: one slave-subscribed to the L crank's ID, and one to the SB20's FE-C broadcast — though the actually interesting messages are the SB20-to-crank acknowledged messages, which are best captured by enabling "extended messages" mode on a stick configured as a slave to the crank.
  - Actually the cleanest method: run a slave channel paired to the L crank's ID with extended messages enabled (so we see both broadcast and acknowledged traffic on that channel). See the `pirower/openant` fork referenced in `06-prior-art-and-references.md` for extended-messages support, or implement extended messages directly using openant's lower-level APIs.
- Then: re-enter the L crank ID in the Stages app, walk through the pairing flow, perform the zero-reset, pedal a bit. Save the capture. Annotate the JSONL with timestamps for "pairing initiated", "pairing complete", "zero-reset started", "zero-reset complete", "pedalling started".
- Output: `findings/captures/stages-L-pairing-NNNN.jsonl`.

#### Session D — Assioma DUO, steady state (15 min)
- Mount Assioma on a different bike (or the SB20 with batteries removed from Stages cranks).
- Capture with `02_capture_assioma.py`.
- Pedal at varied power levels.
- Output: `findings/captures/assioma-steady-NNNN.jsonl`.

#### Session E — Assioma DUO, calibration triggered from a head unit
- Trigger a manual calibration / zero-offset from a Garmin head unit (or any ANT+ display that supports it).
- Capture how the Assioma responds.
- Output: `findings/captures/assioma-calibration-NNNN.jsonl`.

#### Session F — Failure mode capture (the actual reported failure)
- In the Stages app, enter the **Assioma's** ANT+ ID as the L crank ID. (This is the failure the owner originally reported.)
- Run `01_capture_stages.py` listening on that ID, plus capture any ACK traffic.
- Watch what happens. Does the Assioma get sent anything? Does the SB20 give up after a timeout? Does it produce an error message?
- Output: `findings/captures/failure-mode-NNNN.jsonl` plus a written description of what the SB20 app showed.

#### Session G — BLE-paired cranks (optional but recommended)

Purpose: open or close the door on a BLE-side spoofing path, which is the technically necessary route if we ever want this work to be a contribution to QZ (which is BLE-only on mobile). See `09-relationship-to-QZ.md` for context.

- In the Stages app: Power Meters page → toggle **"Pair with Bluetooth"** ON.
- The bike will switch from ANT+-paired cranks to BLE-paired cranks. At this point the cranks are talking to the bike's internal computer over BLE Cycling Power Service rather than ANT+ Bike Power profile.
- Use a BLE sniffer or `bleak`/`pycycling`-based capture script to log:
  - The advertising packet from the L crank (manufacturer data, service UUIDs)
  - The full sequence of GATT operations during pairing (service discovery, characteristic reads, CCCD writes, calibration via Cycling Power Control Point 0x2A66)
  - The Cycling Power Measurement (0x2A63) notifications during steady-state pedalling
- Output: `findings/captures/G-stagesL-ble-NNNN.jsonl`. Plus a written summary covering:
  - Does the bike pair successfully over BLE?
  - Does erg mode still work when cranks are BLE-paired?
  - Is the calibration handshake similar to or different from the ANT+ version?
  - What does the Cycling Power Measurement payload look like (flags field, optional fields populated)?

Don't write spoofing code for BLE in Phase 0; just capture. The captures inform whether Phase 3+ should add `StagesBleTarget` and whether QZ integration is technically viable.

Note: Hard BLE sniffing (decoding all the link-layer traffic) requires either macOS with PacketLogger, a proper sniffer dongle (Nordic nRF52 sniffer, Ellisys, or Frontline), or Linux with btmon. Soft sniffing — running a BLE *central* using bleak that subscribes to the same characteristics the bike does — is much easier and probably enough for Phase 0. Document which method you used.

### Analysis: the diff

After all sessions, compare:

1. **Channel parameters**: device type, transmission type, channel period — same?
2. **Page mix**: what proportion of 0x10 / 0x12 / 0x50 / 0x51 / 0x52 / others does each emit?
3. **Common Page contents**:
   - Page 0x50 manufacturer ID — Stages's vs Favero's
   - Page 0x51 product/HW/SW info
   - Page 0x52 battery descriptor
4. **Calibration**:
   - Does the SB20 send 0x01 calibration requests? When?
   - What does the Stages crank reply look like? (success ID, offset value, etc.)
   - What does the Assioma reply look like in Session E?
5. **Pairing handshake unique to Stages**:
   - Are there any other acknowledged-message exchanges during pairing that aren't standard ANT+ Bike Power?
   - This is the highest-risk discovery — if there's something proprietary, the project complexity goes up.

### Phase 0 deliverable

A markdown document at `findings/phase-0-report.md` that, at minimum, contains:

- One-paragraph executive summary: what is the SB20 actually checking?
- A table of channel parameters: Stages vs Assioma vs "what we need to emit"
- A table of common-page values: Stages vs Assioma vs "what we need to emit"
- A timeline of the pairing handshake including any non-standard messages
- A go/no-go recommendation for Phase 1, with revised architecture if needed
- An updated version of the "open questions" list from `01-project-brief.md`
- (If Session G ran) a short subsection on BLE-side feasibility — what the BLE crank-bike conversation looks like, and whether `StagesBleTarget` (and hence a future QZ contribution) appears feasible

This report drives the rest of the project. Don't write Phase 1 code until it's written.

The report should incorporate:
- The output of `05_diff_captures.py --left A-stagesL... --right D-assioma...` as the central comparison.
- Per-capture summaries from `04_summarize_capture.py` as supporting evidence.
- Optionally: Grafana screenshots from the dashboards in `code/grafana/dashboards/` if a visual is clearer than a table.

See `09-exploring-captures.md` for the full pipeline.

## Risks specific to Phase 0

- **The Stages cranks may already be partially failing.** If captures look weird, consider that one source of weirdness is the cranks themselves rather than the protocol. Check battery voltages and try a fresh CR2032.
- **Two power meters near each other can cause channel collisions in capture.** During Session A, you may see noise from the R crank or from the SB20's own re-broadcast. Use the device-ID filter to scope captures; consider removing the R crank's battery during Session A.
- **`openant`'s default queue settings drop messages above ~15 Hz.** If captures look sparse, see the linked thread in `06-prior-art-and-references.md` about modifying `_worker` and `_main` queue handling for higher rates. For 4 Hz default broadcasts this won't bite, but for any high-rate streams it will.
- **Extended-message mode is needed to see channel ID metadata on incoming messages.** Stock openant slave channels don't enable this by default. The `pirower/openant` fork adds a flag; alternatively, set 0x66 (Enable Extended Messages) directly via openant's lower-level message-send API before opening the channel.
