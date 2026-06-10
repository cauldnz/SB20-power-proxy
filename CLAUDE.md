# CLAUDE.md

Project metadata for [Claude Code](https://claude.ai/code) and similar tools.

## What this project is

An ANT+ man-in-the-middle proxy that lets a Stages SB20 smart bike consume power data from a third-party power meter (primary target: Favero Assioma) as if it came from the bike's native Stages crank power meters. See `README.md` and `01-project-brief.md`.

## Where to start

**Read in this order before doing anything:**

1. `HANDOFF.md` — your role, the first task, and what's not in this package
2. `01-project-brief.md` — goals and success criteria
3. `03-central-hypothesis-and-phase-zero.md` — **most important** — why we don't write proxy code yet
4. `02-technical-context.md` — background on SB20, ANT+ Bike Power, BLE Cycling Power
5. The parent `Research_Content` document in the project files (broader fitness-sensor research)

Then as needed: `04-architecture.md`, `05-implementation-phases.md`, `06-prior-art-and-references.md`, `07-hardware-and-environment.md`, `08-risks-and-gotchas.md`, `09-exploring-captures.md`, `10-relationship-to-QZ.md`.

## Build / install / run

This is a Python project. Layout:

```
code/
├── pyproject.toml               # openant + optional [dev], [ble], [analysis]
├── scripts/                     # one-off scripts (capture, ingest, summarise, diff)
├── src/sb20proxy/               # the actual proxy library — sources/ targets/
├── findings/                    # committed history of captures + analyses
├── docker/                      # InfluxDB + Grafana stack for visualisation
└── grafana/                     # Grafana provisioning + dashboards
```

Setup (Linux / WSL Ubuntu):

```bash
cd code
python3 -m venv .venv && source .venv/bin/activate
pip install -e ".[dev,analysis]"
# udev rule for ANT+ sticks — write directly; openant's `udev_rules` helper
# copies from a relative resources/ path that pip doesn't ship, so it errors.
sudo tee /etc/udev/rules.d/42-ant-usb-sticks.rules >/dev/null <<'RULE'
SUBSYSTEM=="usb", ATTRS{idVendor}=="0fcf", MODE="0666"
RULE
sudo udevadm control --reload-rules && sudo udevadm trigger   # then unplug+replug stick
```

For Windows users, USB passthrough to WSL is required — see `START-HERE.md` and `07-hardware-and-environment.md` §"Windows + WSL".

## Engineering disciplines

A few invariants that this project's success depends on:

- **Capture before code.** Phase 0 (diagnostic capture) is mandatory; the proxy's architecture depends on what it reveals. See `03-central-hypothesis-and-phase-zero.md`.
- **JSONL is the canonical lossless record.** Summaries, diffs, and InfluxDB rows are derived. Never edit a capture; produce a new analysis.
- **Document protocol bytes, not Python idioms.** When findings resolve into a "what we need to spoof" specification, write it as a protocol doc (page formats, byte layouts, calibration response shape) — not as code. The doc ports across languages; the code may not.
- **`findings/decisions.md` is append-only.** Record every numeric value chosen, every hypothesis refuted, every "it works now" moment. Future debugging will rely on it.
- **MIT-licensed.** Don't copy code from GPL-3.0 prior art (qdomyos-zwift especially). Read, understand, reimplement clean-room.

## Validation

For changes to capture / analysis scripts: smoke-test against a synthetic JSONL fixture (the diff and summarize tools have been tested this way; see `code/findings/decisions.md` for the manufacturer-ID-69-vs-263 reference fixture).

For changes touching openant API usage: verify against the installed openant version (`pip show openant`) and the actual openant source on disk before assuming an API exists.

For changes to the proxy itself: nothing replaces hardware testing against a real SB20. CI tests can only cover replay-against-fixtures.
