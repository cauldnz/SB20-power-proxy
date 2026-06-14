# Changelog

## Revision 12 — BLE/ESP32 path planned; Session G capture spec (reuses raedian-probe)

Plans the productisation path (cheap, distributable BLE proxy on ESP32) and specs the
capture that gates it. Key realisation from reviewing the owner's `cauldnz/raedian-probe`
EVSE project: ~80% of the BLE/ESP32 substrate already exists and is directly reusable.

### New
- **`11-ble-and-esp32-path.md`** — strategy + architecture for the ESP32 BLE proxy.
  Dual-role BLE (central→Assioma, peripheral→bike) confirmed feasible on NimBLE. Maps
  the proxy onto `raedian-probe/esp32_bridge_spec.md` (same ESP32-C3 + OLED + NimBLE +
  OTA + onboarding scaffold), and the BLE recon onto its `scan/enumerate/listen/
  probe_write` toolkit + `sniffer_setup_runbook.md` (nRF52840 → Wireshark → pcap_analyze).
  Board strategy: C6-LCD for dev/on-device calibration UX, C3+OLED hero SKU.
- **`code/findings/session-G-ble-capture-spec.md`** — exactly what to capture to evaluate
  and build the BLE path: active recon (crank advert/GATT/reads/CPS/calibration, while
  ANT+-paired) + passive sniff (bonding + the bike's calibration write, in BLE-crank mode)
  + the erg-works-on-BLE-cranks **gate**. Each item mapped to what the ESP32 impersonation
  needs.

### Changed
- `CALIBRATION-RIDE-CARD.md` gains a "→ then BLE evaluation" section: after the ANT+
  work, attempt active BLE recon + the erg-works gate (and the sniff if the nRF dongle
  is ready).

### Why
ESP32 can't do ANT+, so productisation runs on BLE — and the owner has already built the
hard parts (NimBLE client, OTA/observability, OLED onboarding, an nRF sniffer pipeline)
for a parallel EVSE project. The SB20 ESP32 proxy is a second instance of that pattern,
gated only on Session G confirming the SB20's BLE crank mode gives full erg and is spoofable.

## Revision 11 — Session-2 toolkit: multi-source capture, grid analysis, calibration run sheet

De-scopes the calibration model from the critical path (the proxy feeds Assioma
watts directly; the erg loop closes on them — no per-meter model needed; see
`decisions.md`), and builds the tooling for the next on-bike session.

### New
- **`code/scripts/07_capture_multi.py`** — multi-source capture, several ANT+
  devices on one stick into one same-clock JSONL (replaces the dual-only
  `07_capture_dual.py`). Adds an **FE-C decoder** (device type 0x11, page 0x19
  instantaneous power, verified against openant) so we can record the **bike's
  own power output** alongside the crank — the clean test for open-question #7
  (does the SB20 rescale crank power?). Carries the `os._exit`-on-setup-failure
  hardening.
- **`code/scripts/08_analyze_grid.py`** — analyses a multi-source capture: the
  **#7 verdict** (bike-FEC vs crank ratio → pass-through or a factor), the
  Stages/Assioma **ratio surface** (power × cadence), a regression showing
  **which dimension drives the offset** (cadence / power / torque R²), and
  **grid-design guidance** (whether a short torque sweep suffices or a full 2-D
  grid is needed — "how many cells"). Verified on a synthetic torque-dependent
  fixture.
- **`CALIBRATION-RIDE-CARD.md`** — run sheet for session 2: one multi-source
  capture covering the #7 check + a power×cadence grid + sprints (the owner's
  800–1000 W+ range), agent-driven per the runbook.

### Why
Session 1 proved the offset is torque-shaped but also that the proxy *eliminates*
the offset (Assiomas go straight into the erg loop) rather than needing to model
it. So the next ride's real deliverable is **closing #7** — confirming the bike
passes crank power through unscaled, which makes "feed Assioma → erg targets are
Assioma watts" literally true. The grid is now optional research, instrumented so
one ride tells us how cheap a real calibration would be.

