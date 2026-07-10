# Architecture remediation plan — the living checklist

**Status:** IN PROGRESS (opened 2026-07-10). Tick boxes here as each slice ships; this doc is the
source of truth for the structural cleanup, so we don't carry it in a chat context window.

**Origin:** the 3-agent architecture audit logged in [`decisions.md`](decisions.md) **2026-07-10
("Architecture audit …")**. Read that entry for the evidence; this doc is the *plan* derived from it.

**Guiding principle — fix the *accidental* fragmentation, leave the *essential*.** Three serializers /
storage backends (NVS `|`-line, one-MTU GATT binary, HTTP JSON) and the scalar-vs-curve correction
*model* are justified by genuinely different transports and are already unified where it matters (the
shared `Correction` math). Don't collapse those. Target the duplication and the missing seams.

**How we execute (per [`DEV-PLAYBOOK.md`](../../DEV-PLAYBOOK.md)):** one slice → prove (host tests +
compile) → ship as its own PR → tick the box here in the same change. Each sub-item below is sized to be
one short-lived branch. Behaviour-preserving refactors must keep the existing tests green (that's the
proof they didn't change behaviour); new seams get their logic host-tested where it was testable before.

---

## ✅ Already shipped (this arc) — the cheap, safe wins

- [x] **Wire-format parity tests** — `code/tests/test_wire_format_parity.py`: `pages.py` ↔ C++
  `AntBikePower.h` golden bytes (ANT), and SPA `OBC_ACTIONS` order ↔ firmware `sb20ActionOptions`.
  (decisions.md 2026-07-10; commit `d58426e`)
- [x] **SPA scale/offset "lying control"** fixed — `caps.scalarCorrection` gates the scalar inputs off on
  curve-only (ESP32) devices. (commit `d58426e`)
- [x] **OBC label drift** (`"−10W"` U+2212 → `"-10W"` ASCII) fixed + locked by the parity test.
- [x] **`forEachFormField` dedup** — the urlencoded-form loop was copy-pasted 5×; one iterator now.
  (commit `e84d189`)
- [x] **`/setup/save` merge fix** — was the only config writer that rebuilt fresh (wiped mode/curve);
  now merges via `mergeSetupForm`. (commit `e84d189`)

---

## R1 — nRF `main.cpp` seam extraction (the real liability)  🔴 highest value

**Problem (audit):** `firmware-nrf/src/main.cpp` is ~1495 lines with **zero seam classes** — it
re-implements *inline* the four seams the ESP32 already has as classes
(`BleCrankPeripheral`/`BleMeterClient`/`FtmsErgClient`/`BleShifterClient`), carries a ~350-line Bridge GATT
service, config persistence, and IMU recording all in one file, and **bypasses the shared `ProxyCore`**.
Every active nRF task (spoof, ANT) edits this file. Goal: convert it to the ESP32's proven shape — a thin
wiring file over seam classes in `firmware-nrf/src/`.

**Order is deliberate — shrink the surface with the low-risk lifts first, do the entangled radio seam
last.** Extracting R1a–R1c removes ~700 lines and is the precondition that makes R1d tractable.

- [ ] **R1a — `BridgeConfigStore`** (~145 lines: `main.cpp:44–137` config/curve/trainer LittleFS +
  `183–212` buttons persistence). Direct mirror of the ESP32 `ConfigStore`. **Lowest risk / first.**
  Move `cfgLoad/Save`, `curveSave/Load`, `trainerSave/Load`, `applyCorrectionFromCfg`, buttons persist
  behind a class owning the `/bridge.cfg`, `/curve.bin`, `/trainer.txt` paths + `g_cfg`/`g_corr`. Pure
  pack/unpack is already host-tested in `test_bridge`; the LittleFS glue is the seam. **Prove:** both nRF
  envs compile; `pio test -e native` (nRF) still green.
- [ ] **R1b — `BridgeService`** (~350 lines: the GATT control/telemetry callbacks `main.cpp:691–975` +
  char decls `243–269` + the `setup()` begin block). One cohesive service over one already-pure protocol
  (`lib/bridge/Proto.h`); self-similar write/notify handlers touching a bounded global set
  (`g_cfg`,`g_corr`,`g_cal`,`g_wk`,`g_cap`). **Prove:** compile both envs; native green; a bench GATT
  round-trip once hardware is back (R-gated).
- [ ] **R1c — `ImuRecorder`** (~200 lines: `pumpDownload`, `imuSelfTest`, `recCtlWriteCb`, IMU globals,
  loop pacing). Buffer is already pure (`lib/bridge/ImuCapture.h`); this is the hardware + BLE-download
  glue. **Prove:** compile; `IMUTEST` serial self-test once on hardware.
- [ ] **R1d — `IRadioSource`/`IRadioSink` seam** (~560 lines: `scanCb`, `measNotifyCb`, the erg state
  machine, `centralConnect/Disconnect`, `cpWriteCb`, the advertising build). Split into
  `ble/BleCpsSource` + `ble/BleCpsPeripheral` (mirror `BleCrankPeripheral`) + `ble/FtmsErgClient` (mirror
  the ESP32 class name). **Biggest win + the roadmap's actual target, but riskiest** — `scanCb`/
  `centralConnectCb` multiplex four centrals (source/ref/trainer/SB20) by peer-name; pull the routing into
  a small dispatcher first. **Payoff:** the nRF can then adopt the shared `ProxyCore` and delete the
  hand-rolled read→correct→relay in `measNotifyCb`. **Do last.** Ties into the nRF roadmap P4 radio seam.
- [ ] **R1e — ESP32 `LcdController`** (~590 lines: `firmware/src/main.cpp:240–828` — the LCD head-unit
  controller: `buildLcdViews`, `lcdExecute`, `lcdTask`, the touch-cal ritual, `lcdSerialConsole`). The
  ESP32's *one* fat tenant; everything else there is legitimate wiring. Start with the zero-risk
  **touch-cal ritual** (`279–378`) as a standalone `disp/TouchCalRitual`. **Prove:** the LCD envs
  (`esp32s3-pio`, `esp32cyd`) compile; screens sweep on hardware (R-gated).

---

## R2 — Bridge GATT JS + Monkey-C parity harness  🟠 highest silent-drift blast radius

**Problem (audit #2):** the SPA's JS `parse*/pack*` (`web/index.html`) and the Garmin `BridgeBle.mc`
hand-code `DataView`/byte offsets for all 9 Bridge packet types with **no test against `Proto.h`** — a
single offset change silently corrupts the *primary user control surface* with zero CI signal
(`test_spa_sync` only checks the embedded copy is in sync, not that the bytes are right). The idiom to fix
it exists (`test_wire_format_parity.py`); the blocker is that the JS codec isn't executable in isolation.

**Needs an owner decision (records the choice here before building):**
- [ ] **R2-decision — how to make the JS Bridge codec testable.** Options: (a) extract the `parse*/pack*`
  functions into a small ES module the SPA imports *and* a Node test imports (adds Node to CI); (b) a
  headless-browser test; (c) generate a shared golden-vectors JSON from the C++ side and assert both the
  C++ test and a Node test against it. **Recommendation: (a)+(c)** — a `web/bridge-codec.js` module + a
  committed `web/bridge-golden.json` both sides check. Requires a Node step in `tests.yml`.
- [ ] **R2a — extract `web/bridge-codec.js`** (the Bridge parse/pack only; keep the SPA a single served
  file via the existing `gen_spa_header` inlining, or import at build). Regenerate `WebSpa.h`; keep
  `test_spa_sync` green.
- [ ] **R2b — shared golden vectors + Node parity test** — emit canonical `Proto.h` byte vectors, assert
  `bridge-codec.js` decode/encode matches; wire the Node job into CI.
- [ ] **R2c — Monkey-C** (`BridgeBle.mc`): lower priority (no CI build today, needs the Garmin SDK). At
  minimum, a doc note + the shared golden JSON as the reference; a `monkeyc` CI job is a stretch.

---

## R3 — `RuntimeConfig` `|`-line: version it  🟡 contained but a real trap

**Problem (audit #3):** the NVS line is 16 positional fields, **untagged (no version byte** — unlike the
nRF's `PROTO_VER`), append-only *by convention only*, and delimiter-injectable (mitigated solely by the
easily-forgotten `stripConfigDelims`). A middle insert or a missed strip corrupts every later field
silently. Contained (one production round-trip, `ConfigStore`) — so this is a maintenance-hazard fix, not
an active bug.

- [ ] **R3a — add a leading version tag** to `toLine()`/`fromLine()` (e.g. `v2|…`), with `fromLine`
  accepting the untagged legacy line as v1 (count-based, as today) so stored NVS keeps loading. Host-test
  the v1-legacy + v2 round-trips. Low risk, removes the "count == age" fragility.
- [ ] **R3b (optional)** — a tiny `field(name)` accessor / named-offset map so future fields can't be
  read from the wrong slot; or leave positional but documented. Decide after R3a.

---

## R4 — Config field-name vocabulary  🟢 friction, low risk

**Problem (audit #4):** the same concept is spelled three ways —
`meterNameFilter`/`srcFilter`/`src_filter`; `spoofName`/`outName`/`out_name`; mode as enum/bool/string —
forcing a translation layer (`renderConfigJson`/`getConfig`) and a mental map on every reader.

- [ ] **R4a — pick a canonical vocabulary** (recommend the SPA's normalized names as canonical, since
  they're the user-facing surface) and document the field matrix in one place (extend the table in
  decisions.md 2026-07-10 or `web/HTTP-API.md`).
- [ ] **R4b — align names where cheap** (rename within a single language where it doesn't cross a wire
  contract; the *wire* field names stay per-transport but the internal struct fields converge). Do not
  churn the wire formats. Low priority; do opportunistically alongside other work in each file.

---

## Explicitly NOT doing (essential divergence — leave alone)

- The three config **serializers / storage backends** (NVS line vs GATT binary vs HTTP JSON) — justified
  by transport constraints; a single wire format would be worse on ≥2 of them.
- The scalar-vs-curve **correction model** difference — a product decision (nRF exposes scalar knobs, the
  ESP32 doesn't), already unified in the shared `Correction::apply` math.
- Collapsing the two config **UIs** (`/setup` page vs the SPA) — gated on the SPA-over-HTTP path being
  hardware-verified first (it's "unverified until U4"); retiring `/setup` before the replacement is proven
  would be the wrong order.

---

*Maintenance: tick a box in the same PR that lands the slice. When an item completes, add a one-line
"done — commit `<sha>`" next to it. Promote any durable finding to `decisions.md`. If we later prefer
GitHub issues, mirror the unchecked R-items there and link back here.*
