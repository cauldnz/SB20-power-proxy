# 09 — Exploring Captures Together

Phase 0 produces JSONL captures. To make them useful for analysis — both for the owner solo and for collaborative work with Claude — there's a small pipeline of tools.

## The pipeline

```
   Live ANT+ traffic                     Owner reads dashboards
          │                                       ▲
          ▼                                       │
   ┌──────────────────┐    JSONL    ┌──────────────────┐
   │ 01_capture_*.py  │────────────▶│ JSONL files in   │
   │ (forensic capture)│             │ findings/        │
   └──────────────────┘             └──────────────────┘
                                       │       │      │
                              ┌────────┘       │      └────────┐
                              ▼                ▼               ▼
                  ┌────────────────────┐ ┌────────────┐ ┌────────────┐
                  │ 03_ingest_jsonl_   │ │ 04_summa-  │ │ 05_diff_   │
                  │ to_influx.py       │ │ rize.py    │ │ captures.py│
                  └────────┬───────────┘ └─────┬──────┘ └─────┬──────┘
                           │                   │              │
                           ▼                   ▼              ▼
                  ┌────────────────┐    Markdown reports for sharing
                  │  InfluxDB +    │           (with Claude, or in
                  │  Grafana       │            phase-0-report.md)
                  └────────────────┘
                           ▲
                           │ http://localhost:3000
                           │
                       Owner browses
```

JSONL is the canonical, lossless record. Everything else is derived.

## Quick start

```bash
# 1. Spin up InfluxDB + Grafana (one-off; persists across sessions)
cd code/docker
cp .env.example .env             # edit token if you want
docker compose up -d
# InfluxDB:  http://localhost:8086
# Grafana:   http://localhost:3000  (admin/admin, change on first login)

# 2. Install the analysis extras
cd ..
pip install -e ".[analysis]"

# 3. Capture some traffic (Phase 0 sessions A–F per 03-central-hypothesis-and-phase-zero.md)
python scripts/01_capture_stages.py --device-id 12345 --duration 900 \
    --output ../findings/captures/A-stagesL-steady-$(date +%Y%m%d-%H%M).jsonl

# 4. Ingest into InfluxDB for visual exploration
export INFLUXDB_TOKEN=dev-token-change-me   # match docker/.env
python scripts/03_ingest_jsonl_to_influx.py \
    --input ../findings/captures/A-stagesL-steady-NNNN.jsonl \
    --source-role stagesL

# 5. Open Grafana, navigate to "SB20 Proxy → Phase 0 Capture Overview"
#    Filter by capture_id to compare different captures side by side.

# 6. Generate text summaries to share / analyse / commit
python scripts/04_summarize_capture.py \
    --input ../findings/captures/A-stagesL-steady-NNNN.jsonl \
    > ../findings/captures/A-stagesL-summary.md

# 7. Generate a diff between two captures (the core Phase 0 deliverable)
python scripts/05_diff_captures.py \
    --left  ../findings/captures/A-stagesL-steady-NNNN.jsonl \
    --right ../findings/captures/D-assioma-steady-NNNN.jsonl \
    --left-label "Stages L" --right-label "Assioma DUO" \
    > ../findings/captures/diff-stages-vs-assioma.md
```

## Why three tools, not one

Each tool has a different audience:

- **`03_ingest_jsonl_to_influx.py`** → for the owner's eyes. Grafana dashboards are great for visual pattern-matching, time-series exploration, and catching anomalies that don't fit a pre-conceived report.
- **`04_summarize_capture.py`** → for **discussion with Claude**. The output is markdown, copy-pasteable into chat, and surfaces the things that matter most: page mix, common-page values, calibration events, ack messages, statistical sanity.
- **`05_diff_captures.py`** → for **the Phase 0 report itself** and for collaborative diagnosis. The diff between Stages and Assioma is the central artefact of Phase 0.

Claude can read text but not Grafana screenshots. The summary and diff tools exist so we can have substantive conversations about what the captures actually contain, without having to retype field values from a dashboard.

## What to share with Claude

When asking Claude to help analyse a capture or set of captures, paste:

1. The output of `04_summarize_capture.py` for each capture (or just for the most relevant one).
2. The output of `05_diff_captures.py` if comparing two captures.
3. Any specific raw_hex values that look strange — Claude can decode them against the ANT+ Bike Power profile.
4. A description of what was happening on the bike during the capture (idle, mid-pairing, mid-zero-reset, pedalling at X watts).

Don't paste raw JSONL — it's verbose and noisy. The summary/diff tools are designed precisely to extract the signal.

## Schema reference for ad-hoc Flux queries

If you want to run your own Flux queries directly against InfluxDB, the schema is documented at the top of `03_ingest_jsonl_to_influx.py`. Quick reference:

```flux
// Power over time for one capture
from(bucket: "captures")
  |> range(start: -1h)
  |> filter(fn: (r) => r._measurement == "ant_power_only")
  |> filter(fn: (r) => r._field == "power_w")
  |> filter(fn: (r) => r.capture_id == "A-stagesL-steady-20260510-1830")

// Page-mix histogram across all captures from the Stages L crank
from(bucket: "captures")
  |> range(start: -7d)
  |> filter(fn: (r) => r._measurement == "ant_page_count")
  |> filter(fn: (r) => r.source_role == "stagesL")
  |> group(columns: ["page_name"])
  |> sum()

// Manufacturer ID seen across captures, per source_role
from(bucket: "captures")
  |> range(start: -7d)
  |> filter(fn: (r) => r._measurement == "ant_manufacturer")
  |> filter(fn: (r) => r._field == "manufacturer_id")
  |> last()
  |> group(columns: ["source_role"])
```

## What lives where

- `code/docker/docker-compose.yml` — InfluxDB + Grafana stack
- `code/docker/.env.example` — env template (copy to `.env`)
- `code/grafana/dashboards/phase-0-capture-overview.json` — starter dashboard, auto-loaded
- `code/grafana/provisioning/` — Grafana auto-config for the InfluxDB datasource
- `code/scripts/03_ingest_jsonl_to_influx.py` — JSONL → InfluxDB
- `code/scripts/04_summarize_capture.py` — JSONL → markdown summary
- `code/scripts/05_diff_captures.py` — two JSONL files → markdown diff

## Pi vs laptop

The Docker stack runs fine on a Pi 4/5 but uses meaningful resources (a few hundred MB RAM, some disk). For Phase 0 development on a laptop, that's nothing. For Phase 3 production-on-Pi deployment, you probably **don't** want InfluxDB+Grafana on the Pi — instead, the Pi should write JSONL locally and you ingest into a laptop-side InfluxDB when you want to look at data. The capture script doesn't care; it just produces JSONL.

## Capture hygiene reminder

- Always capture the **firmware version** of the SB20 (visible in the Stages app) alongside each session. Note it in `findings/decisions.md` or in the JSONL filename.
- Always note **what the rider was doing** at each rough timestamp during pairing/zero-reset captures (the JSONL has timestamps, but they're more useful when annotated).
- Keep the JSONL files. Disk is cheap; understanding is expensive. If you re-ingest into a fresh InfluxDB later, you can do that any time as long as the JSONL is preserved.