## Revision 10 — First live ride: results + WSL/USB operations hardening

The first guided ride succeeded — captured the calibration handshake and a clean
Session A — but burned ~25 min of bike time on avoidable WSL/USB/process issues.
This revision banks the results and makes sure those issues never recur.

### Results (detail in `code/findings/decisions.md`, 2026-06-14)
- **C-0 PASS:** calibration reply captured on air as broadcast page 0x01,
  `0xAC` success + offsets 903 / −950 matching the app. The proxy's exact
  spoof bytes are now known. Biggest Phase-0 de-risk to date.
- **Session A: validator PASS** — 2,575 broadcasts, power 0→569 W, full
  cadence sweep; `manufacturer_id=69` re-confirmed.
- Device IDs resolved (via BLE survey): **62144 = Stages crank, 17039 =
  Assioma**. Stages BLE advertises Cycling Power even while ANT+-paired.
- New protocol detail: Stages crank **latches last power (~416 W) when
  stopped** instead of zeroing.

### Operations hardening (so the next ride is smooth)
- **`01_capture_stages.py`: force-exit on setup failure.** openant's worker
  thread is non-daemon, so a capture that failed mid-setup used to **hang and
  keep the USB stick claimed**, blocking every retry with "Resource busy".
  `main()` now `os._exit(2)`s on a setup exception, releasing the device
  immediately. This was the single worst time-sink of the day.
- **`code/findings/wsl-capture-runbook.md`** — symptom→cause→fix catalog for
  every problem hit (root-only USB perms, CHANNEL_IN_WRONG_STATE, zombie
  holders, the `pkill -f` self-kill footgun, `pyusb reset` being harmful, the
  broken terminal stdin), plus the agent-drives-captures model and a 60-second
  pre-ride checklist.
- **`code/scripts/run_capture.sh`** — robust launcher: releases the stick from
  any zombie holder (by exact PID, never a name pattern), launches detached,
  retries the transient wrong-state, and prints a live data check.

## Revision 9 — Guided ride wizard for the Phase-0 sessions

Makes the capture sessions a single-command, talked-through experience so the
owner can ride them solo (with Claude watching the JSONL live over
`\\wsl.localhost` and chatting during the ride).

### New
- **`code/scripts/ride_wizard.py`** — interactive WSL wizard that runs
  C-0 → A → optional B in one sitting: preflight (stick check), timed
  on-screen cues with bell (when to zero-reset, power blocks in Stages watts,
  the 30 s coast for zero-power samples, cadence-extreme blocks), a live
  data heartbeat (message count + last power seen, with a loud warning if
  nothing is arriving), automatic C-0 page-0x01 verdict, automatic Session A
  validation, auto-generated per-session `-notes.md` timestamp annotations
  (satisfying the Session C/A annotation requirement), and optional
  auto-launch of the Windows BLE adv survey via WSL interop (`cmd.exe start`).
  `--preview` shows the full cue flow at 20x speed without hardware.
- **`RIDE-CARD.md`** (repo root) — one-page morning reference: stick attach,
  wizard start, session tables, troubleshooting, live-chat workflow, and the
  optional Assioma-on-watch recording for the open question #7 scaling data.

### Fixed during in-WSL testing (real-environment shakeout)
- `importlib` loading of the validator must register the module in
  `sys.modules` first — `@dataclass` resolves its module that way on
  Python 3.12 and crashes otherwise.
- A `CueThread._stop` Event attribute shadowed `threading.Thread._stop()`,
  crashing `join()` — renamed. Both found by running the actual preview in
  the owner's WSL distro rather than assuming.

## Revision 8 — Parallel BLE capture (optionality for a BLE-path implementation)

