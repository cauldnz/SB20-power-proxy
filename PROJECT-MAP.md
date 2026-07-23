# PROJECT-MAP — read this before planning or building anything

**This is the index of what already exists — shipped *capabilities* AND every doc — so a planning
loop extends what's there instead of rebuilding it.** It's the one map that spans the whole repo;
the per-area indexes ([`code/findings/README.md`](code/findings/README.md), [`sessions/README.md`](sessions/README.md))
are the deep dives it points into.

> **Why this exists (a repeated, costly failure):** planning loops kept reading only the `findings/`
> index and missing whole areas — twice nearly **rebuilding things already built** (the meter-to-meter
> corrector; the beta collaboration loop, which was ~95% done). The fix: a map with **summaries of what
> works**, not just file titles. **Before you build, find the capability below; if it's `✅`, you're
> extending, not creating.** CI (`code/tests/test_project_map.py`) fails if a `beta/` or `sessions/` doc
> isn't mapped here, so this can't drift stale.

Status key: ✅ built & working · ⚙ partial / hardening · 🔒 built but blocked on external infra · 🔲 not built (capture- or hardware-gated)

---

## A. Capabilities already built (the anti-rebuild inventory)

### SB20 crank spoof — the product (read a meter → re-present as the Stages crank)
| Capability | Status | Lives in |
|---|---|---|
| Dual-role BLE proxy: subscribe to a meter's CPS `0x2A63` → rebroadcast as `Stages 62144` | ✅ | `firmware/lib/proxy/` (`ProxyCore.h`,`Cps.h`), `firmware/src/ble/` · [phase-0-report](code/findings/phase-0-report.md), [session-G-ble-capture-spec](code/findings/session-G-ble-capture-spec.md) |
| Byte-faithful Stages `0x2F` framing + feature set + DIS identity | ✅ | `Cps.h`, `BleCrankPeripheral` |
| Control-point responder: pair + **calibrate/zero-reset handshake** (company-id 442) | ✅ | `Cps.h` · sessions [8](sessions/session-08-sb20-spoof-calibration.md)/[9](sessions/session-09-zero-reset-onair-confirm.md), [decisions](code/findings/decisions.md) |
| Zero-reset **forwards** to the real Assioma (real offset comp) | ✅ | `BleMeterClient::requestZeroOffset` · forward-plan §10 |
| L/R balance forwarding (plumbed; the *value* grounding from a capture is pending) | ⚙ | `Cps.h`/`ProxyCore.h` · forward-plan §"balance" |
| Bidirectional **crank-length** bridge (app↔meter) | 🔲 | capture-gated — [forward-plan §11](code/findings/forward-plan.md) + its capture recipe |
| Sole-source pairing rule (SB20 needs *both* crank IDs findable) | ✅ understood | forward-plan §12 |

### Config & UX — user-configurable, no rebuild (pre-beta Phase 1, ✅)
| Capability | Status | Lives in |
|---|---|---|
| `/setup` web UI: BLE-scan → pick source → choose crank identity → **NVS** | ✅ | `firmware/src/net/`, `Provisioning.h`, `ConfigPage.h` · [pre-beta-plan](code/findings/pre-beta-plan.md) |
| Source pinning by address; single-sided **×2** toggle | ✅ | `MeterMatch.h`, `BleMeterClient` |
| Dashboard + instruments: `/` `/ui` `/log` `/stats` `/status` `/diag` | ✅ | `WifiLink`, `WebApp.h`, `DiagReport.h`, `LogBuffer.h` |
| **Ride-mode WiFi-off** + coex hardening (PerfMonitor, watchdog, OLED off hot loop) | ✅ | `PerfMonitor.h`,`PerfStats.h` · [perf-coex-plan](code/findings/perf-coex-plan.md) |
| SoftAP per-device PIN (screen) / default passphrase (screenless) | ✅ | `SetupPin.h` |
| **Hard-reset WiFi onboarding, all three head-units** — captive portal (WPA2 AP + PIN), QR join screen on the LCD boards (LVGL `LV_USE_QRCODE`), per-board mDNS hostnames (`sb20proxy` / `-cyd` / `-s3`) | ✅ owner-proven via QR | `WifiLink` portal + `ProvisionView`/LVGL · decisions.md 2026-07-04 |
| **LCD device picker** — CPS/FTMS-only filtered list (`lcdPickerList`, host-tested), crank/meter/trainer labels, Rescan scan-window boost | ✅ | `SourceCandidate.h` · PR #214 |

