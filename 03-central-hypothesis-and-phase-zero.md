# 03 — Central Hypothesis and Phase 0

> **Status note (2026-06-15):** Phase 0 has now largely run. **Results:** H2 fired
> (Stages `manufacturer_id = 69` vs Favero — entering Assioma IDs failed, so a full
> Stages spoof is required, not just ID-matching); Session C-0 PASSED (calibration
> handshake captured); the SB20 does **not** rescale crank power. Read this doc for the
> *plan/reasoning*, but for *what was found* see
> **[`code/findings/phase-0-report.md`](code/findings/phase-0-report.md)**. (Capture
> filenames below use an old `stages-L-steady-NNNN` form; actual files are
> `A-stagesL-steady-…` — see `code/findings/captures/README.md`.)

This is the most important document in the package. Every architectural choice depends on Phase 0 confirming or refuting what's below.

## The central hypothesis

> Entering an Assioma's ANT+ ID into the SB20's "Power meters" field fails not because of the ID itself, but because the SB20's pairing flow exchanges more than passive broadcast packets — it issues calibration and metadata requests that an Assioma either does not answer in the way the SB20 expects, or answers with field values (manufacturer ID, model number, etc.) that the SB20 rejects.

Three sub-hypotheses, in order of likelihood:

### H1 — Calibration handshake mismatch (likely, but see refinement)
The SB20's pairing finishes with a mandatory zero-reset that issues a page 0x01 calibration request and waits for a specific 0x01 response. The SB20 may reject the Assioma either because the response never comes, or because it comes in a form the SB20 doesn't accept.

**Refinement (added on review):** Assioma pedals *do* respond to standard ANT+ manual zero-offset calibration requests — a Garmin Edge's "Calibrate" button works on them. So the "Assioma stays silent" failure mode is unlikely. The more probable H1 variant is that the Assioma responds, but with a payload the SB20 treats as a failure: a different success/calibration-ID byte, an offset value outside the range the SB20 expects, or different timing/sequencing than a Stages crank's reply. This makes H1 and H2 closer in likelihood than originally ranked — do not assume Phase 0 will simply show "no response." Sessions C (Stages calibration handshake) and E (Assioma calibration response) are what actually settle this; capture both before drawing conclusions.

### H2 — Common Pages metadata validation (more likely than first assumed)
The SB20 reads pages 0x50 (Manufacturer ID) and 0x51 (Product Information) and validates that the manufacturer ID is the Stages value (`0x45` / decimal 69 — to be confirmed). An Assioma will report the Favero manufacturer ID and the SB20 may reject it. Given the H1 refinement above, treat H2 as roughly co-equal with H1 rather than a clear second. The page 0x50 manufacturer_id diff (Session A vs Session D) is a one-line smoking-gun test for this.

### H3 — Channel parameter mismatch
The SB20 expects a channel period or transmission type byte that the Assioma doesn't use, and silently fails to establish a stable channel. (Less likely because the device IDs would also fail to be found at all in this case, and the user reports that the Assioma is at least visible.)

The original ranking put H1 well ahead because the Stages docs explicitly require the zero-reset step *as part of pairing* and describe receiving "a zero offset value from the left and right power meters" — strongly suggesting the bike actively solicits a response. That reasoning still holds for *why a calibration exchange matters*; what's changed on review is the recognition that the Assioma almost certainly *does* respond, shifting the question from "does it respond?" to "does the SB20 accept the response and the metadata?". Either way:

The hypothesis is testable. Phase 0 tests it.

## Why we cannot skip Phase 0

The shape of the proxy depends on the answer:

