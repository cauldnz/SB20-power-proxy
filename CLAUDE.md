# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

Read a power meter → correct it → re-broadcast it so a consumer accepts it as its own. Primary form: an **ESP32-C3 BLE firmware** carried on the bike, with Python desk-tooling alongside. Original target: let a Stages SB20 smart bike run its erg loop off a third-party meter (Favero Assioma) by impersonating its native Stages crank. See §Architecture below for the full picture.

## Where to start (current canonical docs)

- **`code/findings/domain-primer.md`** — **new to smart bikes / power meters / BLE fitness protocols? Start here.** General concepts + verified spec facts (CPS/FTMS/ANT+, erg, calibration, pedal meters) so you're grounded without relying on model-inherent knowledge; the project's *measured* bytes live in the docs below and win on conflict.
- **`code/findings/decisions.md`** — append-only chronological log; the source of truth for what's decided/found (every numeric value, refuted hypothesis, "it works now").
- **`code/findings/phase-0-report.md`** — the spoof spec + state of knowledge.
- **`sessions/README.md`** — the session ledger (physical sessions run / planned); **`sessions/PLAYBOOK.md`** — how to run an on-bike one; **`DEV-PLAYBOOK.md`** — the desk dev loop (how we slice/prove/ship software); **`USERS-PLAYBOOK.md`** — how we work with testers/users/customers. The DEV/USERS playbooks are **living** — extend them as we learn.
- **Protocol/feature references in `code/findings/`** — `shifter-ble-protocol.md`, `stages-app-config.md`, `meter-to-meter-proxy.md`, `zwift-controls-research.md`, `forward-plan.md` (backlog).
- The numbered root docs (`01-…`–`10-…`, `HANDOFF.md`, `START-HERE.md`) are the **pre-pivot brief** — useful background, but superseded by the findings docs above (per `README.md`).

## Setup

Python desk-tooling (Linux / WSL Ubuntu), from `code/` — build/run/test commands are in §Commands:

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install -e ".[dev,analysis,ble]"
# udev rule for ANT+ sticks (openant's own helper errors — write it directly):
sudo tee /etc/udev/rules.d/42-ant-usb-sticks.rules >/dev/null <<'RULE'
SUBSYSTEM=="usb", ATTRS{idVendor}=="0fcf", MODE="0666"
RULE
sudo udevadm control --reload-rules && sudo udevadm trigger   # then re-plug the stick
```

Windows needs USB passthrough to WSL (`START-HERE.md` / `07-hardware-and-environment.md` §"Windows + WSL"). Firmware (ESP32-C3) is PlatformIO.

## Commands

**Python (desk tooling + tests) — from `code/`** (after `pip install -e ".[dev,analysis,ble]"`):
- `pytest -q` — the hermetic suite (no hardware/network). One file: `pytest tests/test_ble_cps.py -q`;
  one test by name: `pytest -k cadence`.
- `ruff check src tests` — lint (this is the CI scope; `scripts/` is **not** linted).
- `python scripts/03_static_replay.py --radio loopback --input findings/captures/<cap>.jsonl` — the
  **keystone hardware-free check**: replays a real capture through the proxy + an in-process twin
  ("PASS" = the spoof path works end-to-end). `--radio ant` uses the real stick.
- Capture (needs the stick / BLE): `01_capture_stages.py`, `02_capture_assioma.py`, `06_capture_ble.py`
  (BLE — `--subscribe-all`, `--control-point`), `07_capture_multi.py` (`--meter LABEL:ANTID` ×N, the
  paired meter-to-meter capture), `capture_ftms.py` (the SB20's FTMS surface).
- Calibration: `09_fit_calibration.py --target X --ref Y` → a profile JSON; `08_analyze_grid.py` /
  `12_compare_fit.py` validate it. `04_run_proxy.py --profile <json>` runs the ANT+ proxy;
  `python scripts/ride_web.py` is the ride-day dashboard. `sb20proxy` is the console entry (`cli.py`).

**Firmware (ESP32-C3) — from `firmware/`** (PlatformIO):
- `pio test -e native` — host unit tests for the pure proxy core (no board; runs in CI).
- `pio run -e esp32c3-oled-live-ota` — compile the live OLED build. **The ESP32 target compile is the
  pre-flash gate** — always do it before flashing.
- Flash: `firmware/flash.ps1` (OTA — RSSI pre-flight + auto-retry + reboot verify) or `flash.ps1 -Mode usb`;
  raw `pio run -e <env> -t upload`. Envs + on-bike checklist: `firmware/BENCH-FLASH.md`.
- Observe a running board: `http://sb20proxy.local/` (dashboard; `/ui` aliases it) · `curl …/status`
  (status JSON) · `/setup` (pick the meter/crank source over WiFi → NVS) · `/log` (serial-over-HTTP
  — the main live-session instrument) · `/stats` (loop-perf).