### Meter-to-meter corrector — the second product mode (✅ M1–M5)
| Capability | Status | Lives in |
|---|---|---|
| On-device calibration wizard: read 2 meters → fit curve → corrected rebroadcast under our OWN identity | ✅ | `CalibrationFit.h`,`CalibrationSession.h`,`CalibrationPage.h` · [meter-to-meter-proxy](code/findings/meter-to-meter-proxy.md) |
| Python fit pipeline (scale/offset + grid) + apply | ✅ | `calibration.py`,`transform.py`, `scripts/09_fit_calibration.py`,`08_analyze_grid.py` |

### Beta collaboration loop & packaging (✅ — capture→support→OTA)
| Capability | Status | Lives in |
|---|---|---|
| Tester `/diag` report (config + status + **raw CPS frames**) → `parse_diag.py` → golden-vector stub | ✅ | `DiagReport.h`, `scripts/parse_diag.py`, `analysis/diag.py` · [beta-program](code/findings/beta-program.md), [supported-meters](code/findings/supported-meters.md) |
| Tester **review-&-send** report page `/report` (review on-device → Download/Copy/Email; consent-first) | ✅ | `WebApp.h` `diagReportPageHtml`, `WifiLink` |
| Pre-ship QA acceptance card | ✅ | `scripts/qa_board.py`, `sb20proxy/qa/acceptance.py` |
| Tester kit: onboarding, ride protocol, **ride-feedback form**, recruiting, pitch posts, comms | ✅ | [`beta/`](beta/ONBOARDING.md) |
| Release & fleet-OTA runbook + firmware **version stamping** | ✅ | [`beta/RELEASE-AND-OTA.md`](beta/RELEASE-AND-OTA.md), `Config.h` `SB20_FIRMWARE_VERSION` → `/status`,`/diag` |

### FTMS / erg / training (✅ — long-tail per the locked value-prop)
| Capability | Status | Lives in |
|---|---|---|
| FTMS codec (Indoor Bike Data + Control Point) + `ErgController` + Ride-erg bridge | ✅ | `ble/ftms.py`,`ble/ftms_erg.py`, `Ftms.h` · [ftms-protocol](code/findings/ftms-protocol.md) |
| Ride Director: dynamic plan engine, agent control API, %FTP/Coggan zones, phone UI | ✅ | `sb20proxy/ride/` · [ride-director](code/findings/ride-director.md) |
| MCP workout server (compose + drive an erg workout as agent tools) + interval driver | ✅ | `sb20proxy/workout/`,`sb20proxy/mcp/`, `scripts/{mcp_workout_server,ftms_workout}.py` · [mcp-workout-server](code/findings/mcp-workout-server.md) |
| Shifter-buttons-adjust-erg mapper | ✅ | `Shifter.h` · [shifter-erg-control](code/findings/shifter-erg-control.md) |
| **On-device FTMS erg drive** — the head-unit workout engine erg-drives a trainer directly (no phone/PC): `trainerNameFilter` in RuntimeConfig, Setup-screen pick, serial `TRAINER <name>`, optimistic CP progression | ✅ twin-proven (CYD↔C3 sim) | `FtmsErgClient` + `BleMeterClient` scan hub + `main.cpp` §14 phase 4 · decisions.md 2026-07-05 |
| FTMS trainer sim on a spare board (the erg bench oracle: logs controlled/started/target) | ✅ | `esp32c3-ftms-server` env → COM10 · `FtmsTrainerServer` |

