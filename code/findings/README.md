# findings/

This directory holds capture artefacts, analysis reports, and decision logs from project work. Treat it as **append-only history** — never delete or rewrite. When something breaks in Phase 4 that wasn't broken in Phase 1, the answer is often in here.

> **Start here:** **[`phase-0-report.md`](phase-0-report.md)** is the current source of
> truth (synthesis of what's proven, the spoof spec, device IDs, open items, plan).
> **`decisions.md`** is the full chronological log. **[`captures/README.md`](captures/README.md)**
> indexes the actual capture files. **`screenshots/`** holds app-UI references.

## Structure

The block below is the *idealised* layout from project setup — the real Phase-0 sessions
diverged from these placeholder names. Use `captures/README.md` for the authoritative
capture inventory.

```
findings/
├── README.md                    ← this file
├── phase-0-report.md            ← ★ current source of truth (synthesis)
├── decisions.md                 ← running chronological log
├── captures/                    ← raw JSONL/FIT captures (+ captures/README.md index)
├── screenshots/                 ← app-UI screenshots (stages-app, stages-power-app, favero-assioma-app)
├── phase-1-demo/                ← (future) evidence that Phase 1 replay worked
└── proxy-runs/                  ← (future) rolling logs from Phase 3+ deployment
```

## Naming conventions

- Capture files: `<session-letter>-<device>-<scenario>-<YYYYMMDD-HHMM>.jsonl`
  - Session letters refer to the sessions defined in `../03-central-hypothesis-and-phase-zero.md`
  - Device: `stagesL`, `stagesR`, `assioma`, `sb20fec`
  - Scenario: `steady`, `pairing`, `calibration`, `failure-mode`, `endurance`
- Reports: `phase-N-<topic>.md`
- Decision log entries: prefix with `## YYYY-MM-DD —` followed by a short title

## Why commit captures?

They're not source code, but they're load-bearing project history. Future
debugging — and any future contributor's onboarding — will refer back to them.
The on-disk size is small (a 15-minute Bike Power capture is well under 1 MB).

If a capture grows beyond Github's reasonable file size, compress it
(`gzip -k <file>`) and commit the `.gz`.
