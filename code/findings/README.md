# findings/ — the documentation map (find the doc before you build)

This directory is the **source of truth for what's been measured, decided, and built** — captures,
analysis, protocol specs, decision logs. Most of it is **append-only history**: when something breaks
later that worked before, the answer is usually in here.

> 🗺️ **This index is `findings/`-scoped.** For the **whole-repo** view — a *capabilities-already-built*
> inventory + the map of every doc area (`sessions/`, `beta/`, `tools/`, the playbooks) — start at
> [`PROJECT-MAP.md`](../../PROJECT-MAP.md) (repo root), then come here for the protocol/measurement detail.

> ## 🧭 Before you build tooling for — or judge the readiness/availability of — a subsystem, FIND ITS DOC BELOW AND READ IT.
> Check for the **existing script/tooling** the doc names, too. **Don't re-derive or rebuild what's already
> documented.** *(Session 9, 2026-06-26: a `doctor.ps1` nRF-sniffer gate **and** an "install Npcap" call were
> both built on assumptions because nobody opened [`nrf-sniffer.md`](nrf-sniffer.md) — which documents the
> real capture path (`scripts/sniff_ble.py` + Nordic SnifferAPI, **no Npcap**) and the tooling that already
> existed. Time lost to a doc that was right there. CLAUDE.md makes "read the doc first" an invariant; this
> index is where you look.)*

## Orientation — start here
- **[domain-primer.md](domain-primer.md)** — concepts + verified spec facts (CPS / FTMS / ANT+, erg, calibration, pedal meters). New to the domain? Start here.
- **[phase-0-report.md](phase-0-report.md)** — the SB20 crank-spoof spec + overall state of knowledge.
- **[decisions.md](decisions.md)** — append-only chronological log: every value chosen, hypothesis refuted, "it works now". The running source of truth.
- **[forward-plan.md](forward-plan.md)** — the backlog / roadmap (§-numbered open items + future work).

## SB20 crank spoof (the BLE crank we impersonate)
- **[session-G-ble-capture-spec.md](session-G-ble-capture-spec.md)** — what the ESP BLE proxy must reproduce, byte for byte.
- **[shifter-ble-protocol.md](shifter-ble-protocol.md)** — the SB20 shifter-over-BLE map (char `0c46be60`, one-hot gear bitmap).
- **[stages-app-config.md](stages-app-config.md)** — the Stages app's ride modes / profiles / button config (owner recon).
- **[sb20-power-topology.md](sb20-power-topology.md)** — does erg run off the right meter, and is "200 W" really 200 W?
- **[sb20-hardware-reference.md](sb20-hardware-reference.md)** — SB20 internals + behaviour from **community prior art** (PedalSmart.blog): nRF52832 cranks/bike, the ANT+-internal-vs-BLE topology + **why our BLE spoof works** ("Pair with Bluetooth"), **erg is gated on a working Stages crank**, crank-length-on-the-crank (§11 clue), torque/zero-reset facts. *Secondary source — our captures win on conflict.*