### OTA & security (✅ lockdown · 🔒 signed-pull blocked on backend)
| Capability | Status | Lives in |
|---|---|---|
| Security lockdown: CSRF guard, authenticated ArduinoOTA, no open `/update` | ✅ | `HttpSecurity.h`, `WifiLink` · [ota-update-plan](code/findings/ota-update-plan.md) |
| Authenticated **push-OTA** (the channel that works today) | ✅ | `firmware/flash.ps1` + `firmware/ota_secret.h` |
| Signed-**pull** OTA core (manifest + ed25519/BLAKE2b verify + updater) + signer | 🔒 | `OtaManifest.h`,`OtaVerify.h`,`OtaUpdater.h`, `scripts/ota_sign.py` — needs the unRAID/GitHub backend + keygen |

### ANT+ path (✅ — the original Pi proxy, superseded by BLE for the product)
| Capability | Status | Lives in |
|---|---|---|
| ANT+ master spoof + static replay + digital twins (loopback + BikeTwin) | ✅ | `sb20proxy/{ant,sources,targets,twins}/`, `scripts/{03_static_replay,04_run_proxy,10_bike_twin}.py` · [forward-plan §2](code/findings/forward-plan.md) |

### Capture / analysis / infra
| Capability | Status | Lives in |
|---|---|---|
| nRF BLE sniffer (app↔SB20 passive) | ✅ | `scripts/sniff_ble.py` · [nrf-sniffer](code/findings/nrf-sniffer.md) |
| ANT+ + BLE capture tooling | ✅ | `scripts/0[1267]_capture_*.py`,`capture_*.py` · [wsl-capture-runbook](code/findings/wsl-capture-runbook.md), [traffic-observability](code/findings/traffic-observability.md) |
| Rebuildable SQLite index over captures | ✅ | `analysis/pcap_sqlite.py`,`jsonl_sqlite.py`, `scripts/13_build_sqlite.py` · [sqlite-analysis-layer](code/findings/sqlite-analysis-layer.md) |
| Dev toolchain + capture-rig gate; Infisical secrets | ✅ | [`tools/`](tools/README.md) (`provision-dev-env.ps1`,`doctor.ps1`,`secrets-*.ps1`) · [shared-services-adoption](code/findings/shared-services-adoption.md) |

### nRF52840 Sense bridge — the second firmware, the BLE(/ANT) track (`firmware-nrf/`)
A XIAO nRF52840 Sense variant: dual-role BLE bridge with correction, no WiFi (so its data surface is a
custom GATT service + a Web Bluetooth app). Reuses the pure ESP32 core via `lib_extra_dirs`. Full story:
[nrf52-sense](code/findings/nrf52-sense.md); the GATT contract: [`firmware-nrf/GATT.md`](firmware-nrf/GATT.md).
| Capability | Status | Lives in |
|---|---|---|
| BLE↔BLE bridge + correction · curve · single-sided ×2 · zero-forward · RGB status LED | ✅ | `firmware-nrf/src/main.cpp`, `lib/bridge/` · PRs #215/#221 |
| On-device calibration · BLE OTA (buttonless DFU) · scan-based source picker | ✅ | `CalibrationSession.h` (shared), `BLEDfu`, `SourceCandidate.h` (shared) · PRs #222/#223 |
| FTMS erg + structured workouts + **shifter bias** (3rd central drives a trainer) | ✅ bench (serial `WKTEST`); live erg gated | `main.cpp` erg loop, `WorkoutRuntime.h` (shared), Workout char `0x0008` · PR #224 |
| BLE-controlled IMU track recording (LSM6DS3, bounded linear buffer, CRC download) | ✅ live-sampling proven | `lib/bridge/ImuCapture.h`, RecCtl/RecData chars |
| Pure wire-format core host-tested in CI (Proto.h + ImuCapture.h golden vectors) | ✅ | `firmware-nrf/test/test_bridge/` (`pio test -e native`) · PR #225 |
| Garmin **Connect IQ** app — in-ride controller (record · erg start/pause · shifter ±) | ✅ builds edge540+epix2; on-device pending | [`firmware-nrf/ciq/`](firmware-nrf/ciq/README.md) · PR #227 |
| ANT+BLE mix (receive ANT / send BLE, &c.) | ⏳ needs licensed S340 SoftDevice | seam ready; drop-zone armed · [nrf52-sense](code/findings/nrf52-sense.md) |

