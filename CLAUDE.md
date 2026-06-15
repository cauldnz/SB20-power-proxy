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
- **Test the desk-testable, in the same change.** Any logic that runs without the ANT+ stick or the SB20 — page encode/decode, capture parsing, file replay, `ProxyCore` wiring, calibration-byte construction — ships with `pytest` unit tests in the **same commit** as the code (no "tests later"). Hardware-bound behaviour (radio TX, real pairing) is isolated behind a seam — e.g. an injectable radio — so its logic is still unit-tested with a fake; only the final on-air / pairing check is left to the bench. Build fixtures from the **real committed captures**, never invented bytes (see *Capture before code* and real-data-first). The suite stays hermetic (no hardware, no network) and green; CI runs it on every push. Details in §Validation.
- **MIT-licensed.** Don't copy code from GPL-3.0 prior art (qdomyos-zwift especially). Read, understand, reimplement clean-room.

## Validation

**Unit tests are the standing rule.** They live in `code/tests/`, run with `pytest` from
`code/` (after `pip install -e ".[dev]"`), and are **hermetic** — no ANT+ stick, no SB20, no
network — so they run in CI on every push (`.github/workflows/tests.yml`) and on any laptop.
The conventions:

- **Cover the desk-testable surface in the same commit as the code** — codecs, parsers, sources
  that read files, `ProxyCore` wiring, byte construction (see the *Test the desk-testable*
  discipline above). A change that adds such logic without tests is incomplete.
- **Fixtures come from the real committed captures** in `findings/captures/` (round-trip /
  golden-vector style), not invented bytes. `tests/conftest.py` exposes a `capture_pages` helper
  that iterates the real pages of a capture; reserved/edge byte values were confirmed against the
  captures, not guessed.
- **Isolate hardware behind a seam.** The radio / BLE I/O is the only thing that needs the stick;
  the page-scheduling, calibration, and encode/decode logic must be unit-testable with a fake
  (e.g. a `FakeRadio`). Only the final on-air check is manual.
- **Keep it green and lint-clean.** `pytest` and `ruff check src tests` both pass before a commit.

For changes to capture / analysis scripts: smoke-test against a synthetic JSONL fixture (the diff and summarize tools have been tested this way; see `code/findings/decisions.md` for the manufacturer-ID-69-vs-263 reference fixture).

For changes touching openant API usage: verify against the installed openant version (`pip show openant`) and the actual openant source on disk before assuming an API exists.

For changes to the proxy itself: nothing replaces hardware testing against a real SB20. The bench loopback and SB20 pairing are **manual** steps (documented in `NEXT-BIKE-SESSION.md` and `code/findings/forward-plan.md`); CI covers only the replay-against-fixtures / codec / wiring layer.
