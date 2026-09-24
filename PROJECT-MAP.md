# PROJECT-MAP — read this before planning or building anything

**This is the inventory of what already exists — shipped *capabilities* and the lifecycle of every
doc — so a planning loop extends what's there instead of rebuilding it.** It's the one map that spans
the whole repo; the per-area indexes ([`code/findings/README.md`](code/findings/README.md),
[`sessions/README.md`](sessions/README.md), [`docs/reviews/README.md`](docs/reviews/README.md)) are the
deep dives it points into.

> **This map is what *exists*. What to build *next* is [`ROADMAP.md`](ROADMAP.md)** (Now / Next / Later /
> Parked, owner-prioritised; only Now items are mirrored as GitHub issues).

> **Why this exists (a repeated, costly failure):** planning loops kept reading only the `findings/`
> index and missing whole areas — twice nearly **rebuilding things already built** (the meter-to-meter
> corrector; the beta collaboration loop, which was ~95% done). The fix: a map with **summaries of what
> works**, not just file titles. **Before you build, find the capability below; if it's `✅`, you're
> extending, not creating.** CI (`code/tests/test_project_map.py`) fails if a `beta/`, `docs/` or
> `design/` doc is missing from this map, if a doc classed *living* in §F carries a ⛔ banner, or if a
> link here is dead; `code/tests/test_sessions_ledger.py` fails if a `sessions/` doc is missing from the
> ledger. So this can't drift stale silently.

Status key: ✅ built & working · ⚙ partial / hardening · 🔒 built but blocked on external infra · 🔲 not built (capture- or hardware-gated)

---

## A. Capabilities already built (the anti-rebuild inventory)