### Shared web UI — one SPA + one palette across all frontends (`web/`, `design/tokens.json`)
Convergence work so the ESP32 web UI, the nRF Web Bluetooth app, and the LVGL device UI don't drift.
| Capability | Status | Lives in |
|---|---|---|
| One self-contained SPA behind a `Transport` seam — `BleTransport` (Web Bluetooth) + `HttpTransport` (ESP32 JSON) auto-selected by host | ✅ both live | [`web/`](web/README.md) (`index.html`, [`HTTP-API.md`](web/HTTP-API.md)) · PRs #229/#230 |
| Single-source design tokens → generated into the web CSS, ESP32 `WebUi.h`, LVGL RGB565; CI-guarded | ✅ one edit re-themes all | `design/tokens.json`,`design/gen_tokens.py`, `code/tests/test_tokens_sync.py` · PR #229 |
| ESP32 embeds + serves the same SPA at `GET /app` (the two web UIs are one file) | ✅ hardware-verified (C3 .165) | `web/gen_spa_header.py`→`WebSpa.h`, `WifiLink` `/app`, `code/tests/test_spa_sync.py` · PR #234 |
| Portable calibration profiles — export/import a curve across the nRF, ESP32, and desk tooling | ✅ | ESP32 `/curve` (`WebJson.h`), nRF Curve GATT char, SPA profile card · PR #233 |
| Same on-device calibration approach both builds (shared `CalibrationSession`→`CorrectionCurve`) | ✅ | `firmware/lib/proxy/CalibrationFit.h` (shared) · [nrf52-sense](code/findings/nrf52-sense.md) |

---

## B. Where every doc lives (the doc-area map)

- **[`code/findings/`](code/findings/README.md)** — **the source of truth** (28 docs): captures, protocol
  specs, decisions, plans. **Its own index [`code/findings/README.md`](code/findings/README.md) is the deep
  map** (grouped by subsystem, CI-guarded). Start there for any protocol/measurement question. Key entries:
  [decisions.md](code/findings/decisions.md) (append-only log), [pre-beta-plan.md](code/findings/pre-beta-plan.md)
  (north star), [forward-plan.md](code/findings/forward-plan.md) (technical backlog), [domain-primer.md](code/findings/domain-primer.md) (concepts).
- **[`sessions/`](sessions/README.md)** — physical (bike/flash/pair) sessions: the [ledger](sessions/README.md)
  (state of play), the [`PLAYBOOK.md`](sessions/PLAYBOOK.md) (how to run one), and one doc per session
  (`session-04`…`session-09` — plan *and* actuals).
- **[`beta/`](beta/ONBOARDING.md)** — tester-facing + program ops: [ONBOARDING](beta/ONBOARDING.md),
  [TESTER-RIDE-PROTOCOL](beta/TESTER-RIDE-PROTOCOL.md), [RIDE-FEEDBACK-FORM](beta/RIDE-FEEDBACK-FORM.md),
  [RELEASE-AND-OTA](beta/RELEASE-AND-OTA.md), [recruiting-and-selection](beta/recruiting-and-selection.md),
  [pitch-posts](beta/pitch-posts.md), [comms-templates](beta/comms-templates.md). (The *operating* doc is
  [code/findings/beta-program.md](code/findings/beta-program.md).)
- **[`tools/`](tools/README.md)** — dev-environment + capture-rig provisioning, the `doctor.ps1` gate, and
  the Infisical secrets scripts.