Adds passive BLE Cycling Power capture alongside the ANT+ sessions, motivated by
implementation optionality: a future low-cost target like an **ESP32 cannot speak
ANT+** (no ANT radio), so any ESP32/QZ endgame runs on the BLE path — and the BLE
protocol model of the Stages cranks is capturable for free during the rides we're
already doing. Existing DIY projects (e.g. kochcodes/ESP32_BLE_CyclingPowerMeter)
prove ESP32-as-CPS-peripheral is viable.

### New
- **`code/scripts/06_capture_ble.py`** — bleak-based, cross-platform (Windows/
  Linux/macOS) BLE capture: advertisement survey, GATT table dump, device-info
  reads, and decoded Cycling Power Measurement (0x2A63) notifications to JSONL
  (same envelope as the ANT+ captures; `protocol: "ble"` in session_start).
  **Passive by design** — never writes to any characteristic; the Control Point
  (0x2A66) is logged as present but untouched. Auto-reconnects on link drops.
  CPS flag-field decoder unit-tested against synthetic payloads incl. truncation.
- **Windows-side venv** at `code/.venv-win` (bleak 3.0.2 / Python 3.14) — BLE
  capture runs on **native Windows**, because the stock WSL2 kernel has no
  Bluetooth subsystem (custom-kernel-only; not worth it). ANT+ stays in WSL;
  the two terminals share the host clock so JSONL timestamps align.
  bleak 3.0.2's API surface (BleakScanner detection_callback,
  discovered_devices_and_advertisement_data, BleakClient disconnected_callback)
  verified against the installed source, same discipline as openant.
- **START-HERE.md** §"Optional: parallel BLE capture" — second-terminal workflow;
  `03-...md` cross-reference ahead of the session list.

### Guardrail
Parallel BLE capture is additive-only: the bike remains ANT+-paired to the
cranks throughout Sessions A–F. The app's "Pair with Bluetooth" toggle (which
re-homes the crank↔bike link onto BLE) stays a Session G activity, after the
ANT+ baseline is fully captured.

### Owner decisions (2026-06-10)
- First dual-capture ride runs BLE in `--adv-only` survey mode; connect-mode
  capture follows on a later ride.
- **ESP32 confirmed as a real deployment target** → Session G promoted from
  optional to planned (post A–F) in `03-central-hypothesis-and-phase-zero.md`;
  the phase-0 report must now cover BLE-paired erg behaviour and the CP 0x2A66
  calibration handshake.

## Revision 7 — Capture path verified against real openant + hardened before first session

Pre-flight hardening of `01_capture_stages.py` ahead of the first real Session A
and the Session C-0 ACK dry run. Every assumed openant API was checked against the
**actual installed source (openant 1.3.4)**, not against assumption. Full detail in
`code/findings/decisions.md` (2026-06-10 entry).

### Verified (openant 1.3.4 source)
- `Channel.Type.BIDIRECTIONAL_RECEIVE`, `set_id/set_period/set_rf_freq/set_search_timeout`,
  `ANTPLUS_NETWORK_KEY`, and `on_acknowledge_data` (the correct RX-ack dispatch hook)
  all exist as assumed.
- `Channel.enable_extended_messages()` exists natively (0x66) — **no pirower fork needed.**
- Reference `PowerMeter` uses `period=8182, device_type=11, trans_type=0` — matches our
  defaults and the validator.
- Page 0x50 `manufacturer_id` at bytes 4–5 confirmed against openant `devices/common.py`
  (the H2 smoking-gun offset is correct as coded).

### Changed — `code/scripts/01_capture_stages.py`
- **Extended messages now actually enabled.** The docstring claimed they were on; the
  code never called `enable_extended_messages`. Now it does, and logs an `ext_messages`
  record. `decode_page` parses the appended source channel ID into `ext_device_number` /
  `ext_device_type` / `ext_transmission_type` (proves *which* meter each packet came from).
- **Toggle-bit-robust page matching** — pages matched against `data[0] & 0x7F` while the
  raw page byte + toggle bit are still recorded.
