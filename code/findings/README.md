# findings/

This directory holds capture artefacts, analysis reports, and decision logs from project work. Treat it as **append-only history** — never delete or rewrite. When something breaks in Phase 4 that wasn't broken in Phase 1, the answer is often in here.

## Structure

```
findings/
├── README.md                    ← this file
├── captures/                    ← raw JSONL captures from openant
│   ├── A-stagesL-steady-*.jsonl
│   ├── B-stagesR-steady-*.jsonl
│   ├── C-stagesL-pairing-*.jsonl
│   ├── D-assioma-steady-*.jsonl
│   ├── E-assioma-calibration-*.jsonl
│   └── F-failure-mode-*.jsonl
├── decisions.md                 ← running log of "we decided X because Y" with dates
├── phase-0-report.md            ← written after Phase 0 captures are complete
├── phase-1-demo/                ← evidence that Phase 1 worked
├── phase-2-report.md
└── proxy-runs/                  ← rolling logs from Phase 3+ deployment runs
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
