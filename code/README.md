# sb20proxy — code

The Python implementation of the SB20 power proxy. See the parent directory for project background and specifications.

## Layout

```
code/
├── pyproject.toml
├── README.md                  ← this file
├── docker/                    ← InfluxDB + Grafana stack for analysis
│   ├── docker-compose.yml
│   └── .env.example
├── grafana/                   ← starter Grafana dashboards (auto-loaded)
│   ├── provisioning/
│   └── dashboards/
├── scripts/                   ← runnable entry points per phase
│   ├── 00_validate_capture.py      Phase 0 — sanity-check a capture (PASS/REVIEW/FAIL)
│   ├── 01_capture_stages.py        Phase 0 — capture Stages crank (ANT+)
│   ├── 02_capture_assioma.py       Phase 0 — capture Assioma (wraps 01)
│   ├── 03_ingest_jsonl_to_influx.py  Analysis — JSONL → InfluxDB
│   ├── 04_summarize_capture.py     Analysis — JSONL → markdown summary
│   ├── 05_diff_captures.py         Analysis — diff two JSONLs
│   ├── 06_capture_ble.py           BLE (CPS) capture + guarded control-point — native Windows
│   ├── 07_capture_multi.py         Multi-source ANT+ (crank+Assioma+bike FE-C) on one stick
│   ├── 08_analyze_grid.py          Calibration analysis: #7 check, ratio surface, grid sizing
│   ├── run_capture.sh              Robust WSL launcher (release stick, detached, retry)
│   └── ride_wizard.py              Solo guided-ride wizard (assisted rides are agent-driven)
└── src/sb20proxy/
    ├── __init__.py
    ├── reading.py             ← canonical PowerReading event
    ├── sources/
    │   ├── __init__.py
    │   ├── base.py            ← PowerSource ABC
    │   └── ...                ← implementations added per phase
    └── targets/
        ├── __init__.py
        ├── base.py            ← PowerTarget ABC
        └── ...                ← implementations added per phase
```

## Setup

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -e ".[dev,analysis]"

# Linux only — udev rule for ANT+ stick (write directly; openant's helper
# isn't pip-shipped). Then unplug+replug the stick.
sudo tee /etc/udev/rules.d/42-ant-usb-sticks.rules >/dev/null <<'RULE'
SUBSYSTEM=="usb", ATTRS{idVendor}=="0fcf", MODE="0666"
RULE
sudo udevadm control --reload-rules && sudo udevadm trigger

# Optional — bring up InfluxDB + Grafana for capture analysis
cd docker
cp .env.example .env
docker compose up -d
cd ..
```

See `../09-exploring-captures.md` for the full capture-to-analysis workflow.

## Tests

```bash
pip install -e ".[dev]"
pytest              # run from code/ — hermetic, no ANT+ stick or SB20 needed
ruff check src tests
```

Unit tests are **required** for any logic that doesn't need hardware (codecs, parsers, replay
sources, `ProxyCore` wiring) and ship in the **same commit** as the code. Fixtures are built from
the real captures in `findings/captures/` (round-trip / golden-vector), never invented bytes.
Hardware-bound radio I/O is isolated behind a seam and tested with a fake; the on-air / pairing
checks are manual (see `../NEXT-BIKE-SESSION.md`). CI (`.github/workflows/tests.yml`) runs `pytest`
+ `ruff` on every push. Full policy: `../CLAUDE.md` §Validation.

### Software loopback (no hardware) — digital twins

```bash
python scripts/03_static_replay.py \
    --input findings/captures/A-stagesL-steady-20260614-165737.jsonl --request-zero
```
Replays a real capture through the whole pipeline into a `BikeTwin` (a software SB20) and prints
what it sees, including the zero-reset handshake. The `sb20proxy.twins` package runs the same twin
over a `LoopbackTransport` (here / CI), an `AntSlaveTransport` (a real stick), or against a real
device — no code change.

### Hardware loopback (needs an ANT+ stick)

```bash
# One stick — sanity-check the real radio binding (skipped in CI):
pytest --run-hardware

# On-air loopback — needs a SECOND receiver (a stick can't hear its own TX):
#   stick A: broadcast a spoofed crank
python scripts/03_static_replay.py --radio ant --input <capture.jsonl> --spoof-id 62145
#   stick B: receive it as a BikeTwin (or use a phone ANT+ app / Garmin paired to 62145)
python scripts/10_bike_twin.py --device-id 62145 --request-zero
```

## Phase 0 usage

```bash
# Capture Stages L crank for 15 minutes
python scripts/01_capture_stages.py \
    --device-id 12345 \
    --duration 900 \
    --output findings/captures/A-stagesL-steady-$(date +%Y%m%d-%H%M).jsonl

# CHECKPOINT: validate Session A before running any further sessions.
# PASS (exit 0) / REVIEW (exit 1) / FAIL (exit 2). Add --markdown to paste into chat.
python scripts/00_validate_capture.py \
    --input 'findings/captures/A-stagesL-steady-*.jsonl'   # keep glob quoted

# Same for Assioma
python scripts/02_capture_assioma.py \
    --device-id 67890 \
    --duration 900 \
    --output findings/captures/D-assioma-steady-$(date +%Y%m%d-%H%M).jsonl

# Ingest into InfluxDB so you can explore visually in Grafana
export INFLUXDB_TOKEN=dev-token-change-me  # match docker/.env
python scripts/03_ingest_jsonl_to_influx.py \
    --input findings/captures/A-stagesL-steady-NNNN.jsonl \
    --source-role stagesL

# Produce a markdown summary (good for sharing with Claude)
python scripts/04_summarize_capture.py \
    --input findings/captures/A-stagesL-steady-NNNN.jsonl \
    > findings/captures/A-stagesL-summary.md

# Produce a side-by-side diff of two captures (the headline Phase 0 artefact)
python scripts/05_diff_captures.py \
    --left  findings/captures/A-stagesL-steady-NNNN.jsonl \
    --right findings/captures/D-assioma-steady-NNNN.jsonl \
    --left-label "Stages L" --right-label "Assioma" \
    > findings/captures/diff-stages-vs-assioma.md
```

See `../03-central-hypothesis-and-phase-zero.md` for the full Phase 0 capture plan.

## Status by phase

| Phase | Status |
|-------|--------|
| 0 — capture & analysis | **substantially complete** — 5 on-bike sessions; see `findings/phase-0-report.md` |
| 1 — replay | **code-complete; software loopback passes** — codec + replay + `StagesAntTarget` + digital-twin loopback (`03_static_replay.py --radio loopback`). Remaining: real-stick hardware loopback + SB20 pairing (Phase 1B). |
| 2 — live proxy | **code-complete; software loop passes** — generic `AntPowerSource` + `PowerMeterTwin` + correction transform; full meter→proxy→bike loop tested (`04_run_proxy.py --radio loopback`). Remaining: run live on a stick + latency/endurance. |
| 2a — meter calibration | **fitter done** — `09_fit_calibration.py` turns a dual-meter capture into a JSON profile (`04_run_proxy.py --profile`); `transform.py` scale/offset + non-linear `GridTransform`. Capture→analyze→fit→apply all built; ESP32 deploy remains. See `findings/forward-plan.md` §4a. |
| 3 — robustness | not started |
| 4 — distributable | generic ANT+ source **already done** (Phase 2) |