## Architecture (big picture)

**Two codebases for one idea: read a power meter → correct it → re-broadcast it so a consumer accepts it
as its own.** Both mirror a `ProxyCore` (`source → correction → target`) with hardware behind a seam.

- **`firmware/` — the on-device runtime (ESP32-C3, BLE) — the primary product.** A **dual-role** BLE
  device: a **central** that subscribes to a real power meter (Cycling Power Service `0x1818`) *and* a
  **peripheral** that re-presents the corrected power. Split into a **pure, host-tested core** in
  `firmware/lib/proxy/` — `Cps.h` (CPS measurement + control-point codec), `ProxyCore.h` (the
  read→correct→rebroadcast wiring), `Correction.h` (scale/offset/curve), `Config.h` (the read/spoof
  identity + cal values), `OledScreen.h`, `PerfMonitor.h` — and a thin **hardware seam** in
  `firmware/src/`: `ble/BleMeterClient` (central), `ble/BleCrankPeripheral` (peripheral + the control-point
  responder the SB20 demands), `net/WifiLink` (captive portal + the `/`,`/ui`,`/log`,`/stats` HTTP),
  `disp/` (OLED). The core compiles and unit-tests with no radio; only the seam needs a board. Build
  flavours via `platformio.ini` envs: mock-meter (ramp) vs `*-live` (reads a real meter), ± OLED, ± OTA.
- **`code/` — Python desk tooling + the original ANT+ proxy.** The `sb20proxy` package mirrors the same
  flow over **ANT+**: `sources/` (read), `targets/` (re-broadcast, e.g. `stages_ant`), `core.py`
  (`ProxyCore`), `ble/cps.py` (the Python CPS codec — the twin of `Cps.h`), `ant/` (openant master +
  page codecs), `calibration.py` + `transform.py` (the correction model), `ride/` (a ride-director + web
  app), `twins/` (in-process fakes so the proxy is hermetically testable). The numbered `scripts/` are the
  capture / fit / replay / proxy entry points (see §Commands).

**Two product modes** (same core, different identity + correction):
1. **SB20 crank spoof** — *must* impersonate the Stages L crank (`Stages 62144`, byte-faithful `0x2F`
   framing, answer the control point) because the SB20 only accepts its own crank; feeds the bike's erg
   loop from a third-party meter. The active bike-session work.
2. **Meter-to-meter corrector** — read e.g. an XCadey, re-broadcast on the Assioma scale under our **own**
   identity (no spoof — head units accept any CPS meter). See `code/findings/meter-to-meter-proxy.md`.

**The real-data pipeline is the spine:** on-bike **captures** (committed JSONL in `findings/captures/`) →
**codecs/fixtures** (golden-vector tests) → **fit** a correction (`calibration.py`, from paired captures)
→ **deploy** into the firmware `Correction`. Protocol facts live in `code/findings/` docs — `decisions.md`
(append-only log), `phase-0-report.md` (the spoof spec), and the `*-protocol.md` / `stages-app-config.md`
references for the shifter, FTMS, and app surfaces. Nothing is built ahead of the capture that grounds it.

## Engineering disciplines

These are the **invariants** (non-negotiable). For the *working process* that applies them — how to
slice a feature, prove it, and ship it across small PRs — see **[`DEV-PLAYBOOK.md`](DEV-PLAYBOOK.md)**,
and fold each session's lessons back into it so the next session starts ahead.

Invariants this project depends on — follow them:

- **Real-data-first / capture before code.** Don't build a codec or correction ahead of the on-bike capture that grounds it (Phase 0 is mandatory). Fixtures come from the **real committed captures** in `code/findings/captures/`, never invented bytes.
- **JSONL captures are the canonical lossless record** — never edit one; derive summaries/diffs/analyses from it.
- **Document protocol bytes, not Python idioms.** A "what to spoof" spec is a protocol doc (page/byte layouts, calibration-response shape) that ports across languages — not code.
- **`code/findings/decisions.md` is append-only** — log every numeric value chosen, hypothesis refuted, and "it works now"; future debugging relies on it.
- **Test the desk-testable in the same commit.** Any logic that runs without the stick/SB20 (codecs, parsers, file replay, `ProxyCore` wiring, calibration-byte construction) ships with `pytest` / `pio test -e native` tests in the same change — no "tests later". Isolate hardware behind a seam (injectable radio / `FakeRadio`) so its logic is unit-tested with a fake; only the final on-air / pairing check is manual. Keep the suite **hermetic** (no hardware/network) and green, and `ruff check src tests` clean — CI runs both on every push.
- **MIT — clean-room.** Read GPL prior art (qdomyos-zwift, SHIFTR, bikecontrol) to *understand*; never copy — reimplement.

