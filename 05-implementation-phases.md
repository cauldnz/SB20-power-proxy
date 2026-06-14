# 05 — Implementation Phases

Five phases. Each has explicit entry criteria, exit criteria, and an artefact that gets committed. Don't skip phases; don't merge them. The temptation is real — resist it.

## Phase 0 — Diagnostic capture

> **Status (2026-06-15): substantially COMPLETE.** Exit criteria met — capture tooling
> built and hardware-proven across 5 sessions, central hypothesis resolved (H2/H1
> confirmed; SB20 is pass-through), and the synthesis is written:
> [`code/findings/phase-0-report.md`](code/findings/phase-0-report.md). Sessions ran as
> A / C-0 / multi-source / BLE recon (not the literal A–F list below). **Phase 1 (replay)
> is the next milestone.**

**Entry criteria**
- Hardware acquired (one ANT+ stick minimum, two preferred — see `07-hardware-and-environment.md`)
- Dev environment working: `openant scan` finds the SB20's L crank when active

**Work**
- Implement `code/scripts/01_capture_stages.py` (raw + decoded JSONL output)
- Implement `code/scripts/02_capture_assioma.py`
- Run capture sessions A through F per `03-central-hypothesis-and-phase-zero.md`
- Write `findings/phase-0-report.md`

**Exit criteria**
- All capture sessions completed and committed
- Phase 0 report written, with the central hypothesis confirmed or refuted
- "What we need to spoof" table is concrete: device type, transmission type, channel period, manufacturer ID, page mix, calibration response shape
- A go/no-go decision for Phase 1, written down

**Artefact**: `findings/phase-0-report.md`, plus capture JSONL files in `findings/captures/`

**Time budget**: 1–2 evenings of capture; 1 evening of analysis. Realistic 4–8 hours of focussed work.

---

## Phase 1 — Static replay

The goal of Phase 1 is to prove the protocol model is right *before* introducing a live input source. We replay a captured Stages stream from our hardware to the SB20 and check that it accepts us.