### SB20 crank spoof — the product (read a meter → re-present as the Stages crank)
| Capability | Status | Lives in |
|---|---|---|
| Dual-role BLE proxy: subscribe to a meter's CPS `0x2A63` → rebroadcast as a Stages crank | ✅ | `firmware/lib/proxy/` (`ProxyCore.h`,`Cps.h`), `firmware/src/ble/` · [phase-0-report](code/findings/phase-0-report.md), [session-G-ble-capture-spec](code/findings/session-G-ble-capture-spec.md) |
| Byte-faithful Stages `0x2F` framing + feature set + DIS identity; the captured Stages bytes live in one spoof profile, not the general codec | ✅ | `Cps.h`, `spoofs/StagesSpm2.h`, `BleCrankPeripheral` · R5a |
| Control-point responder: pair + **calibrate/zero-reset handshake** (company-id 442) | ✅ | `Cps.h` · sessions [8](sessions/session-08-sb20-spoof-calibration.md)/[9](sessions/session-09-zero-reset-onair-confirm.md), [decisions](code/findings/decisions.md) |
| Zero-reset **forwards** to the real Assioma (real offset comp) | ✅ | `BleMeterClient::requestZeroOffset` · forward-plan §10 |
| Control-point results applied in the order the SB20 demands (reply **before** the source zero; an unanswered CP write drops the link, reason 531); the BLE-task→`loop()` handoff is one tested type | ✅ | `CpApply.h`, `LoopDrain.h`, `firmware/test/test_loopdrain/` · R10 |
| L/R balance forwarding (plumbed; the *value* grounding from a capture is pending) | ⚙ | `Cps.h`/`ProxyCore.h` · forward-plan §"balance" |
| Bidirectional **crank-length** bridge (app↔meter) | 🔲 | capture-gated — [forward-plan §11](code/findings/forward-plan.md) + its capture recipe |
| Sole-source pairing rule (SB20 needs *both* crank IDs findable) | ✅ understood | forward-plan §12 |
| **Spoof identity per board.** Every board still *defaults* to `Stages 62144` (bike 1's real crank); a unique-by-default identity and a fleet table are ROADMAP Now item IDENT (#330) | ⚙ hazard | `Config.h`, `RuntimeConfig.h` · decisions 2026-09-23 |

### Config & UX — user-configurable, no rebuild (pre-beta Phase 1, ✅)
| Capability | Status | Lives in |
|---|---|---|
| `/setup` web UI: BLE-scan → pick source → choose crank identity → **NVS** | ✅ | `firmware/src/net/`, `Provisioning.h`, `ConfigPage.h` · [pre-beta-plan](code/findings/pre-beta-plan.md) |
| Source pinning by address; single-sided **×2** toggle | ✅ | `MeterMatch.h`, `BleMeterClient` |
| The NVS config line is **versioned** (`v2|…`, legacy lines parse as v1) and delimiter-safe at the serialiser | ✅ | `RuntimeConfig.h` · R3a (PR #301) |
| Dashboard + instruments: `/` `/ui` `/log` `/stats` `/status` `/diag` | ✅ | `WifiLink`, `WebApp.h`, `DiagReport.h`, `LogBuffer.h` |
| **Ride-mode WiFi-off** + coex hardening (PerfMonitor, watchdog, OLED off hot loop); the C3 met its 30-minute ride-ready definition of done on 2026-09-23 (zero reboots, heap flat, dual-role BLE costs nothing) | ✅ | `PerfMonitor.h`,`PerfStats.h`, `scripts/perf_soak.py` · [perf-coex-plan](code/findings/perf-coex-plan.md), [perf-results](code/findings/perf-results.md) |
| SoftAP per-device PIN (screen) / default passphrase (screenless) | ✅ | `SetupPin.h` |
| **Hard-reset WiFi onboarding, all head-units** — captive portal (WPA2 AP + PIN), QR join screen on the LCD boards (LVGL `LV_USE_QRCODE`), per-board mDNS hostnames (`sb20proxy` / `-cyd` / `-s3` / `-guition`) | ✅ owner-proven via QR | `WifiLink` portal + `ProvisionView`/LVGL · decisions.md 2026-07-04, 2026-09-23 |
| **LCD device picker** — CPS/FTMS-only filtered list (`lcdPickerList`, host-tested), crank/meter/trainer labels, Rescan scan-window boost | ✅ | `SourceCandidate.h` · PR #214 |

### Head-unit boards — one LVGL UI on four ESP32 boards (`firmware/src/disp/`, `src/ui/`)
| Capability | Status | Lives in |
|---|---|---|
| **C3 + OLED** (0.42" SSD1306; 0.96" SH1106 variant): the ride board `sb20proxy.local` | ✅ rides | `disp/OledDisplay`, `OledScreen.h` · [BOARDS.md](BOARDS.md) |
| **CYD** (ESP32-2432S028R, 2.8" ILI9341 + XPT2046, no PSRAM, banded render, lazy screen construction) | ✅ ported + twin-tested; never ridden | `disp/CydDisplay`, `esp32cyd*` envs · [cyd-board](code/findings/cyd-board.md) |
| **Waveshare ESP32-S3-Touch-LCD-1.47** (pioarduino, 8 MB flash) | ✅ LVGL touch UI verified 07-05; OTA-deaf to espota | `disp/LcdDisplay`, `esp32s3-pio*` envs · [advanced-board-s3-touch](code/findings/advanced-board-s3-touch.md) |
| **Guition JC3248W535** (ESP32-S3-N16R8, 3.5" 320×480 AXS15231B QSPI + capacitive touch) — the two-bike stack's head unit | ✅ validated on a simulated meter 2026-09-23; never ridden; not compiled in CI (#323) | `disp/GuitionDisplay`, `lib/esp_lcd_axs15231b/`, `esp32-guition*` envs · [guition-board](code/findings/guition-board.md) |
| The LVGL UI is **host-tested**: `native-lvgl` renders the real `src/ui/LvglUi.cpp` headless and asserts pixels + tap→`UiAction` | ✅ CI | `firmware/test/test_lvglui/`, `firmware/test/lvgl_shim/` · U5 (PR #267) |
| Device state → view model projected **once for both panel families**; the calibrate wizard's projection shared by the LCD frame and `GET /calibrate` | ✅ | `UiModel.h` (`projectRideView`), `CalibrationPage.h` (`projectCalWizard`), `firmware/test/test_uiproject/` · R1e.2 |
| Touch-calibration ritual testable without a screen | ✅ | `TouchCalRitual.h`, `firmware/test/test_touchcal/` · R1e.1 |
| **Bench camera**: a USB camera + ffmpeg see what a panel shows, so a session can build → flash → capture → judge with no eyes on it | ✅ | recipe in [BOARDS.md](BOARDS.md) (2026-09-18) |
| Watty Birds, a power-flown Flappy Bird easter egg | ✅ | `WattyBird.h`, `WattyBirdRender.h` · PR #273 |

### Meter-to-meter corrector — the second product mode (✅ M1–M5)
| Capability | Status | Lives in |
|---|---|---|
| On-device calibration wizard: read 2 meters → fit curve → corrected rebroadcast under our OWN identity | ✅ | `CalibrationFit.h`,`CalibrationSession.h`,`CalibrationPage.h` · [meter-to-meter-proxy](code/findings/meter-to-meter-proxy.md) |
| Python fit pipeline (scale/offset + grid) + apply | ✅ | `calibration.py`,`transform.py`, `scripts/09_fit_calibration.py`,`08_analyze_grid.py` |
| **MeterCompare**: live A/B meter statistics, the torque-axed Compare screen, a Python twin with a parity test | ✅ (real two-meter data still pending) | `MeterCompare.h`, `CompareService.h`, `sb20proxy/compare.py`, `scripts/compare_meters.py` · [meter-compare-visualization](code/findings/meter-compare-visualization.md) |
| Corrector mode still answers head units with the Stages-shaped crank-length reply | ⚙ | `Cps.h` · R5b, issue #305 |

### Beta collaboration loop & packaging (✅ — capture→support→OTA; used first by round-zero beta)
| Capability | Status | Lives in |
|---|---|---|
| Tester `/diag` report (config + status + **raw CPS frames**) → `parse_diag.py` → golden-vector stub | ✅ | `DiagReport.h`, `scripts/parse_diag.py`, `analysis/diag.py` · [beta-program](code/findings/beta-program.md), [supported-meters](code/findings/supported-meters.md) |
| Tester **review-&-send** report page `/report` (review on-device → Download/Copy/Email; consent-first) | ✅ | `WebApp.h` `diagReportPageHtml`, `WifiLink` |
| Pre-ship QA acceptance card | ✅ | `scripts/qa_board.py`, `sb20proxy/qa/acceptance.py` |
| Tester kit: onboarding, ride protocol, **ride-feedback form**, recruiting, pitch posts, comms | ✅ (programme parked; kit exercised by ROADMAP Now item ROUND0) | [`beta/`](beta/ONBOARDING.md) |
| Release & fleet-OTA runbook + firmware **version stamping** (git SHA + build time in every build) | ✅ | [`beta/RELEASE-AND-OTA.md`](beta/RELEASE-AND-OTA.md), `firmware/scripts/build_version.py`, `Config.h` `SB20_FIRMWARE_VERSION` → `/status`,`/diag` |

### FTMS / erg / training — the training stack (the Now lane since 2026-09-23)
| Capability | Status | Lives in |
|---|---|---|
| FTMS codec (Indoor Bike Data + Control Point) + `ErgController` + Ride-erg bridge | ✅ | `ble/ftms.py`,`ble/ftms_erg.py`, `Ftms.h` · [ftms-protocol](code/findings/ftms-protocol.md) |
| Ride Director: dynamic plan engine, agent control API, %FTP/Coggan zones, phone UI | ✅ | `sb20proxy/ride/` · [ride-director](code/findings/ride-director.md) |
| MCP workout server (compose + drive an erg workout as agent tools) + interval driver | ✅ | `sb20proxy/workout/`,`sb20proxy/mcp/`, `scripts/{mcp_workout_server,ftms_workout}.py` · [mcp-workout-server](code/findings/mcp-workout-server.md) |
| **On-device workout engine**: structured workouts (presets + ZWO/FIT import over `/workout`) persisted in NVS and executed deterministically as the FTMS erg controller | ✅ phases 1–4 (PRs #186–#189, #212) | `WorkoutEngine.h`, `WorkoutRuntime.h`, `WorkoutPresets.h`, `WorkoutStore`, `scripts/import_workout.py` · [on-device-workout-engine](code/findings/on-device-workout-engine.md) |
| **On-device FTMS erg drive** — the head-unit workout engine erg-drives a trainer directly (no phone/PC): `trainerNameFilter` in RuntimeConfig, Setup-screen pick, serial `TRAINER <name>`, optimistic CP progression | ✅ twin-proven (CYD↔C3 sim); **never driven a real SB20** (session 14 G2) | `FtmsErgClient` + `BleMeterClient` scan hub + `main.cpp` · decisions.md 2026-07-05 |
| Shifter-buttons-adjust-erg mapper | ✅ | `Shifter.h` · [shifter-erg-control](code/findings/shifter-erg-control.md) |
| FTMS trainer sim on a spare board (the erg bench oracle: logs controlled/started/target) | ✅ | `esp32c3-ftms-server` env · `FtmsTrainerServer` |

### OpenBikeControl (OBC) button proxy — the SB20's handlebar buttons re-presented to any OBC consumer
| Capability | Status | Lives in |
|---|---|---|
| OBC codec + SB20 button map + shifter→action composer (pure, host-tested) | ✅ | `Obc.h`, `Sb20ButtonMap.h`, `ObcShifterSource.h` · [obc-protocol](code/findings/obc-protocol.md) |
| Transports: BLE peripheral (ESP32 + nRF) and mDNS/TCP-UDP (ESP32); devmode name `OBC-SB20`; the shifter-sink central to the SB20; `/obc/buttons.json` + the shared-SPA button config | ✅ merged 07-10 (#251); G1 proven on air in session 13 | `net/ObcNet`, `BleShifterClient`, `BleCrankPeripheral` · [session 13](sessions/session-13-qz-obc-consumer-and-sb20-buttons.md) |
| qz as an OBC consumer (the fork's `obclistener`, binding by name) | ✅ on air; upstream PR not yet opened (#324) | [qz-upstream-contribution](code/findings/qz-upstream-contribution.md) |
| Real paddle → C3 shifter-sink → OBC → qz | 🔒 the discovery-ordering deadlock (#291) | [session 11](sessions/session-11-obc-bike-test.md) (blocked) |
| Third-party shifters (Di2 D-Fly, SRAM AXS) over ANT → OBC: the Controls-page decoder exists, no capture yet | ⚙ decoder only | `AntControlsSource.h` · [obc-shifter-sources](code/findings/obc-shifter-sources.md), issue #249 |

### OTA, flashing & security (✅ lockdown · 🔒 signed-pull blocked on backend)
| Capability | Status | Lives in |
|---|---|---|
| Security lockdown: CSRF guard enforced on the *method* in a pure route layer; authenticated ArduinoOTA; no open `/update` | ✅ | `HttpSecurity.h`, `WebRoutes.h`, `WifiLink` · [ota-update-plan](code/findings/ota-update-plan.md), R9 |
| Authenticated **push-OTA** (the channel that works today; the C3 takes it first-attempt at −72 dBm) with an RSSI pre-flight | ✅ | `firmware/flash.ps1` + `firmware/ota_secret.h` · decisions 2026-09-23 |
| **Flash guards**: a ride-unsafe env (mock meter, bench match, ⛔ superseded) is refused without `-Force`; the nRF flasher refuses a SoftDevice-layout mismatch; a USB flash of an S3 board no longer wipes NVS | ✅ | `tools/PioIni.ps1` (+ `tools/tests/`), `firmware-nrf/flash.ps1`, `scripts/flash_s3.py` · PRs #302, #313, #322 |
| **Route-behaviour oracle**: capture all 57 HTTP behaviours from a running board and diff two captures | ✅ hardware-proven | `scripts/route_baseline.py` · R9b |
| Signed-**pull** OTA core (manifest + ed25519/BLAKE2b verify + updater) + signer | 🔒 | `OtaManifest.h`,`OtaVerify.h`,`OtaUpdater.h`, `scripts/ota_sign.py` — needs the backend + keygen |

### ANT+ path (✅ — the original Pi proxy, superseded by BLE for the product)
| Capability | Status | Lives in |
|---|---|---|
| ANT+ master spoof + static replay + digital twins (loopback + BikeTwin) | ✅ | `sb20proxy/{ant,sources,targets,twins}/`, `scripts/{03_static_replay,04_run_proxy,10_bike_twin}.py` · [forward-plan §2](code/findings/forward-plan.md) |

### Capture / analysis / infra
| Capability | Status | Lives in |
|---|---|---|
| nRF BLE sniffer (app↔SB20 passive) | ✅ | `scripts/sniff_ble.py` · [nrf-sniffer](code/findings/nrf-sniffer.md) |
| ANT+ + BLE capture tooling; the one-clock ride monitor | ✅ | `scripts/0[1267]_capture_*.py`,`capture_*.py`, `15_monitor_ride.py` · [wsl-capture-runbook](code/findings/wsl-capture-runbook.md), [traffic-observability](code/findings/traffic-observability.md) |
| Capture records are **self-describing about damage** — a field the flags advertised but the frame could not carry is named (`truncated_at_field`), never silently omitted | ✅ | `scripts/06_capture_ble.py`, `ant/pages.py`, `ant/fec.py` · `tests/test_capture_ble_decoders.py` · issue #306 |
| Rebuildable SQLite index over captures + annotations | ✅ | `analysis/pcap_sqlite.py`,`jsonl_sqlite.py`,`annotations.py`, `scripts/13_build_sqlite.py` · [sqlite-analysis-layer](code/findings/sqlite-analysis-layer.md) |
| Every committed capture is indexed, and every session doc has a ledger row — both CI-guarded | ✅ | [`captures/README.md`](code/findings/captures/README.md), `code/tests/test_captures_index.py`, `code/tests/test_sessions_ledger.py` |
| Dev toolchain + capture-rig gate; Infisical secrets | ✅ | [`tools/`](tools/README.md) (`provision-dev-env.ps1`,`doctor.ps1`,`secrets-*.ps1`) · [shared-services-adoption](code/findings/shared-services-adoption.md) |
| Doc guards: every relative Markdown link resolves; the findings index, this map and the ledger are complete | ✅ | `code/tests/test_doc_links.py`, `test_findings_index.py`, `test_project_map.py`, `test_sessions_ledger.py` |

### nRF52840 Sense bridge — the second firmware, the BLE(/ANT) track (`firmware-nrf/`)
A XIAO nRF52840 Sense variant: dual-role BLE bridge with correction, no WiFi (so its data surface is a
custom GATT service + a Web Bluetooth app). Reuses the pure ESP32 core via `lib_extra_dirs`. Full story:
[nrf52-sense](code/findings/nrf52-sense.md); the GATT contract: [`firmware-nrf/GATT.md`](firmware-nrf/GATT.md);
the roadmap: [nrf-roadmap](code/findings/nrf-roadmap.md).
| Capability | Status | Lives in |
|---|---|---|
| BLE↔BLE bridge + correction · curve · single-sided ×2 · zero-forward · RGB status LED | ✅ | `firmware-nrf/src/main.cpp`, `lib/bridge/` · PRs #215/#221 |
| The peer-role ladder and the read→correct→re-frame relay are pure, host-tested seams | ✅ | `lib/bridge/PeerRole.h`, `SourceRelay.h`, `test/test_peerrole/`, `test/test_sourcerelay/` · R1d.1/R1d.2 |
| On-device calibration · BLE OTA (buttonless DFU) · scan-based source picker | ✅ | `CalibrationSession.h` (shared), `BLEDfu`, `SourceCandidate.h` (shared) · PRs #222/#223 |
| **SB20 crank-spoof mode** on the nRF (Stages 0x2F framing + identity + the 442 calibrate reply) | ✅ bench (identity verified over BLE); never met a real SB20 (R3) | `main.cpp`, `spoofs/StagesSpm2.h` (shared) · PR #251 |
| FTMS erg + structured workouts + **shifter bias** (3rd central drives a trainer) | ✅ bench (serial `WKTEST`); live erg gated | `main.cpp` erg loop, `WorkoutRuntime.h` (shared), Workout char `0x0008` · PR #224 |
| BLE-controlled IMU track recording (LSM6DS3, bounded linear buffer, CRC download) | ✅ live-sampling proven | `lib/bridge/ImuCapture.h`, RecCtl/RecData chars |
| Pure wire-format core host-tested in CI (Proto.h + ImuCapture.h golden vectors) | ✅ | `firmware-nrf/test/test_bridge/` (`pio test -e native`) · PR #225 |
| **ANT+ Bike Power master** on the licensed S340 SoftDevice: transmitting, received by a Garmin stick, the full BLE-source → nRF → ANT+ loop bench-proven | ✅ (S340 + key provisioned 07-13; on air 07-14/15) | `src/ant/AntMasterChannel.h`, `lib/bridge/AntBikePower.h`, `AntMasterScheduler.h`, `xiao-sense-s340` env · [nrf-s340-ant-bringup](sessions/nrf-s340-ant-bringup.md) |
| Garmin **Connect IQ** app — in-ride controller (record · erg start/pause · shifter ±) | ✅ builds edge540+epix2; on-device pending; its Bridge mirror has no golden lock (R2c) | [`firmware-nrf/ciq/`](firmware-nrf/ciq/README.md) · PR #227 |
| ANT+ slave channel (receive ANT / send BLE) | 🔲 | [nrf-roadmap](code/findings/nrf-roadmap.md) P4 remainder |

### Shared web UI — one SPA + one palette + one wire contract across all frontends (`web/`, `ui-schema/`, `design/tokens.json`)
Convergence work so the ESP32 web UI, the nRF Web Bluetooth app, and the LVGL device UI don't drift.
| Capability | Status | Lives in |
|---|---|---|
| One self-contained SPA behind a `Transport` seam — `BleTransport` (Web Bluetooth) + `HttpTransport` (ESP32 JSON) auto-selected by host; iOS/Bluefy quirks and auto-reconnect handled | ✅ both live | [`web/`](web/README.md) (`index.html`, [`HTTP-API.md`](web/HTTP-API.md)) · PRs #229/#230, session 13 |
| Single-source design tokens → generated into the web CSS, ESP32 `WebUi.h`, LVGL RGB565; CI-guarded | ✅ one edit re-themes all | `design/tokens.json`,`design/gen_tokens.py`, `code/tests/test_tokens_sync.py` · PR #229 |
| ESP32 embeds + serves the same SPA at `GET /app` (the two web UIs are one file), streamed from flash | ✅ hardware-verified | `web/gen_spa_header.py`→`WebSpa.h`, `WifiLink` `/app`, `code/tests/test_spa_sync.py` · PR #234, R9c |
| **Bridge wire contract generated from one schema** — `ui-schema/bridge.json` → the JS codec, golden vectors and the C++ golden header; the SPA runs the generated codec; Node + C++ parity in CI | ✅ | `scripts/gen_bridge.py`, `web/bridge-codec.js`, `web/test/bridge-codec.test.mjs` · [ui-schema-design](design/ui-schema-design.md), R2/U2 |
| **Web-JSON contract lint** — `ui-schema/web-json.json` checks the ESP32 serializers and the SPA agree on every field | ✅ | `scripts/gen_webjson.py`, `WebJson.h`, `ui-schema/web-json.md` (generated) · U2 |
| One gate over all four generators, pre-push and in CI | ✅ | `scripts/check_generated.py`, `.githooks/pre-push`, `tools/install-hooks.ps1` · PR #294 |
| Portable calibration profiles — export/import a curve across the nRF, ESP32, and desk tooling | ✅ | ESP32 `/curve` (`WebJson.h`), nRF Curve GATT char, SPA profile card · PR #233 |
| Same on-device calibration approach both builds (shared `CalibrationSession`→`CorrectionCurve`) | ✅ | `firmware/lib/proxy/CalibrationFit.h` (shared) · [nrf52-sense](code/findings/nrf52-sense.md) |
| Design of record for the UI unification (U-series) and the structural remediation (R-series) | ✅ living checklists | [ui-unification](code/findings/ui-unification.md), [architecture-remediation](code/findings/architecture-remediation.md), [ui-architecture-review](design/ui-architecture-review.md) |

---

## B. Where every doc lives (the doc-area map)

- **[`ROADMAP.md`](ROADMAP.md)** — what to do next; the only prioritised backlog.
- **[`code/findings/`](code/findings/README.md)** — **the source of truth**: captures, protocol specs,
  decisions, plans. **Its own index [`code/findings/README.md`](code/findings/README.md) is the deep
  map** (grouped by subsystem, CI-guarded). Start there for any protocol/measurement question. Key entries:
  [decisions.md](code/findings/decisions.md) (append-only log), [forward-plan.md](code/findings/forward-plan.md)
  (technical detail behind backlog items), [domain-primer.md](code/findings/domain-primer.md) (concepts),
  [captures/README.md](code/findings/captures/README.md) (every committed capture, CI-guarded).
- **[`sessions/`](sessions/README.md)** — physical (bike/flash/pair) sessions: the [ledger](sessions/README.md)
  (state of play, CI-guarded), the [`PLAYBOOK.md`](sessions/PLAYBOOK.md) (how to run one), one doc per
  session (`session-04`…`session-14`, plan *and* actuals), the un-numbered records (the nRF S340 bring-up,
  the `CAPTURE-*` runs) and the laptop hand-off.
- **[`docs/`](docs/architecture.md)** — [`architecture.md`](docs/architecture.md) (the conceptual
  architecture with four rendered diagrams; canonical), [`system-reference.md`](docs/system-reference.md)
  (how the pieces fit at runtime: roles, discovery rules, modes, legal configurations, the two-bike
  matrix; issue #290), [`ui-feature-map.md`](docs/ui-feature-map.md) (every feature on every surface,
  its device-vs-web placement and the test that proves it; issue #345), [`bike-session-workflow.md`](docs/bike-session-workflow.md)
  (a short companion to the playbook), [`diagrams/`](docs/diagrams/README.md) (Mermaid sources + SVGs),
  [`agents/`](docs/agents/issue-tracker.md) (the issue-tracker, triage-label and domain-doc conventions the
  engineering skills read), [`reviews/`](docs/reviews/README.md) (dated review snapshots, indexed there), and
  the two idea lists ([dreaming-20-ideas](docs/dreaming-20-ideas.md), [sol-dreaming-17Jul26](docs/sol-dreaming-17Jul26.md);
  historical — ideas graduate only via ROADMAP).
- **[`design/`](design/ui-schema-design.md)** — the UI design of record ([ui-architecture-review](design/ui-architecture-review.md),
  [ui-schema-design](design/ui-schema-design.md)), the design tokens + generator, the locked LCD mockups and
  their renders, the S3 case ([s3-case](design/s3-case/README.md)), and the remote-mode recipes
  ([REMOTE-RENDER-RECIPE](design/REMOTE-RENDER-RECIPE.md), [SHOW-IMAGE-RECIPE](design/SHOW-IMAGE-RECIPE.md)).
- **[`beta/`](beta/ONBOARDING.md)** — tester-facing + program ops: [ONBOARDING](beta/ONBOARDING.md),
  [TESTER-RIDE-PROTOCOL](beta/TESTER-RIDE-PROTOCOL.md), [RIDE-FEEDBACK-FORM](beta/RIDE-FEEDBACK-FORM.md),
  [RELEASE-AND-OTA](beta/RELEASE-AND-OTA.md), [recruiting-and-selection](beta/recruiting-and-selection.md),
  [pitch-posts](beta/pitch-posts.md), [comms-templates](beta/comms-templates.md). (The *operating* doc is
  [code/findings/beta-program.md](code/findings/beta-program.md); the programme is parked, the kit is in use.)
- **[`tools/`](tools/README.md)** — dev-environment + capture-rig provisioning, the `doctor.ps1` gate, the
  flash ride-guard and its tests, the pre-push hook installer, the Infisical secrets scripts.
- **Firmware-adjacent docs:** [`firmware/README.md`](firmware/README.md), [`firmware/BENCH-FLASH.md`](firmware/BENCH-FLASH.md)
  (the env table + flash card), [`firmware/webflash/README.md`](firmware/webflash/README.md),
  [`firmware-nrf/GATT.md`](firmware-nrf/GATT.md), [`firmware-nrf/ciq/README.md`](firmware-nrf/ciq/README.md),
  [`firmware-nrf/vendor/softdevice/README.md`](firmware-nrf/vendor/softdevice/README.md) (the licensed S340 drop zone).
- **Desk-tooling docs:** [`code/README.md`](code/README.md), the script runbooks
  [BLE-LOOP](code/scripts/BLE-LOOP.md), [PC-CRANK](code/scripts/PC-CRANK.md), [RIDE-WEB](code/scripts/RIDE-WEB.md),
  [`web/README.md`](web/README.md), [`web/HTTP-API.md`](web/HTTP-API.md), [`ui-schema/web-json.md`](ui-schema/web-json.md) (generated).
- **Root — living operational docs:** [`ROADMAP.md`](ROADMAP.md), [`CLAUDE.md`](CLAUDE.md) (the project
  instructions), [`AGENTS.md`](AGENTS.md), [`DEV-PLAYBOOK.md`](DEV-PLAYBOOK.md) (desk dev loop),
  [`USERS-PLAYBOOK.md`](USERS-PLAYBOOK.md) (working with testers), [`BOARDS.md`](BOARDS.md) (the physical
  boards), [`README.md`](README.md).
- **Root — pre-pivot brief (background; superseded by `findings/`):** the numbered `01-…`–`12-…` docs +
  `START-HERE.md`, `HANDOFF.md`, `CLAUDE-CODE-PROMPT.md`. Useful history; `findings/` wins on any conflict.
- **Root — legacy session/ride cards and hand-offs (historical, kept for append-only links):**
  `BIKE-SESSION-2.md`, `BIKE-SESSION-3.md` (session records), `NEXT-BIKE-SESSION.md`, `RIDE-CARD.md`,
  `CALIBRATION-RIDE-CARD.md`, `BIKE-SESSION-READY.md`, `HANDOFF-NEXT-SESSION.md`, `CHANGELOG.md`
  (stopped 2026-06-15; `decisions.md` is the log).

---

## C. Tooling & entry points

- **Capture:** `01_capture_stages` · `02_capture_assioma` · `06_capture_ble` · `07_capture_multi` ·
  `capture_ble_multi` · `capture_ftms` · `sniff_ble` (nRF) · `15_monitor_ride` (the one-clock ride
  supervisor) · `16_scan_ant` · `run_capture.sh` (WSL).
- **Analyse / fit:** `00_validate_capture` · `04_summarize_capture` · `05_diff_captures` · `08_analyze_grid` ·
  `09_fit_calibration` · `12_compare_fit` · `13_build_sqlite` · `14_build_pcap_fit` ·
  `03_ingest_jsonl_to_influx` · `compare_meters`.
- **Proxy / replay / twins:** `03_static_replay` · `04_run_proxy` · `10_bike_twin`.
- **Ride / erg / MCP:** `ride_web` · `ride_control` · `ride_wizard` · `ftms_workout` · `ftms_hw_loop` ·
  `mcp_workout_server` · `import_workout`.
- **Beta / QA / OTA / flash:** `parse_diag` · `qa_board` · `route_smoke` · `route_baseline` · `ota_sign` ·
  `build_factory_bin` · `flash_c3` · `flash_s3`.
- **Bench fakes and probes:** `fake_meter` · `fake_crank` · `crank_reader` · `obc_reader` · `perf_soak` · `bench_s3`.
- **Generators (one source, committed mirrors, one gate):** `gen_bridge` · `gen_webjson` · `check_generated`
  (+ `web/gen_spa_header.py`, `design/gen_tokens.py`).
- **Firmware envs** (`firmware/platformio.ini`, 37 envs): `native` and `native-lvgl` (host tests);
  `esp32c3-supermini` (mock link guard); `esp32c3-wifi` (net/OTA); **`esp32c3-oled-live-ota` — the shipping
  C3 build** (`esp32c3-oled-live` is the same without espota; `oled96*` for the 0.96" boards); `esp32cyd*`,
  `esp32s3-pio*`, `esp32-guition*` (the LCD head units, each with `-live` and `-ota` variants); FTMS bench
  and probe envs. Bench (`METER_MATCH_ANY_CPS=1`) and mock envs are refused by the flash guard. CI compiles
  6 of the 37 (#323). Flash via `firmware/flash.ps1` / `scripts/flash_c3.py` / `scripts/flash_s3.py` /
  `firmware-nrf/flash.ps1`. Toolchain gate: `tools/doctor.ps1`.

## D. Source tree at a glance

- **`code/src/sb20proxy/`** — `ant/` `ble/` (CPS+FTMS codecs) · `sources/` `targets/` `twins/` (proxy I/O) ·
  `ride/` (director) · `workout/` `mcp/` (workout server) · `qa/` (acceptance) · `analysis/` (diag, SQLite,
  annotations) · `ota/` (signer) · `calibration.py`/`transform.py` (the correction model) · `compare.py`
  (MeterCompare twin) · `fitcompare.py` · `llm.py`, `obs.py`, `logparse.py` (only their tests use them).
- **`firmware/lib/proxy/`** — the pure, host-tested core (CPS + `spoofs/`, ProxyCore, Correction, Config,
  RuntimeConfig, MeterMatch, Calibration*, MeterCompare, Ota*, Status/DiagReport, Provisioning, SetupPin,
  HttpSecurity, WebRoutes/WebApp/WebJson/WebSpa/WebUi, Perf*, Shifter, Obc*, Ftms, Workout*, UiModel,
  LcdUi/LcdCanvas, TouchCal*, CpApply, LoopDrain, WattyBird*). **`firmware/src/`** — the hardware seams:
  `ble/` (meter central, crank peripheral, shifter client, FTMS erg client + trainer server), `net/`
  (WifiLink + portal + HTTP + OTA pull + OBC network), `disp/` (OLED and the three LCD panel seams),
  `ui/` (the LVGL UI + generated theme + fonts). **`firmware/lib/`** also vendors `monocypher/` and
  `esp_lcd_axs15231b/`.
- **`firmware-nrf/`** — `lib/bridge/` (Proto, AntBikePower, AntMasterScheduler, ImuCapture, PeerRole,
  SourceRelay) · `src/` (main, BridgeService, BridgeConfigStore, `ant/`) · `ciq/` (Connect IQ) ·
  `vendor/softdevice/` (S340, gitignored).
- **`web/`**, **`ui-schema/`**, **`design/`** — the shared SPA + generated codec, the wire-contract schemas,
  the tokens + mockups + case.

## E. Product lines and their status (as of 2026-09-23)

| Line | Status | Where it stands |
|---|---|---|
| **SB20 crank spoof** on the C3 (the product) | core · rides | proven end to end (pair → power → calibrate/zero); the two-bike stack runs one per bike |
| **On-device erg / workouts on a head unit** | core (since the 2026-09-23 north star) | built and twin-proven; the real-SB20 erg round-trip is ROADMAP Now item ERG (session 14) |
| **Head-unit boards** (CYD, S3-Touch, Guition) | core delivery vehicle | Guition on both bikes; validated on simulated data, never ridden; CI does not compile it (#323) |
| **Round-zero beta** (the owner and their daughter) | core | the tester kit exercised on a fleet of two (ROADMAP Now item ROUND0) |
| **qz/Peloton path** (qz drives erg; we proxy power) | supported | proven in sessions 7 and 13; never two erg controllers on one bike |
| **Meter-to-meter corrector** | supporting | built M1–M5; never ridden (session 5 deferred) |
| **OBC button proxy** | supporting | G1 proven; the shifter-sink → qz path blocked on #291 |
| **nRF bridge + ANT+** | supporting | BLE bridge, spoof mode and ANT+ master bench-proven; never met a real SB20 |
| **MCP / agent surfaces** | supporting | the PC-side workout server exists; a head-unit adapter is #321 |
| **Garmin Connect IQ remote** | parked | builds; on-device pending |
| **External beta programme** | parked | un-parks after round zero |
| **Erg dyno / autotuner; track launch control** | parked (approved, paused) | research + plans only, no code |
| **Python ANT+ Pi proxy** | superseded | history; the replay + twins still serve as the desk oracle |

## F. Doc lifecycle (every doc outside `findings/`, `sessions/` and `docs/reviews/`, which have their own indexes)

Classes: **living** (kept current; must not carry a ⛔ banner) · **index** · **plan** (proposes work; its
status line says how much shipped) · **session** (a record of a physical session) · **historical**
(superseded; do not follow) · **tester** (tester-facing) · **generated** (do not edit by hand).
CI parses this table.

| Doc | Class | Note |
|---|---|---|
| [ROADMAP.md](ROADMAP.md) | living | the one backlog |
| [CLAUDE.md](CLAUDE.md) | living | project instructions and invariants |
| [AGENTS.md](AGENTS.md) | living | mirror of the diagrams convention for other tools |
| [DEV-PLAYBOOK.md](DEV-PLAYBOOK.md) | living | the desk dev loop |
| [USERS-PLAYBOOK.md](USERS-PLAYBOOK.md) | living | working with testers |
| [BOARDS.md](BOARDS.md) | living | the physical boards, MACs, hostnames |
| [README.md](README.md) | index | the front door |
| [PROJECT-MAP.md](PROJECT-MAP.md) | index | this map |
| [docs/architecture.md](docs/architecture.md) | living | the conceptual architecture (canonical) |
| [docs/system-reference.md](docs/system-reference.md) | living | how the pieces fit at runtime (issue #290) |
| [docs/ui-feature-map.md](docs/ui-feature-map.md) | living | every UI feature, its placement, its test (issue #345) |
| [docs/bike-session-workflow.md](docs/bike-session-workflow.md) | living | short companion to the playbook |
| [docs/diagrams/README.md](docs/diagrams/README.md) | index | diagram convention |
| [docs/reviews/README.md](docs/reviews/README.md) | index | dated reviews, indexed there |
| [docs/agents/issue-tracker.md](docs/agents/issue-tracker.md) | living | how skills use GitHub issues |
| [docs/agents/triage-labels.md](docs/agents/triage-labels.md) | living | the five triage labels |
| [docs/agents/domain.md](docs/agents/domain.md) | living | domain-doc conventions |
| [docs/dreaming-20-ideas.md](docs/dreaming-20-ideas.md) | historical | idea list; ideas graduate via ROADMAP |
| [docs/sol-dreaming-17Jul26.md](docs/sol-dreaming-17Jul26.md) | historical | idea list; two ideas graduated and are parked |
| [design/ui-architecture-review.md](design/ui-architecture-review.md) | plan | UI design of record; much has shipped |
| [design/ui-schema-design.md](design/ui-schema-design.md) | plan | the wire-contract codegen design (shipped) |
| [design/REMOTE-RENDER-RECIPE.md](design/REMOTE-RENDER-RECIPE.md) | living | show visuals to a remote owner |
| [design/SHOW-IMAGE-RECIPE.md](design/SHOW-IMAGE-RECIPE.md) | living | the general image recipe |
| [design/s3-case/README.md](design/s3-case/README.md) | living | the 3D-printed S3 case |
| [design/s3-case/board-reference/README.md](design/s3-case/board-reference/README.md) | living | the STEP geometry reference |
| [code/README.md](code/README.md) | living | the Python package readme (partly ANT+-era) |
| [code/findings/README.md](code/findings/README.md) | index | the findings index (CI-guarded) |
| [code/findings/captures/README.md](code/findings/captures/README.md) | index | every capture (CI-guarded) |
| [code/scripts/BLE-LOOP.md](code/scripts/BLE-LOOP.md) | living | ESP↔Python BLE dev loop |
| [code/scripts/PC-CRANK.md](code/scripts/PC-CRANK.md) | living | the PC fake-crank rig |
| [code/scripts/RIDE-WEB.md](code/scripts/RIDE-WEB.md) | living | the ride dashboard |
| [sessions/README.md](sessions/README.md) | index | the session ledger (CI-guarded) |
| [sessions/PLAYBOOK.md](sessions/PLAYBOOK.md) | living | how to run a physical session |
| [beta/ONBOARDING.md](beta/ONBOARDING.md) | tester | onboarding one-pager |
| [beta/TESTER-RIDE-PROTOCOL.md](beta/TESTER-RIDE-PROTOCOL.md) | tester | the ride script |
| [beta/RIDE-FEEDBACK-FORM.md](beta/RIDE-FEEDBACK-FORM.md) | tester | per-ride form |
| [beta/RELEASE-AND-OTA.md](beta/RELEASE-AND-OTA.md) | living | release + fleet OTA runbook |
| [beta/recruiting-and-selection.md](beta/recruiting-and-selection.md) | tester | programme parked |
| [beta/pitch-posts.md](beta/pitch-posts.md) | tester | programme parked |
| [beta/comms-templates.md](beta/comms-templates.md) | tester | programme parked |
| [tools/README.md](tools/README.md) | living | toolchain, guards, secrets |
| [firmware/README.md](firmware/README.md) | living | ESP32 firmware readme |
| [firmware/BENCH-FLASH.md](firmware/BENCH-FLASH.md) | living | env table + flash card |
| [firmware/webflash/README.md](firmware/webflash/README.md) | living | browser flashing |
| [firmware-nrf/GATT.md](firmware-nrf/GATT.md) | living | the Bridge GATT contract |
| [firmware-nrf/ciq/README.md](firmware-nrf/ciq/README.md) | living | the Connect IQ app |
| [firmware-nrf/vendor/softdevice/README.md](firmware-nrf/vendor/softdevice/README.md) | living | the S340 drop zone |
| [web/README.md](web/README.md) | living | the shared SPA |
| [web/HTTP-API.md](web/HTTP-API.md) | living | the ESP32 JSON API |
| [ui-schema/web-json.md](ui-schema/web-json.md) | generated | do not edit by hand |
| [BIKE-SESSION-2.md](BIKE-SESSION-2.md) | session | session 2 record |
| [BIKE-SESSION-3.md](BIKE-SESSION-3.md) | session | session 3 record |
| [BIKE-SESSION-READY.md](BIKE-SESSION-READY.md) | historical | cold-start card; live content moved to session 14 |
| [HANDOFF-NEXT-SESSION.md](HANDOFF-NEXT-SESSION.md) | historical | 2026-07-10 hand-off |
| [NEXT-BIKE-SESSION.md](NEXT-BIKE-SESSION.md) | historical | ANT+ Phase-1B run sheet |
| [RIDE-CARD.md](RIDE-CARD.md) | historical | Phase-0 ride card |
| [CALIBRATION-RIDE-CARD.md](CALIBRATION-RIDE-CARD.md) | historical | session-2 ANT+ card |
| [CHANGELOG.md](CHANGELOG.md) | historical | stopped 2026-06-15; decisions.md is the log |
| [START-HERE.md](START-HERE.md) | historical | the owner's Phase-0 cookbook |
| [HANDOFF.md](HANDOFF.md) | historical | the pre-pivot hand-off |
| [CLAUDE-CODE-PROMPT.md](CLAUDE-CODE-PROMPT.md) | historical | the original opening prompt |
| [01-project-brief.md](01-project-brief.md) | historical | pre-pivot brief |
| [02-technical-context.md](02-technical-context.md) | historical | pre-pivot brief |
| [03-central-hypothesis-and-phase-zero.md](03-central-hypothesis-and-phase-zero.md) | historical | pre-pivot brief |
| [04-architecture.md](04-architecture.md) | historical | pre-pivot brief |
| [05-implementation-phases.md](05-implementation-phases.md) | historical | pre-pivot brief |
| [06-prior-art-and-references.md](06-prior-art-and-references.md) | historical | pre-pivot brief |
| [07-hardware-and-environment.md](07-hardware-and-environment.md) | historical | pre-pivot brief |
| [08-risks-and-gotchas.md](08-risks-and-gotchas.md) | historical | pre-pivot brief |
| [09-exploring-captures.md](09-exploring-captures.md) | historical | pre-pivot brief |
| [10-relationship-to-QZ.md](10-relationship-to-QZ.md) | historical | pre-pivot brief |
| [11-ble-and-esp32-path.md](11-ble-and-esp32-path.md) | historical | pre-pivot brief |
| [12-digital-twins-and-capture.md](12-digital-twins-and-capture.md) | historical | pre-pivot brief |

---

*Maintenance: when you add a doc under `beta/`, `docs/` or `design/`, add it to §B and §F in the same
change; when you ship a capability, add or update its §A row; when a doc is superseded, reclass it in §F
and give it the ⛔ banner. CI (`code/tests/test_project_map.py`) enforces `beta/`+`docs/`+`design/`
coverage, the living-vs-⛔ rule and link validity; `findings/`, `sessions/` and `docs/reviews/` keep their
own CI-guarded indexes, and this map points to them rather than duplicating them.*