- **New `--log-channel-events` flag** tees non-data channel events (RX_FAIL, search
  timeout, channel closed, collision) into the JSONL. Off by default; recommended for
  Sessions C/F to catch pairing failures. Replaces previously-dead `_on_event` code.

### Changed — framing
- **Session C-0 pass criterion rewritten** in `03-central-hypothesis-and-phase-zero.md`.
  The capturable artefact is the crank's calibration **response** (broadcast page 0x01,
  ID 0xAC), **not** a `"kind": "acknowledged"` record. The SB20→crank *request* is a
  slave uplink a passive sniffer cannot see; its absence is normal, not a failure. The
  honest fallback for the request bytes (sniffer hardware) is noted, alongside the point
  that the proxy only strictly needs the response.

### Validation (no hardware)
- `py_compile` clean; `decode_page` smoke-tested on synthetic fixtures (plain page,
  page+ext tail, manufacturer-ID 69, toggled 0x90 page, calibration offset −50,
  short-payload guard); full synthetic JSONL run end-to-end through `00_validate`,
  `04_summarize`, and `05_diff` with the new schema fields present.

### Changed — WSL ANT-stick passthrough (START-HERE.md, 07, CLAUDE.md)
- **Fixed a broken udev command across all setup docs.** `python -m openant.udev_rules`
  copies a rules file from a relative `resources/` path that pip does **not** ship in the
  openant wheel, so it fails with `FileNotFoundError`. Replaced everywhere with a direct
  one-line rule write (`/etc/udev/rules.d/42-ant-usb-sticks.rules`, vendor `0x0fcf`,
  `MODE="0666"`) + `udevadm reload/trigger`.
