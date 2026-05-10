# Changelog

## Revision 5 — GitHub bootstrap + Windows/WSL onboarding

The owner is taking the project to a GitHub repo and will run Phase 0 captures themselves on a Windows machine with WSL2 before kicking off Claude Code. This revision adds the missing pieces for that:

### New
- **`LICENSE`** — MIT. Permissive on purpose so other SB20 owners can adopt freely. (Note in `08-risks-and-gotchas.md` already explains why we don't want GPL inheritance from QZ.)
- **`.gitignore`** at repo root — extends what `code/.gitignore` covered. Ignores `__pycache__`, `.venv`, IDE detritus, OS files, `code/docker/.env`, raw `findings/captures/*.jsonl` (with `.gitkeep` exception).
- **`CLAUDE.md`** — small Claude Code project-level metadata file. Points at `HANDOFF.md` and the numbered docs; codifies the engineering disciplines (capture-before-code, JSONL is canonical, document-protocol-not-code, append-only decisions log, no-GPL-copying). Follows the QZ convention; intentionally short rather than duplicating `HANDOFF.md`.
- **`START-HERE.md`** — substantial new user-facing walkthrough. Audience: the project owner (or any SB20 owner) doing initial Phase 0 themselves on Windows + WSL2. Covers:
  - Hardware checklist (the user has two ANT+ sticks)
  - One-time WSL2 + `usbipd-win` USB-passthrough setup, with real PowerShell commands
  - Per-WSL-session `usbipd attach` reminder
  - openant udev rule application inside WSL
  - Project setup: clone in WSL native fs, venv, install
  - Pre-capture sanity checks (Wi-Fi off 2.4 GHz, fresh CR2032s, etc.)
  - Run-by-run instructions for capture sessions A through F (and optional G), with concrete commands
  - Quick-analysis flow (summarize, diff)
  - Optional Docker stack bring-up (Docker Desktop or Docker Engine in WSL)
  - Hand-off to Claude Code
  - Troubleshooting section
- **`code/findings/captures/.gitkeep`** — so the captures directory exists when the repo is cloned, even before any captures are taken.

### Changed
- **`README.md`** — reworked to be a proper GitHub front page. Adds a "Where to start" table that routes different audiences to `START-HERE.md` / `HANDOFF.md` / the numbered docs, an architecture-in-one-paragraph block, an acknowledgements section, and a clearer "do not write proxy code before Phase 0" framing. The repo layout block now shows the new files.
- **`07-hardware-and-environment.md`** — replaced the one-line "Windows: harder, use WSL" with a proper Windows + WSL2 section. Covers `usbipd-win` install, bind/attach lifecycle, two-stick handling, filesystem-performance gotcha (clone in `~`, not `/mnt/c`), and the persistent Wi-Fi-interference caveat. The Linux/macOS sections are unchanged.
- **`HANDOFF.md`** — first paragraph of "First concrete task: Phase 0" now acknowledges that the owner may have done captures already and points at `START-HERE.md`. The capture-then-analyse sequence is preserved but with explicit branching: "if captures exist, start at step 5".

### Why this matters
Phase 0 is the work item right now. The owner is on Windows. Without `usbipd-win` instructions and a WSL-aware setup walkthrough, they'd hit the USB-passthrough wall before getting any captures. This revision removes that friction. The GitHub bootstrap pieces (LICENSE, .gitignore, CLAUDE.md, polished README) make the repo cleanly cloneable by other SB20 owners later.

---

## Revision 4 — QZ relationship (strategic position)

The owner raised the question: should this work eventually upstream into QZ (qdomyos-zwift) rather than be a standalone tool?

### New

- **`10-relationship-to-QZ.md`** — full strategic writeup. Headline: build standalone first, decide post-Phase-2.
  - Articulates the architectural distinction: QZ operates between bike and apps; this project operates between cranks and bike. Different layers; could coexist or merge.
  - Identifies the technical wedge: QZ on phones is BLE-only; integrating with QZ would require BLE-side spoofing, not ANT+-side. Whether BLE-paired cranks behave the same as ANT+-paired ones in the SB20's internal control loop is unknown — added Phase 0 Session G to find out cheaply.
  - Names three concrete decision points (end of Phase 0, end of Phase 2, end of Phase 3). The choice doesn't need to be made now.
  - Lists three small architectural disciplines that keep the QZ-port option cheap to exercise later (clean source/target seam, document protocol bytes not Python idioms, declarative config).

### Changed

- **`03-central-hypothesis-and-phase-zero.md`** — added **Session G** (BLE-paired cranks). Captures what the crank-bike conversation looks like over BLE, using bleak/pycycling rather than openant. The Phase 0 deliverable now includes an optional BLE-side feasibility subsection if Session G ran.
- **`README.md`** — repository layout adds `10-relationship-to-QZ.md`.
- **`HANDOFF.md`** — "What's already in this package" lists docs 09 and 10. The QZ entry under "What's not in this package" now points at `10-relationship-to-QZ.md` for the strategic position.
- **`CLAUDE-CODE-PROMPT.md`** — adds `10-relationship-to-QZ.md` to the read-as-needed list with a one-line note that the decision is deferred.

### Why this matters

The QZ question is the right kind of question to surface early and answer late. Surfacing it early means we pick architectural decisions (clean source/target seam, declarative config, document protocol-not-code) that keep the option open. Answering late means we have working code and protocol documentation to negotiate with — both internally about whether the port is worth the effort, and externally with cagnulein about whether the contribution is wanted.

Session G is the cheapest thing that decides whether the QZ path is even technically possible. Worth running in Phase 0 regardless of whether we eventually take that path.

---

## Revision 3 — Capture analysis pipeline

Added a small but complete pipeline for turning JSONL captures into shareable analysis.

### New

- **`code/docker/docker-compose.yml`** — InfluxDB 2.7 + Grafana 11 stack for visual capture exploration. One-command bring-up via `docker compose up -d`. Persistent volumes; data survives across restarts.
- **`code/docker/.env.example`** — environment variable template (org, bucket, token).
- **`code/grafana/provisioning/`** — auto-config for the InfluxDB datasource and the dashboards folder. Grafana picks them up on container start.
- **`code/grafana/dashboards/phase-0-capture-overview.json`** — starter dashboard with five panels: power-over-time, cadence-over-time, page-mix histogram, common-pages-table, channel-events-and-acks-table. Capture-id selector at the top to filter.
- **`code/scripts/03_ingest_jsonl_to_influx.py`** — JSONL → InfluxDB ingester. Maps each ANT+ page to its own measurement (`ant_power_only`, `ant_crank_torque`, `ant_manufacturer`, etc.) plus a counter measurement for page-mix charting and a forensic `ant_raw` measurement.
- **`code/scripts/04_summarize_capture.py`** — JSONL → markdown summary. Output is designed to be pasted into chat with Claude. Surfaces session metadata, page mix with rates, common-page values, power/cadence statistics, calibration events with full payloads, and acknowledged-message traffic.
- **`code/scripts/05_diff_captures.py`** — two JSONL files → side-by-side markdown diff. The headline Phase 0 deliverable: Stages L vs Assioma manufacturer-ID, page-mix, and calibration-handshake comparison in one report. Includes a "headline questions" prompt at the bottom to guide analysis.
- **`09-exploring-captures.md`** — workflow documentation: pipeline diagram, quick-start, "what to share with Claude", schema reference for ad-hoc Flux queries, and Pi-vs-laptop deployment notes.

### Changed

- **`code/pyproject.toml`** — replaced the old `[influx]` extra (which depended on openant's CLI integration) with an `[analysis]` extra that pulls in `influxdb-client` directly. Cleaner separation: `[analysis]` covers our scripts, `[ble]` covers BLE-side work.
- **`code/README.md`** — restructured to document the new layout (docker/, grafana/, scripts/03-05). Added quick-reference for each tool's role.
- **`HANDOFF.md`** — Phase 0 step list now includes ingestion into InfluxDB and running the summary/diff tools as part of the standard workflow.
- **`07-hardware-and-environment.md`** — replaced the old "Optional Phase 0 visualization" section with a pointer to the docker-compose stack and the workflow doc.
- **`03-central-hypothesis-and-phase-zero.md`** — Phase 0 deliverable section now explicitly references using the diff/summary tools as supporting evidence in the report.
- **`README.md`** — repository layout updated to show docker/, grafana/, and the new doc.
- **`CLAUDE-CODE-PROMPT.md`** — added 09 to the read list, added a question about which tool outputs are designed for Claude vs the owner, and added a schema-consistency cross-check between the capture script and the analysis scripts. Preserved the user's earlier additions about QZ's CLAUDE.md.

### Why this matters

Phase 0 is *only* useful if the captures get analysed. The previous package had capture scripts but no analysis pipeline — which would have meant either the owner doing manual work to turn JSONL into something readable, or me trying to reason about raw bytes. Both bad. Now:

- The owner can scan captures visually in Grafana to catch anomalies that don't fit a pre-conceived report.
- The owner can produce a markdown summary or diff with one command and paste it into chat for collaborative analysis.
- The Phase 0 written report has its supporting evidence (diff + summaries) as derivable artefacts, not hand-typed extracts.

The summary/diff tools have been smoke-tested with synthetic JSONL and produce the expected output (manufacturer-ID diff, calibration-message comparison, page-mix table).

---

## Revision 2 — Parent research integration

### Behavioural changes

- **Reading order now leads with `Research_Content`.** `HANDOFF.md` and `CLAUDE-CODE-PROMPT.md` both make Phase 0 step 0 = "read the parent research". The package no longer re-explains protocol fundamentals already covered there.

### Content additions

- **`02-technical-context.md`** — added "the SB20 plays two roles simultaneously" table making explicit which protocol layer we touch vs which we leave alone. Added firmware-version sensitivity note (Stages 3.7.0+ moved scaling logic).
- **`04-architecture.md`** — added long-term Rust evolution path section. Added concrete openant.devices.power_meter snippet for the source side.
- **`06-prior-art-and-references.md`** — added previously-missing references that are the closest architectural prior art for "broadcast as a fitness device": `zwack`, `OpenRowingMonitor`, `raralabs/pm5-emulator`. Added `pycycling` and `bleak` writeups. Added an explicit "parent research (read first)" pointer at the top.
- **`05-implementation-phases.md`** — Phase 1 entry criteria now include skimming dhague/vpower and OpenRowingMonitor before writing replay code.
- **`07-hardware-and-environment.md`** — added optional BLE install path; added `openant scan --auto_create` tip.
- **`08-risks-and-gotchas.md`** — strengthened Wi-Fi interference and PSU undervolting warnings. Added Stages firmware version variance gotcha.

### Code changes

- **`code/scripts/01_capture_stages.py`** — uses `from openant.devices import ANTPLUS_NETWORK_KEY` instead of hardcoding the key bytes.