- If H1 is correct, the proxy must handle calibration requests faithfully, but otherwise we have a lot of flexibility — we can use any ANT+ ID, any device, any input source. This is the *easy* outcome.
- If H2 is correct, we must spoof the manufacturer ID to Stages's value. This is straightforward but raises a small ethical/legal flag worth flagging in the README (we are presenting another maker's identity to a device on a private network — low risk for personal interoperability with hardware you own; see `08-risks-and-gotchas.md` §"Ethical / legal flags", which now reflects that the Stages brand is owned by Giant/SPIA rather than defunct).
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
- Linux laptop or Raspberry Pi with `openant` installed (`pip install openant`; for non-root USB access write a udev rule for vendor `0x0fcf` — see `07-hardware-and-environment.md`. Note: openant's `python -m openant.udev_rules` helper is not pip-shipped and errors out).
- The SB20 fully assembled, paired, and working with its native cranks.
- The Assioma DUO charged and woken (rotate cranks to wake).

A second ANT+ stick is *very* useful for capturing pairing handshakes — see the next section.

### Capture sessions

> **Optional, any pedalling session:** run `06_capture_ble.py` in a second terminal (native Windows — WSL2 has no Bluetooth) to passively capture the same meter's BLE Cycling Power side in parallel. Costs nothing on the bike, and builds the BLE protocol model a future BLE-path target (QZ contribution, ESP32 — which cannot do ANT+) would need. Passive only: the bike stays ANT+-paired; the "Pair with Bluetooth" toggle remains Session G's job. See `START-HERE.md` §"Optional: parallel BLE capture".

#### Session A — Stages L crank, steady state (15 min)
- Pair the Assioma's ANT+ ID is *not* relevant here. We're snooping the existing, working SB20 setup.
- Start `01_capture_stages.py` with the Stages L crank's ANT+ ID.
- Pedal at varied power levels (sit on bike, ride 5 minutes covering low/medium/high power).
- Stop the script. Output: `findings/captures/stages-L-steady-NNNN.jsonl`.

#### Session B — Stages R crank, steady state (10 min)
- Same as A, but capturing the R crank's independent broadcast (its "half power" mode).
- Important for understanding what the R crank looks like on-air, even though we may not need to spoof it.

#### Session C-0 — ACK-capture dry run (do this BEFORE Session C)

The entire value of Session C rests on an unproven assumption: that our capture script can actually *see* the acknowledged messages the SB20 sends back to the crank during pairing/zero-reset. A standard ANT+ slave subscribed to the crank may not see those directed messages at all, depending on whether extended messages are enabled and how the SB20 addresses them. Do not discover this during the careful, annotated, hard-to-repeat Session C.

Instead, run a cheap dry run first, on the **working** (Stages-paired) setup:

1. Start `01_capture_stages.py` on the Stages L crank's ID for a short window (e.g. `--duration 180`).
2. While it runs, trigger a single zero-reset from the Stages app (cranks vertical, tap zero).
3. Stop the capture and grep the JSONL for any record decoding to **calibration page 0x01** — `grep '"page": 1' <file>` or look for `"page_hex": "0x01"`. **Expect it as `"kind": "broadcast"`, not `"kind": "acknowledged"`** (see the note below on why).

**What you are actually looking for, and why.** There are two different messages in a zero-reset, and they are not equally capturable by a passive sniffer:

- The **SB20 → crank calibration *request*** (page 0x01, ID 0xAA) is an *acknowledged uplink* sent by the SB20, which is itself an ANT+ *slave* to the crank (the master). A second passive slave — our capture stick — generally **cannot** see another slave's uplink to the master. So do **not** expect a `"kind": "acknowledged"` record for the bike's request; its absence is normal and not a failure. (`on_acknowledge_data` is still wired as cheap insurance in case the master ever directs an ACK at us, and extended messages are enabled — but neither makes the uplink appear.)
- The **crank → calibration *response*** (page 0x01, ID 0xAC + 16-bit offset) is what the Bike Power profile sends as an **interleaved broadcast page** in the crank's normal stream. *This* we do capture, and it is the high-value artefact — it is literally the reply our proxy will have to mimic. It arrives as `"kind": "broadcast"` with `page == 1`.

Outcome:
- **If a page 0x01 broadcast appears when you trigger the zero-reset** → the capture method can see the calibration exchange we care about; proceed to Session C with confidence.
- **If no page 0x01 appears at all (only 0x10/0x50/0x51/0x52)** → we are not seeing the calibration response. Investigate before Session C: confirm the zero-reset actually fired in the app, confirm `ext_messages` shows `enabled=True` in the JSONL, check the crank battery, and try `--log-channel-events` to see whether the channel dropped during the reset. If the response genuinely can't be captured this way, the fallback for the *request* bytes is a true sniffer/observer (Nordic nRF52840 + ANT sniffer firmware) rather than a stock stick — but note that for the *proxy* we only strictly need the response, which a stock stick should see. Capturing Session C with a method that can't see the page 0x01 response wastes the most important session in Phase 0.

This is also the natural first concrete task to hand to Claude Code if the capture script needs to be hardened before any real session is run.

#### Session C — Stages L crank, full pairing + zero-reset (CRITICAL)
- This is the highest-information capture. Before starting:
  - In the Stages app, *unpair* the L crank (clear its ANT+ ID).
  - Have `01_capture_stages.py` running in promiscuous/broadcast-snoop mode if openant supports it. If not, run two parallel captures: one slave-subscribed to the L crank's ID, and one to the SB20's FE-C broadcast — though the actually interesting messages are the SB20-to-crank acknowledged messages, which are best captured by enabling "extended messages" mode on a stick configured as a slave to the crank.
  - Actually the cleanest method: run a slave channel paired to the L crank's ID with extended messages enabled (so we see both broadcast and acknowledged traffic on that channel). **De-risk this before the careful Session C run** — see "Session C-0" below. The `pirower` fork is sometimes cited for extended-messages support, but it lives on GitLab, was last touched around 2020, and targets a pre-1.0 openant tree (from before the `ant`→`openant` rename); do **not** expect it to apply cleanly to openant 1.3.x. Prefer enabling extended messages directly via openant's lower-level message API (send the 0x66 "Enable Extended Messages" config message before opening the channel), and treat the fork as reference reading only.
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

#### Session G — BLE-paired cranks (PLANNED — promoted from optional, 2026-06-10)

Purpose: open or close the door on a BLE-side spoofing path. Promoted from optional to a planned Phase-0 session because the owner has named **ESP32 as a real deployment target** — and ESP32 has no ANT+ radio, so an ESP32 implementation *requires* the bike to pair its cranks over BLE and us to impersonate a BLE Cycling Power server. The same path is technically necessary for any QZ contribution (BLE-only on mobile). See `10-relationship-to-QZ.md` for context. Sequencing: run G **after** the ANT+ sessions A–F are captured and validated, so the working ANT+ baseline is never disturbed mid-stream.

The phase-0 report must therefore answer two extra questions: does erg mode work fully with BLE-paired cranks, and what does the BLE pairing/calibration handshake (Cycling Power Control Point 0x2A66) look like?

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
- (Session G — now planned) a subsection on BLE-side feasibility — what the BLE crank-bike conversation looks like, whether erg mode works with BLE-paired cranks, and whether `StagesBleTarget` (and hence the ESP32 target and/or a future QZ contribution) appears feasible

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