**Verification gotchas:** `code/tests/conftest.py` exposes `capture_pages` (iterate a real capture's pages for golden-vector tests). Touching openant? verify against the installed version *and* its source on disk before assuming an API exists. Capture/analysis scripts → smoke-test against a synthetic JSONL fixture. **Nothing replaces hardware testing against a real SB20** — the bench loopback + pairing are manual; CI covers only the replay / codec / wiring layer.

## Git & branch hygiene (one human, several concurrent Claude sessions)

`main` is the single source of truth; work lands via PR. One dev, but **multiple Claude sessions share this repo at once** — a long-lived branch once drifted from `main` while other sessions advanced it and a capture-grounded fix was nearly lost. Hence:

- **Sync before you touch anything** — `git fetch origin`; if `origin/main` moved, recut/rebase onto it *first*. Never assume the tree or your branch is current (another session may have merged since).
- **Cut branches fresh from up-to-date `origin/main`; never resurrect a prior session's branch** (assume it diverged — diff it first). A branch is **one task**, not one topic.
- **One task → one short-lived branch → PR → green CI → merge → delete**, same session. Direct pushes to `main` are blocked by design; merge promptly, don't let branches outlive the task.
- **Before ANY PR merge, `git fetch` and survey *all* open branches/PRs first** (`gh pr list`) — concurrent sessions may have opened PRs for work you're about to do (don't duplicate it) or merged something that leaves the PR you're about to merge stale/conflicting. Confirm the PR is current with `origin/main` (its CI ran on the current base); if it's behind, merge `origin/main` into it and re-run CI **before** merging. A green check from an old base is not enough.
- **Concurrent sessions coordinate only through `origin/main`** — avoid two sessions editing the same files; rebase often so conflicts stay small.
- **Reconciling divergent work: real data wins, never silently drop the other line's finding.** If two branches disagree on a captured/numeric value (e.g. BLE cal-offset `0` vs ANT+ `903`), verify against the actual capture in `findings/captures/` before choosing; if both defensible, ask. Port the **specific delta**, never a pre-PR branch wholesale (it deletes newer work).
- **Flash/build only from firmware confirmed current with `origin/main`** — the wrong build wastes a session and can ship a wrong value (e.g. the ANT+ offset on a BLE crank). The local ESP32 compile is the pre-flash gate (§Commands).

## Session plans & the session ledger (bike / physical-interaction work)

Any session touching hardware we can't drive from the desk (bike, flash/pair, on-air test) is **planned and recorded in one doc** — the plan *and* what actually happened — and tracked in **one ledger**.

- **Run it via the playbook — [`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md):** plan (desk-derisk, front-load the gates, pre-stage turnkey) → execute (one step at a time, explicit pass/fail, timestamp + record actuals) → document → **retro**. The rider's **time and patience are the budget**; never send them to do something you haven't verified is ready.
- **One ledger — [`sessions/README.md`](sessions/README.md):** every physical session is a row (number, date, status, outcome, link). It alone tells you the state of play.
- **Each session doc is Plan *and* Actual** — annotate each step in place (`✅`/`❌`/`⚠️` + observed bytes/values/`/log`), keep a `Status:` header (`PLANNED → IN PROGRESS → ✅ DONE (date)` / `⛔ SUPERSEDED`), and flip to DONE with a one-line Outcome. Don't leave results only in chat.
- **Promote durable findings** to `findings/decisions.md` (append-only) + commit captures to `findings/captures/` — those are canonical; the session doc is the narrative.
- **No detritus / one open session at a time.** New docs live in `sessions/`; completed ones stay (marked DONE) and are linked from the ledger — no ad-hoc `NEXT-`/`READY-` variants. Don't draft session N+1 until N is DONE. (Legacy `BIKE-SESSION-*.md` stay at root because append-only `decisions.md` links them.)
- **Improve the playbook after every session** — fold the retro's lessons back into `PLAYBOOK.md`; it's a living, compounding doc. A session whose lessons never reach the playbook is only half-closed.