## FTMS / erg control
- **[ftms-protocol.md](ftms-protocol.md)** — ⭐ **canonical for FTMS** (service `0x1826`, control point `0x2AD9`, Set Target Power). → `code/src/sb20proxy/ble/ftms.py` + `ftms_erg.py`, `scripts/capture_ftms.py`, `scripts/ftms_workout.py`, `scripts/ftms_hw_loop.py`.
- **[ftms-implementation-plan.md](ftms-implementation-plan.md)** — the spec-built implementation plan behind it.
- **[ride-director.md](ride-director.md)** — the steerable session / erg engine. → `code/src/sb20proxy/ride/`, `scripts/ride_control.py`, `scripts/ride_web.py`.
- **[ride-director-uplift-plan.md](ride-director-uplift-plan.md)** — the uplift build plan for it.
- **[shifter-erg-control.md](shifter-erg-control.md)** — shifter buttons adjust the erg target (the SB20's missing feature).
- **[mcp-workout-server.md](mcp-workout-server.md)** — drive the SB20 erg as agent (MCP) tools: compose a structured workout + drive it live over FTMS. → `code/src/sb20proxy/workout/`, `code/src/sb20proxy/mcp/`, `scripts/mcp_workout_server.py`.
- **[on-device-workout-engine.md](on-device-workout-engine.md)** — *(PLANNED)* the **on-device** workout executor: a structured workout uploaded over a `/workout` route, persisted, and run deterministically as the FTMS erg controller. Canonical format = compact JSON (1:1 with the Python `Segment`); FIT/ZWO are desk-side import formats. Backlog: [`forward-plan.md`](forward-plan.md) §14.

## Capture, sniffing & analysis (BLE + ANT+)
- **[nrf-sniffer.md](nrf-sniffer.md)** — ⭐ **canonical for the nRF BLE sniffer** (passively watch the app↔SB20 link). The capture path is **`scripts/sniff_ble.py`** (Nordic SnifferAPI over the dongle's serial port — **NOT Npcap/tshark**, which is only the GUI alternative); `tools/doctor.ps1` gates the rig. **Read this before any nRF sniffing or capture-rig tooling.**
- **[wsl-capture-runbook.md](wsl-capture-runbook.md)** — ANT+ capture in WSL (usbipd, the `[Errno 13]` perms gotcha, zombie-holder recovery). → `scripts/run_capture.sh`, `scripts/01_capture_stages.py` / `02_capture_assioma.py`.
- **[traffic-observability.md](traffic-observability.md)** — the dual-radio "watch every meter + the SB20 on one clock, across rides" capture strategy.
- **[sqlite-analysis-layer.md](sqlite-analysis-layer.md)** — the rebuildable SQLite index over JSONL/pcap captures (query it, don't dump raw captures into context). → `analysis/pcap_sqlite.py`, `jsonl_sqlite.py`, `scripts/13_build_sqlite.py`.

## Meters & calibration
- **[meter-to-meter-proxy.md](meter-to-meter-proxy.md)** — the corrector mode: read an XCadey, rebroadcast it on the Assioma scale under our own identity.
- **[supported-meters.md](supported-meters.md)** — which power meters work / what we screen testers for.

## Performance & coexistence
- **[perf-coex-plan.md](perf-coex-plan.md)** — on-device load monitoring + the measure→improve→iterate loop (ESP32-C3).
- **[perf-results.md](perf-results.md)** — measured perf/coex iterations (append-only).

## OTA, secrets & home infra
- **[ota-update-plan.md](ota-update-plan.md)** — the firmware-update security plan (authenticated push → signed-pull).
- **[shared-services-adoption.md](shared-services-adoption.md)** — SB20 → cauldnz-pos infra (secrets / observability / local LLMs). → `tools/secrets-*.ps1`, `sb20proxy/obs.py`, `llm.py`.

## Users / beta
- **[beta-program.md](beta-program.md)** — running the pre-beta with ~10 SB20 testers (the live instance of `USERS-PLAYBOOK.md`).
- **[pre-beta-plan.md](pre-beta-plan.md)** — the plan to get to ~10 collaborator-testers.

## Research / roadmap
- **[zwift-controls-research.md](zwift-controls-research.md)** — Zwift integration / controls research.
- **[obc-protocol.md](obc-protocol.md)** — ⭐ **canonical for OpenBikeControl** (MIT): re-present the SB20 buttons to any OBC app (MyWhoosh, qz via #4504) over **BLE** (nRF + ESP) and **mDNS/TCP-UDP** (ESP). Pure codec `firmware/lib/proxy/Obc.h` — host-tested (M1 done); BLE/network transports next. → issue #247.
- **[obc-shifter-sources.md](obc-shifter-sources.md)** — *(research)* the **read side** for OBC: which third-party electronic-shifter spare buttons (Shimano Di2 D-Fly, SRAM AXS Bonus/Blip, Campagnolo EPS) we can listen to and over which transport → **ANT sources need the nRF box, BLE ones fit either**. Feasibility table + OBC action mapping + capture gates. → issue #249.
- **[advanced-board-s3-touch.md](advanced-board-s3-touch.md)** — planning ideas for an optional **"advanced" hardware tier** (Waveshare ESP32-S3-Touch-LCD-1.47): dual-core kills the C3 coex hang, a touch head-unit (no phone), SD capture — reusing the existing proxy core.
- **[cyd-board.md](cyd-board.md)** — the AliExpress **ESP32-2432S028R "Cheap Yellow Display"** (classic ESP32 + 2.8" 240×320 resistive touch, COM17): ported + twin-tested head-unit target (`esp32cyd*` envs, banded no-PSRAM rendering) + the port gotchas (GPIO8 = flash pin!).
- **[architecture-remediation.md](architecture-remediation.md)** — ⭐ **the living structural-cleanup checklist** born from the 3-agent architecture audit (decisions.md 2026-07-10): nRF `main.cpp` seam extraction (R1), the Bridge-GATT JS/Monkey-C parity harness (R2), the versioned config line (R3), field-vocabulary alignment (R4) — plus what's explicitly *not* worth unifying. **Tick boxes here as slices ship.**
- **[ui-unification.md](ui-unification.md)** — ⭐ **the UI "U-series" plan & living checklist** (unify UI across Web + all ESP32 boards): one design-token source (U0 ✅), one view-model (U3 ✅), a host LVGL test harness (U5), the interaction-model parity question (U1 — reframed, with the reasoning why "LVGL defers to lcdHandleTap" is a regression), the wire-contract codegen (U2) + onboarding (U4). The UI sibling of `architecture-remediation.md`. **Read before any head-unit/OLED/web UI structural work.**
- **[nrf-roadmap.md](nrf-roadmap.md)** — ⭐ **the nRF completeness roadmap** (ANT+ · SB20-spoof port · generic-board support). Phased P1–P5 + the spoof port, the owner-action gates (S340/thisisant, Garmin CIQ SDK), and the hardware run-sheets (R1 S340 swap dongle-first · R2 ANT on-air · R3 spoof-on-SB20 · R4 shifter capture). **P1 (the C++ ANT page codec) is DONE.** Read before any nRF ANT / spoof / board work.
- **[nrf52-sense.md](nrf52-sense.md)** — the **Seeed XIAO nRF52840 Sense** (COM18): the BLE(/ANT) track-bike repeater-with-correction + IMU capture board. Toolchain (maxgerhardt `#develop`, `xiaoblesense_adafruit`), the licensed **S340/ANT** path + brick-risk staging, and the Web-Bluetooth/Connect-IQ control surface. → `firmware-nrf/`.

## Also in here
- **[captures/README.md](captures/README.md)** — the index of committed JSONL/pcap capture files (the canonical lossless record — never edit one).
- **screenshots/** — app-UI references (Stages app, Stages Power app, Favero Assioma app).

> Adding a new findings doc? **Add a one-line entry here** in the same change — **CI
> (`code/tests/test_findings_index.py`) fails if a top-level findings doc is missing from this index, or if a
> link here is dead**, so the map can't drift stale. (The doc itself should carry a `Status:` line + name the
> tooling it governs.)

---

## Naming conventions

- Capture files: `<session-letter>-<device>-<scenario>-<YYYYMMDD-HHMM>.jsonl` (BLE pcaps: `.pcap`).
  - Device: `stagesL`, `stagesR`, `assioma`, `xcadey`, `sb20`, `sb20fec`.
  - Scenario: `steady`, `pairing`, `calibration`, `zero`, `erg`, `recon`, `failure-mode`, `endurance`.
- Reports: `phase-N-<topic>.md` / `<subsystem>-<topic>.md`.
- Decision-log entries: prefix with `## YYYY-MM-DD —` and a short title (append-only).

## Why commit captures?

They're not source code, but they're **load-bearing project history** — future debugging and onboarding
refer back to them, and they ground every codec/fixture (real bytes, never invented). On-disk size is small
(a 15-minute power capture is well under 1 MB). If one grows beyond a sensible size, `gzip -k <file>` and
commit the `.gz`.
