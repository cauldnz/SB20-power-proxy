# Handoff — Start Here

> **⚠️ This doc describes the project's *original* starting point. As of 2026-06-15, Phase 0
> is largely DONE** (5 on-bike sessions; the approach is validated and de-risked). For the
> *current* state, read **[`code/findings/phase-0-report.md`](code/findings/phase-0-report.md)**
> first (synthesis: what's proven, the spoof spec, device IDs, open items, next-steps plan),
> with **`code/findings/decisions.md`** as the full chronological log. The rest of this file is
> historical context for how the project was framed before any captures existed.

You are picking up a research/engineering project to build an ANT+ power-meter proxy for a Stages SB20 spin bike. The goal: feed Favero Assioma pedal data into the bike as if it came from the bike's native (failing/discontinued) Stages crank power meters, so erg-mode resistance control keeps working.

Read this file (for framing). Then `01-project-brief.md` for goals — but treat the open-questions/hypotheses in `01`–`03` as *answered in the Phase 0 report*, not still-open.

## The one-paragraph summary

The owner has a Stages SB20 and Favero Assioma DUO pedals. The SB20's app accepts ANT+ device IDs for its expected cranks; entering the Assioma IDs there does *not* work. We hypothesise that the SB20's pairing flow is doing more than passive listening — it issues calibration commands and validates responses in a way that an Assioma will not satisfy. We need to confirm that hypothesis (Phase 0/1), then design a proxy that subscribes to the Assioma's power broadcast and re-broadcasts the same data on ANT+ in a form the SB20 will accept as its own crank.

## What "done" looks like

A small Python application running on a Raspberry Pi (or laptop) with one or two ANT+ USB sticks attached:

- Subscribes to the Assioma's ANT+ broadcast as a slave/receiver.
- Re-broadcasts the same power and cadence data as a master, advertising as a Stages-style power meter (specific device type, transmission type, channel period, ANT+ ID) such that the SB20 accepts it as its native left crank.
- Responds correctly to any calibration / metadata requests the SB20 issues during pairing.
- Lets the SB20 control resistance based on this proxied power, with latency low enough that erg mode feels responsive (target: <250 ms end-to-end).

Stretch goals: configurable input source (not just Assioma), single-binary install on a Pi, optional BLE path.

## First concrete task: Phase 0

**Do not start writing the proxy.** The first task is diagnostic capture.

In many cases the project owner runs the captures themselves before kicking off Claude Code — see [`START-HERE.md`](START-HERE.md) for the user-facing walkthrough. By the time you read this, there may already be JSONL captures committed under `code/findings/captures/` and a notes file or two. Check first:

```bash
ls code/findings/captures/   # any *.jsonl files? any *-notes.md files?
```

If captures exist, your first task is analysis — start at step 5 below and use the existing data. If they don't exist or are incomplete, your task includes the capture work too.

The full sequence:

0. Read the parent `Research_Content` document — at least the protocols primer and the SB20 section. Skim the python libraries section. This grounds your vocabulary and saves you re-discovering things.
1. Set up the dev environment per `07-hardware-and-environment.md`. Verify openant works against the user's Assioma and against a working Stages crank. Quick smoke test: `openant scan` should find both when they're awake. Optionally bring up the InfluxDB+Grafana stack via `code/docker/docker-compose.yml` (see `09-exploring-captures.md` for the workflow — strongly recommended for analysis).
2. Implement / refine `code/scripts/01_capture_stages.py` — an ANT+ traffic capture that logs every packet from the SB20's left and right cranks during (a) idle, (b) the SB20 pairing flow including zero-reset, (c) steady-state pedalling. Output: timestamped JSONL with full raw payloads and decoded data pages.
3. Implement / refine `code/scripts/02_capture_assioma.py` — same idea but capturing the Assioma broadcast in isolation.
4. Run all capture sessions per `03-central-hypothesis-and-phase-zero.md`. **After Session A, run `code/scripts/00_validate_capture.py --input <jsonl>` and confirm a PASS/REVIEW verdict before continuing** — it gates whether the capture mechanism works at all. **Before Session C, run the Session C-0 ACK dry run** (also in `03-...md`) to confirm inbound/acknowledged traffic is actually captured; if it isn't, hardening the capture script's extended-message support is the first coding task.
5. For each capture, run `04_summarize_capture.py` to produce a markdown summary, and ingest into InfluxDB via `03_ingest_jsonl_to_influx.py` for visual exploration in Grafana.
6. Run `05_diff_captures.py` to produce side-by-side comparisons (Stages L vs Assioma is the headline diff).
7. Synthesise everything into `code/findings/phase-0-report.md`. The diff and summary outputs are committed alongside as supporting evidence; the report is your written analysis on top.

Only after that report exists do we move to Phase 1 (build a static replay) and Phase 2 (build the live proxy).

## What's already in this package

- A complete project brief and success criteria — `01-project-brief.md`
- Background on the SB20, Assioma, ANT+ Bike Power profile, and BLE Cycling Power Service — `02-technical-context.md`
- The central hypothesis and Phase 0 plan — `03-central-hypothesis-and-phase-zero.md`
- A proposed software architecture (treat as v0; revise after Phase 0) — `04-architecture.md`
- A phase-by-phase delivery plan — `05-implementation-phases.md`
- Annotated prior art with links — `06-prior-art-and-references.md`
- Hardware shopping list and dev environment setup — `07-hardware-and-environment.md`
- Known risks and gotchas — `08-risks-and-gotchas.md`
- Capture-analysis pipeline workflow (JSONL → InfluxDB/Grafana → markdown summaries) — `09-exploring-captures.md`
- Strategic position on QZ contribution: build standalone first, decide later — `10-relationship-to-QZ.md`
- Skeleton Python project under `code/` with `pyproject.toml` and stub scripts

## What's not in this package (you'll need to fetch)

- **The parent `Research_Content` document** in the project files. This is the wider fitness-sensor research the owner has pulled together. Read at minimum the "Protocols Primer", "Python Libraries", and "Target Devices → Stages SB20" sections before doing anything else. It covers ANT+/BLE protocol fundamentals, the openant/pycycling/bleak toolkit, and SB20's protocol surface in detail — this package builds on top of that and does not duplicate it.
- The official ANT+ Bicycle Power Device Profile spec (D00001086 Rev 5.0+). Linked in `06-prior-art-and-references.md`. Read sections 7 (Power-Only Sensors), 8 (Crank Torque Sensors), 12 (Common Pages), and 13 (Calibration). This is non-negotiable before writing transmit code.
- **`cagnulein/qdomyos-zwift` (QZ)** — the most substantial open-source project in the same space. GPL-3.0 (so we read but don't copy). Their `CLAUDE.md` is worth skimming early; their device-architecture pattern (a `bluetoothdevice` abstract base + virtual-device targets) validates our `PowerSource`/`PowerTarget` design. They also have working Peloton, Zwift, Wahoo Direct Connect, and Zwift Play implementations relevant to future work — see `06-prior-art-and-references.md`. **Whether this project should eventually live as a contribution to QZ is a strategic question covered in `10-relationship-to-QZ.md` — short answer: build standalone first, decide after Phase 2 with working code in hand.**
- The openant source tree. `pip install openant` and read `openant/devices/power_meter.py` for the receive side. The transmit-master side is less documented; you may need to look at `openant.base` directly and follow the patterns in the prior-art repos linked in section 6 (especially `zwack`, `OpenRowingMonitor`, and `raralabs/pm5-emulator` — which solve the same architectural problem for different devices).

## Style/working notes

- The owner is technical and wants a workable hobby/open-source tool, not a polished product. Optimise for clarity over cleverness.
- Capture artefacts (JSONL logs, diff reports, hypothesis updates) belong under `code/findings/` — committed, time-stamped, never deleted. Future debugging will rely on them.
- When something doesn't work, don't paper over it. Document the failure and propose hypotheses. The owner wants to learn from this, not just have a result.
- Eventual delivery target: a small Raspberry Pi image / Github repo other SB20 owners can use. Keep external dependencies and platform-specific code minimal from day one.

## When in doubt

Re-read `03-central-hypothesis-and-phase-zero.md`. The single biggest failure mode for this project is jumping to "build the proxy" before understanding what the SB20 actually requires. Capture first.