**Entry criteria**
- Phase 0 report complete and committed
- Stages cranks isolated (batteries removed) so they don't compete on-air
- A clean L-crank Stages capture is available (Session A from Phase 0)
- Read at least the README of `dhague/vpower` (the closest prior art for "rebroadcasting as an ANT+ Bike Power master from Python on a Pi") and skim `OpenRowingMonitor` (the cleanest open-source "spoof a name-brand fitness device" project, even though it's a different device class). See `06-prior-art-and-references.md`.

**Work**
- Implement `StagesAntTarget` in `src/sb20proxy/targets/stages_ant.py` with:
  - Configurable channel parameters
  - Page rotation logic per ANT+ Bike Power profile
  - Calibration request handling (per Phase 0 findings)
- Implement `ReplayFileSource` in `src/sb20proxy/sources/replay.py` that reads a captured JSONL and emits `PowerReading`s in real-time
- Wire them via `ProxyCore`
- Implement `code/scripts/03_static_replay.py` as the runnable entry point
- In the Stages app, set the SB20 to expect our spoofed device ID (the `target.spoof_device_id` from config)
- Walk through pairing, zero-reset, and pedal-replay
- Verify: the SB20 sees power, accepts the zero-reset, and erg mode reacts to the replayed numbers (use a connected app like Zwift in erg mode, or the Stages app's built-in resistance control)

**Exit criteria**
- The SB20 successfully pairs with our spoofed device
- The SB20 displays power numbers matching the replayed capture
- Erg mode resistance changes when the replayed power changes
- A short demo video (or annotated screenshot sequence) showing this is committed under `findings/phase-1-demo/`

**Artefact**: working `03_static_replay.py` plus the `StagesAntTarget` and `ReplayFileSource` modules; demo evidence under `findings/phase-1-demo/`

**Time budget**: 1–2 weekends. The first attempt will not work; budget for at least one cycle of "pair fails, capture what happened, adjust target params, retry."

**Common failure modes to expect**
- Pairing fails because manufacturer ID is wrong → adjust per Phase 0 findings
- Pairing succeeds but no power displayed → likely page 0x10 encoding issue; check accumulated-power and event-count rollover handling
- Pairing succeeds, power displays, but erg doesn't react → possible smoothing/filtering problem on bike side; possibly latency-related
- Pairing fails after zero-reset → calibration response not accepted; check page 0x01 response payload exactly

---

## Phase 2 — Live proxy with Assioma

Once static replay is reliable, swap the input source for live Assioma data.

**Entry criteria**
- Phase 1 demo working and committed
- Assioma DUO available, charged, and confirmed broadcasting on ANT+

**Work**
- Implement `AssiomaAntSource` in `src/sb20proxy/sources/assioma.py`
  - ANT+ slave channel
  - Decode pages 0x10 (power-only) and 0x12 (crank torque) — Assioma may send either or both
  - Emit `PowerReading`s
- Update `cli.py` / a `04_run_proxy.py` script to wire Assioma source → Stages target
- Test: pair the SB20 to the spoofed device, pedal, verify SB20 power matches Assioma's own broadcast (compare on a Garmin / second app)
- Latency measurement: timestamp Assioma reception and Stages broadcast; report end-to-end latency
- Endurance test: 1-hour ride. Look for drops, channel desyncs, drift, calibration weirdness

**Exit criteria**
- Numbers match between Assioma and SB20-proxied within tolerance (probably ±2 W or 1%)
- End-to-end latency consistently <250 ms (target: <100 ms)
- 1-hour ride completes without intervention
- A `findings/phase-2-report.md` documents latency, accuracy, and any issues

**Artefact**: working `AssiomaAntSource`; `findings/phase-2-report.md`

**Time budget**: 1–2 weekends.

---

## Phase 3 — Robustness, configuration, packaging

Phase 2 produces a working proof of concept. Phase 3 makes it usable by the owner for daily training.

**Entry criteria**
- Phase 2 endurance test passed

**Work**
- Move all hardcoded values to TOML config
- Add a small `--validate-config` mode and clear error messages for common misconfig
- systemd unit file for autostart on a Raspberry Pi
- Logging: rolling JSONL of inputs and outputs to `findings/proxy-runs/` (useful for debugging future issues)
- Recovery: if the Assioma drops out, what happens? Probably: stop broadcasting until it reconnects, so the SB20 sees a clean drop rather than stale numbers
- Recovery: if the SB20 disconnects/reconnects, the master channel should continue gracefully
- Optional: a small status indicator (LED on Pi, or terminal status output)
- Update `code/README.md` with install instructions

**Exit criteria**
- Owner can run a 4-week training block using the proxy without manual intervention beyond starting/stopping
- Recovery from common failure modes is automatic or at worst requires a single restart

**Artefact**: a working "v1" — tagged release, install docs

**Time budget**: 1–2 weekends.

---

## Phase 4 — Distributable

Make it useful to other SB20 owners.

**Entry criteria**
- Phase 3 complete; owner has been using the proxy for 1+ weeks of real training

**Work**
- Generic ANT+ source (`generic_ant.py`) — works with any standard Bike Power meter, not just Assioma
- Optional: BLE input source for owners without ANT+ pedals
- Optional: BLE target path (alternative spoofing route)
- Pi image build using packer or a simple `setup.sh`
- Github README aimed at non-experts: hardware shopping list, install steps, troubleshooting FAQ
- Release notes / version tagging
- Light integration tests against captured data (replay-based, no hardware needed in CI)

**Exit criteria**
- Another SB20 owner can install and use it from the README without further hand-holding (test on at least one volunteer)

**Artefact**: published Github repo, documented Pi image

**Time budget**: open-ended; depends on appetite.

---

## Cross-phase notes

### What goes in `findings/`

Everything captured or derived during diagnostic work. Treat it as append-only history. Examples:

- `findings/captures/` — raw JSONL captures, named with session ID and timestamp
- `findings/phase-0-report.md` (and similar for each phase)
- `findings/decisions.md` — running log of "we decided X because Y", with date stamps
- `findings/phase-1-demo/` — screenshots, video clips, annotated logs

When something breaks in Phase 4 that wasn't broken in Phase 2, the answer is often in Phase 0/1's captures. Don't tidy them up.

### Don't optimise prematurely

Especially in Phases 0–2: write things long-form, verbose, with print statements and obvious data structures. Don't introduce abstractions until the second use case forces them. The architecture in `04-architecture.md` is a *destination*, not a starting point — get something working end-to-end first, refactor toward the architecture later.

### When stuck

The honest debugging path is almost always: capture more, decode more, read the spec more carefully. Avoid the trap of re-running with small changes hoping for different behaviour. Each retry should be motivated by a specific hypothesis derived from a specific captured fact.
