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
│   ├── 01_capture_stages.py        Phase 0 — capture Stages crank
│   ├── 02_capture_assioma.py       Phase 0 — capture Assioma
│   ├── 03_ingest_jsonl_to_influx.py  Analysis — JSONL → InfluxDB
│   ├── 04_summarize_capture.py     Analysis — JSONL → markdown summary
│   └── 05_diff_captures.py         Analysis — diff two JSONLs
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

# Linux only — udev rule for ANT+ stick
sudo $(which python) -m openant.udev_rules

# Optional — bring up InfluxDB + Grafana for capture analysis
cd docker
cp .env.example .env
docker compose up -d
cd ..
```

See `../09-exploring-captures.md` for the full capture-to-analysis workflow.

## Phase 0 usage

```bash
# Capture Stages L crank for 15 minutes
python scripts/01_capture_stages.py \
    --device-id 12345 \
    --duration 900 \
    --output ../findings/captures/A-stagesL-steady-$(date +%Y%m%d-%H%M).jsonl

# Same for Assioma
python scripts/02_capture_assioma.py \
    --device-id 67890 \
    --duration 900 \
    --output ../findings/captures/D-assioma-steady-$(date +%Y%m%d-%H%M).jsonl

# Ingest into InfluxDB so you can explore visually in Grafana
export INFLUXDB_TOKEN=dev-token-change-me  # match docker/.env
python scripts/03_ingest_jsonl_to_influx.py \
    --input ../findings/captures/A-stagesL-steady-NNNN.jsonl \
    --source-role stagesL

# Produce a markdown summary (good for sharing with Claude)
python scripts/04_summarize_capture.py \
    --input ../findings/captures/A-stagesL-steady-NNNN.jsonl \
    > ../findings/captures/A-stagesL-summary.md

# Produce a side-by-side diff of two captures (the headline Phase 0 artefact)
python scripts/05_diff_captures.py \
    --left  ../findings/captures/A-stagesL-steady-NNNN.jsonl \
    --right ../findings/captures/D-assioma-steady-NNNN.jsonl \
    --left-label "Stages L" --right-label "Assioma" \
    > ../findings/captures/diff-stages-vs-assioma.md
```

See `../03-central-hypothesis-and-phase-zero.md` for the full Phase 0 capture plan.

## Status by phase

| Phase | Status |
|-------|--------|
| 0 — capture & analysis | scripts in place, not yet run |
| 1 — replay | not started |
| 2 — live proxy | not started |
| 3 — robustness | not started |
| 4 — distributable | not started |