- **Root — living operational docs:** [`CLAUDE.md`](CLAUDE.md) (the project instructions),
  [`DEV-PLAYBOOK.md`](DEV-PLAYBOOK.md) (desk dev loop), [`USERS-PLAYBOOK.md`](USERS-PLAYBOOK.md) (working with
  testers), [`BIKE-SESSION-READY.md`](BIKE-SESSION-READY.md) (bike-machine cold-start),
  [`HANDOFF-NEXT-SESSION.md`](HANDOFF-NEXT-SESSION.md) (**resume on the hardware machine** — build + the
  hardware verifications now unblocked; points at the architecture-remediation tracker), [`README.md`](README.md),
  [`CHANGELOG.md`](CHANGELOG.md).
- **Root — pre-pivot brief (background; superseded by `findings/`):** the numbered `01-…`–`12-…` docs +
  `START-HERE.md`, `HANDOFF.md`, `CLAUDE-CODE-PROMPT.md`. Useful history; `findings/` wins on any conflict.
- **Root — legacy session/ride cards (historical, kept for append-only links):** `BIKE-SESSION-2.md`,
  `BIKE-SESSION-3.md`, `NEXT-BIKE-SESSION.md`, `RIDE-CARD.md`, `CALIBRATION-RIDE-CARD.md`.

---

## C. Tooling & entry points

- **Capture:** `01_capture_stages` · `02_capture_assioma` · `06_capture_ble` · `07_capture_multi` ·
  `capture_ble_multi` · `capture_ftms` · `sniff_ble` (nRF).
- **Analyse / fit:** `00_validate_capture` · `04_summarize_capture` · `05_diff_captures` · `08_analyze_grid` ·
  `09_fit_calibration` · `12_compare_fit` · `13_build_sqlite` · `14_build_pcap_fit`.
- **Proxy / replay / twins:** `03_static_replay` · `04_run_proxy` · `10_bike_twin` · `16_scan_ant`.
- **Ride / erg / MCP:** `ride_web` · `ride_control` · `ride_wizard` · `ftms_workout` · `ftms_hw_loop` ·
  `mcp_workout_server`.
- **Beta / QA / OTA:** `parse_diag` · `qa_board` · `route_smoke` · `ota_sign` · `build_factory_bin` · `flash_c3`.
- **Bench fakes:** `fake_meter` · `fake_crank` · `crank_reader` · `perf_soak`.
- **Firmware envs** (`firmware/platformio.ini`): `native` (host tests), `esp32c3-supermini` (mock link guard),
  `esp32c3-wifi` (net/OTA), `esp32c3-oled-live` (the **shippable** build). Flash via `firmware/flash.ps1` /
  `scripts/flash_c3.py`. Toolchain gate: `tools/doctor.ps1`.

## D. Source tree at a glance

- **`code/src/sb20proxy/`** — `ant/` `ble/` (CPS+FTMS codecs) · `sources/` `targets/` `twins/` (proxy I/O) ·
  `ride/` (director) · `workout/` `mcp/` (workout server) · `qa/` (acceptance) · `analysis/` (diag, SQLite) ·
  `ota/` (signer) · `calibration.py`/`transform.py` (the correction model).
- **`firmware/lib/proxy/`** — the pure, host-tested core (CPS, ProxyCore, Correction, Config, Calibration*,
  Ota*, Status/DiagReport, Provisioning, SetupPin, HttpSecurity, Perf*, Shifter, Ftms). **`firmware/src/`** —
  the hardware seams: `ble/` (central + crank peripheral), `net/` (WifiLink + portal + HTTP), `disp/` (OLED).

---

*Maintenance: when you add a doc under `beta/` or `sessions/`, or a major capability, add it here in the
same change — CI (`code/tests/test_project_map.py`) enforces the `beta/`+`sessions/` coverage and link
validity. `findings/` keeps its own CI-guarded index; this map points to it rather than duplicating it.*