- **Added `wsl --update` to Step 1** + a troubleshooting note for the classic "`usbipd
  attach` succeeds but `lsusb` is empty" symptom (outdated WSL kernel missing USB/IP).
- Documented the WSL udev caveat (rules auto-apply only under systemd/udev) with a
  `sudo $(which python) ...` capture fallback. Confirmed `usbipd` 4.x syntax.

### Changed — final pre-session review pass
- **Fixed an end-of-capture crash in `01_capture_stages.py`.** On the normal
  duration-expiry path, `stop()` ran twice (once from the SIGALRM handler, once from
  `run()`'s `finally`); the second call tried to close an already-stopped driver and then
  log the failure to an already-closed file — `ValueError: I/O operation on closed file`
  at the end of *every* duration-limited capture. Data was written fine, but the user
  would see a traceback and reasonably conclude the session failed. `stop()` is now
  idempotent (`_stopped` guard). Pre-existing bug; surfaced by tracing the alarm path.
- **Validator now checks the `ext_messages` record** (`00_validate_capture.py`):
  PASS when extended messages enabled, WARN when the enable failed or the record is
  missing (old script copy). Surfaces stick capability in the Session A paste-back.
- **Findings path unified to `code/findings/captures/`.** START-HERE (cwd repo root),
  09 + code/README + 07 (cwd `code/`), HANDOFF, and CLAUDE-CODE-PROMPT all previously
  resolved capture paths to a nonexistent root-level `findings/`; the committed tree is
  `code/findings/`. All commands now resolve there. Root `.gitignore` capture patterns
  updated to match (raw `.jsonl` stays ignored-by-default, opt-in via `git add -f`).
- **Validator globs quoted** in START-HERE/code/README examples — an unquoted glob
  expands to multiple args (and an argparse error) as soon as a second matching capture
  exists, e.g. a smoke-test file plus the real Session A.

### Why this matters
The previous capture script would have run a real session with extended messages *off*
(despite the docstring), and the team would have searched Session C-0 output for
`"kind": "acknowledged"` records that a passive slave can never produce — concluding the
capture method was broken when it was working. The setup docs would also have failed at
the udev step (a command that cannot work with a pip-installed openant) and at an empty
`lsusb` with no hint why. The review pass then caught a crash on the normal end-of-capture
path that would have made every successful session *look* like a failure, and a path
mismatch that would have scattered captures outside the committed findings tree. This
revision aligns code, docs, and on-air reality — and clears the USB-passthrough path —
before the hard-to-repeat sessions are run.

## Revision 6 — Review pass + Session A validator (re-added) + current Stages status

A fresh-eyes review of the whole package, plus the realisation that the
Session A checkpoint work from the previous session's final turn never made
it into the committed repo. This revision restores it and applies a set of
review fixes.

### New / restored
- **`code/scripts/00_validate_capture.py`** — restored. Sanity-checks a Phase 0
  capture and emits a single verdict: PASS (exit 0) / REVIEW (exit 1) /
  FAIL (exit 2). Reads the JSONL schema that `01_capture_stages.py` produces
  (no openant import, no hardware). `--markdown` emits paste-friendly output
  for a second opinion in chat. Verified against synthetic PASS / REVIEW /
  FAIL / empty / no-receipt fixtures.
- **Session A checkpoint** re-added to `START-HERE.md` between Session A and
  Session B, and referenced in `README.md` (script list), `code/README.md`
  (Phase 0 usage), and `HANDOFF.md` (step 4).
- **Session C-0 ACK dry run** added to both `03-central-hypothesis-and-phase-zero.md`
  and `START-HERE.md`. De-risks the assumption that the capture can see the
  SB20→crank acknowledged traffic *before* the hard-to-repeat Session C. If
  the dry run shows no inbound traffic, hardening extended-message capture is
  the first Claude Code task.

### Changed — factual / framing
- **Stages status updated throughout.** Stages Cycling's assets were acquired
  by Giant Group (SPIA Cycling) in 2024; there's a discretionary, time-limited
  support program. "Stages went bankrupt / support will run out / no commercial
  harm" framing replaced with the accurate position in `README.md`,
  `01-project-brief.md`, and `08-risks-and-gotchas.md`. The manufacturer-ID
  spoofing ethics note now rests on "private interoperability with hardware you
  own," not "the maker is gone."
- **Power-source consistency promoted to a first-class use case.** The brief
  now leads with using a standard meter (Assioma) to drive the SB20's *erg
  control loop* — not just app display — so indoor targets match outdoor
  efforts. Added open question #7 about whether the SB20 applies internal power
  scaling (which would affect this use case).

### Changed — review fixes
- **H1/H2 ranking refined** in `03-...md`. Assiomas *do* respond to standard
  ANT+ zero-offset calibration, so "Assioma stays silent" is unlikely; the
  likelier H1 variant is the SB20 rejecting the *form* of the response, making
  H1 and H2 roughly co-equal. Documented so Claude Code doesn't anchor on
  "no response."
- **pirower fork guidance corrected** — flagged as GitLab, ~2020, pre-1.0
  openant; reference-only, not a drop-in for openant 1.3.x. Prefer enabling
  extended messages via the 0x66 config message directly.
- **Page 0x50 decode comment fixed** in `01_capture_stages.py` — the comment
  contradicted the (correct) code and the spec; manufacturer_id is at bytes
  4–5. Added a warning not to "simplify" it.
- **SIGALRM note** added to `01_capture_stages.py` — Unix-only; run captures
  in WSL2, not native Windows.
- **Doc cross-reference fixed** — `03-...md` pointed at `09-relationship-to-QZ.md`;
  corrected to `10-relationship-to-QZ.md`.

### Why this matters
The validator and checkpoint are the safety rail for Phase 0; their absence
from the repo meant the owner could have run all six sessions off a bad first
capture. The C-0 dry run protects the single most valuable session. The Stages
status fix keeps the public-facing framing honest now that an active company
owns the IP. The H1/H2 refinement prevents premature anchoring during analysis.

---

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
