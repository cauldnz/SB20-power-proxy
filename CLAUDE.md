# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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
- Observe a running board: `curl http://sb20proxy.local/` (status JSON) · `/ui` (dashboard) · `/log`
  (serial-over-HTTP — the main live-session instrument) · `/stats` (loop-perf).

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

Invariants this project depends on — follow them:

- **Real-data-first / capture before code.** Don't build a codec or correction ahead of the on-bike capture that grounds it (Phase 0 is mandatory). Fixtures come from the **real committed captures** in `code/findings/captures/`, never invented bytes.
- **JSONL captures are the canonical lossless record** — never edit one; derive summaries/diffs/analyses from it.
- **Document protocol bytes, not Python idioms.** A "what to spoof" spec is a protocol doc (page/byte layouts, calibration-response shape) that ports across languages — not code.
- **`code/findings/decisions.md` is append-only** — log every numeric value chosen, hypothesis refuted, and "it works now"; future debugging relies on it.
- **Test the desk-testable in the same commit.** Any logic that runs without the stick/SB20 (codecs, parsers, file replay, `ProxyCore` wiring, calibration-byte construction) ships with `pytest` / `pio test -e native` tests in the same change — no "tests later". Isolate hardware behind a seam (injectable radio / `FakeRadio`) so its logic is unit-tested with a fake; only the final on-air / pairing check is manual. Keep the suite **hermetic** (no hardware/network) and green, and `ruff check src tests` clean — CI runs both on every push.
- **MIT — clean-room.** Read GPL prior art (qdomyos-zwift, SHIFTR, bikecontrol) to *understand*; never copy — reimplement.

**Verification gotchas:** `code/tests/conftest.py` exposes `capture_pages` (iterate a real capture's pages for golden-vector tests). Touching openant? verify against the installed version *and* its source on disk before assuming an API exists. Capture/analysis scripts → smoke-test against a synthetic JSONL fixture. **Nothing replaces hardware testing against a real SB20** — the bench loopback + pairing are manual; CI covers only the replay / codec / wiring layer.

## Git & branch hygiene (one human, several concurrent Claude sessions)

There is a **single developer** here, but often **multiple Claude sessions sharing this repo at the same time**, with work landing on `main` via PRs between sessions. That combination once produced a painful divergence — a long-lived branch drifted from `main` while other sessions advanced it, then had to be reconciled commit-by-commit, and a capture-grounded fix was almost lost. These rules keep `main` the single source of truth and branches cheap and short-lived:

- **`main` is the source of truth; treat every branch as disposable.** Long-lived feature branches are *the* hazard — they silently diverge from whatever other sessions merged. A branch lives for **one task**, not one "topic."
- **Sync before you touch anything.** At the start of a session/task, `git fetch origin` and check whether `origin/main` (and your current branch) moved. If `origin/main` advanced, recut or rebase onto it **before** working. Never assume the working tree or your branch is current — another session may have merged since.
- **Cut branches fresh from an up-to-date `origin/main`; never resurrect a previous session's branch** — assume it has diverged. If you start on an existing `claude/*` (or any older) branch, `git fetch` and diff it against `origin/main` before adding to it; if it's behind, branch off `origin/main` instead.
- **One task → one short-lived branch → PR → merge → delete, within the session where possible.** Don't leave branches lying around for the next session to trip over. Direct pushes to `main` are blocked by design — PR + green CI is the path — but merge promptly and don't let the branch outlive the task.
- **Concurrent sessions coordinate only through `origin/main`.** Don't assume changes already in the tree are yours (`git status` / `git fetch` first). Avoid two sessions editing the same files; if unavoidable, rebase on `main` often so conflicts stay small.
- **Reconciling divergent work: real data wins, and never silently drop the other line's finding.** When two branches disagree on a captured or numeric value (e.g. the BLE cal-offset `0` vs the ANT+ `903`), **verify against the actual capture in `findings/captures/` before choosing** — a capture-grounded value beats "but it was bike-tested." If both look defensible, ask rather than drop. This is *capture before code* / real-data-first applied to merges. When porting one branch's fix onto another, port the **specific delta**; never take a pre-PR branch wholesale (it will delete newer work).
- **Flash/build only from firmware you've confirmed is current with `origin/main`.** Before flashing the bike, check the source isn't a superseded branch — flashing the wrong build wastes a session and can ship a wrong value (e.g. the ANT+ offset on a BLE crank). The local ESP32 compile is the pre-flash gate (see §Commands).

## Session plans & the session ledger (bike / physical-interaction work)

Any session that touches hardware we can't drive from the desk — a bike session, a flash/pair run, an on-air test — is **planned and recorded in the repo**, with the plan and what actually happened in the **same doc**, and every session tracked in **one ledger**. History stays valuable without leaving a scatter of stale files.

- **How to run one well: the playbook — [`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md).** Plan (desk-derisk, front-load the gates, pre-stage turnkey) → execute (one step at a time, explicit pass/fail, record actuals) → document → **retro** (turn what went wrong *and* right into a concrete change before the next session). The rider's **time and patience are the budget**; never send them to do something you haven't verified is ready. Read it before directing a session.
- **One ledger: `sessions/README.md`.** Every physical session is a row there — number, date, status, one-line outcome, link to its doc. Check the ledger to know what's current, what's done, and what each session concluded; nothing else needs scanning to see the state of play.
- **Each session doc is Plan *and* Actual.** It starts as the plan; while guiding the session live, **annotate each step in place** with what actually happened — `✅` pass / `❌` fail / `⚠️` partial, plus the observed bytes / values / UI / `/log` lines. Don't leave the result only in chat; write it back. The plan text stays — Actual is added next to it, not swapped in.
- **Status header, always:** `Status: PLANNED → IN PROGRESS → ✅ DONE (YYYY-MM-DD)` (or `⛔ SUPERSEDED → <successor>`). At the end, flip to DONE and add a one-line **Outcome** at the top.
- **Promote durable findings.** The session doc is the blow-by-blow narrative; lasting facts (a confirmed byte value, "the SB20 ergs off our crank", a refuted hypothesis) are also appended to `findings/decisions.md` (append-only) and captured bytes committed to `findings/captures/`. Those are the canonical record.
- **No detritus.** New session docs live in `sessions/`; completed ones stay there marked DONE (history is valuable — don't delete) and are linked from the ledger. Don't spawn ad-hoc `NEXT-`/`READY-` variants per session — one ledger, one doc per session. (Legacy `BIKE-SESSION-*.md` / `NEXT-BIKE-SESSION.md` stay at the repo root because the append-only `decisions.md` links them; the ledger tracks them in place.)
- **One open session at a time.** Don't draft session N+1 until N is DONE — keeps "what's current" unambiguous.
- **Improve the playbook after every session.** Closing a session includes reviewing its retro and folding the durable lessons back into `sessions/PLAYBOOK.md` (the rules + the §Lessons section) — it's a **living, compounding** doc, not a write-once one. A session whose lessons never reach the playbook is only half-closed; the whole point is that each session is better-prepared than the last.
