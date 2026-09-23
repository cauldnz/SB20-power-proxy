# Code inventory — appendix to the 2026-09-23 state-of-the-repo review

**Baseline:** `origin/main` = `8a64161`. Read-only; nothing here was changed by the review. Paths are
repo-relative and written in backticks (not links) so this snapshot never breaks the link guard.

## A. Python package `code/src/sb20proxy/` (71 modules)

| Area | Modules | Purpose | Test coverage |
|---|---|---|---|
| core | `core.py`, `reading.py`, `transform.py`, `calibration.py`, `config.py`, `cli.py` | `ProxyCore` relay, `PowerReading`, scale/offset/grid transforms, the meter-to-meter fitter, TOML config, console entry | covered (`test_replay_and_core`, `test_transform`, `test_calibration*`, `test_config`) |
| `ant/` | `pages.py`, `fec.py`, `master.py`, `openant_master.py`, `usb_select.py` | ANT+ Bike Power and FE-C codecs, the master seam with a loopback, the real openant radio, stick pinning | covered except `usb_select.py` (hardware-only) |
| `ble/` | `cps.py`, `ftms.py`, `ftms_erg.py`, `crank.py`, `twin.py`, `loopback.py`, `source.py`, `multi_capture.py`, `shifter_erg.py`, `sniffer.py`, `winrt_peripheral.py` | CPS/FTMS codecs (twins of `Cps.h`/`Ftms.h`), erg controller, spoofed crank over a loopback GATT, capture replay, multi-device capture helpers, sniffer address matching, the WinRT peripheral | covered (`winrt_peripheral` import-only) |
| `sources/`, `targets/`, `twins/` | `ant_power.py`, `replay.py`, `base.py`; `stages_ant.py`; `bike.py`, `meter.py`, `trainer.py`, `transport.py` | proxy I/O seams and the in-process device twins | covered; `sources/base.py` is an unused re-export shim |
| `ride/` | `director.py`, `control.py`, `state.py`, `server.py`, `webapp.py`, `replay.py`, `workouts.py` | Ride Director: plan engine, agent control API, live state, dashboard | covered (9 test files) |
| `workout/`, `mcp/` | `builder.py`, `importers.py`, `session.py`; `server.py`, `driver.py` | workout spec builder, ZWO/FIT import, session verbs; the MCP server and drive loop | covered |
| `analysis/` | `jsonl_sqlite.py`, `pcap_sqlite.py`, `fit_sqlite.py`, `annotations.py`, `diag.py`, `_jsonl.py` | the rebuildable SQLite index over captures, FIT import, annotations, `/diag` parsing | covered |
| `qa/`, `ota/` | `acceptance.py`, `route_check.py`; `sign.py` | pre-ship acceptance verdict, live route checks; ed25519/BLAKE2b signing | covered |
| standalone | `compare.py`, `fitcompare.py`, `llm.py`, `obs.py`, `logparse.py` | MeterCompare twin, FIT-vs-broadcast comparison, Ollama client, OTLP emitter, `/log` parser | `llm.py`, `obs.py`, `logparse.py` have no consumer outside their own tests |

Packaging: `pyproject.toml` entry point `sb20proxy = "sb20proxy.cli:main"` resolves; extras `dev`,
`ble`, `analysis`, `ota`, `mcp` (`mcp>=1.0,<2`, pinned after 2.x renamed FastMCP). CI installs
`dev,ota,mcp` only; `ble`/`analysis` code is import-guarded. Python version string `0.0.1`.

## B. Scripts `code/scripts/` (49 + 3 runbooks)

Numbering runs `00`–`10`, `12`–`16` (gap at 11); two collisions (`03_ingest_jsonl_to_influx` /
`03_static_replay`, `04_run_proxy` / `04_summarize_capture`); 27 scripts are unnumbered. Every
script is named in at least one Markdown document; the least-cited are `check_generated.py` (one
doc) and `RIDE-WEB.md` (one doc).

| Group | Scripts |
|---|---|
| Capture | `01_capture_stages`, `02_capture_assioma`, `06_capture_ble`, `07_capture_multi`, `capture_ble_multi`, `capture_ftms`, `sniff_ble`, `15_monitor_ride`, `16_scan_ant`, `run_capture.sh` |
| Analyse / fit | `00_validate_capture`, `04_summarize_capture`, `05_diff_captures`, `08_analyze_grid`, `09_fit_calibration`, `12_compare_fit`, `13_build_sqlite`, `14_build_pcap_fit`, `03_ingest_jsonl_to_influx`, `compare_meters` |
| Proxy / replay / twins | `03_static_replay`, `04_run_proxy`, `10_bike_twin` |
| Ride / erg / MCP | `ride_web`, `ride_control`, `ride_wizard`, `ftms_workout`, `ftms_hw_loop`, `mcp_workout_server`, `import_workout` |
| Beta / QA / OTA / flash | `parse_diag`, `qa_board`, `route_smoke`, `route_baseline`, `ota_sign`, `build_factory_bin`, `flash_c3`, `flash_s3` |
| Bench fakes and probes | `fake_meter`, `fake_crank`, `crank_reader`, `obc_reader`, `perf_soak`, `bench_s3` |
| Generators | `gen_bridge`, `gen_webjson`, `check_generated` |
| Runbooks | `BLE-LOOP.md`, `PC-CRANK.md`, `RIDE-WEB.md` |

## C. ESP32 firmware `firmware/`

**Pure core `lib/proxy/` (53 headers + `spoofs/StagesSpm2.h`):** interfaces and data (`IPowerSource`,
`ICrankOutput`, `PowerReading`); codecs (`Cps.h`, `Ftms.h`, `Obc.h`); the relay and correction
(`ProxyCore`, `Correction`, `CalibrationFit`, `CalibrationSession`, `MeterCompare`, `CompareService`);
configuration and identity (`Config`, `RuntimeConfig`, `MeterMatch`, `SetupPin`, `Provisioning`,
`Onboarding`); HTTP and web (`WebRoutes`, `WebApp`, `WebJson`, `WebSpa` [generated], `WebUi`
[token block generated], `HttpSecurity`, `Status`, `DiagReport`, `LogBuffer`, `PerfMonitor`, `PerfStats`);
OTA (`OtaManifest`, `OtaVerify`, `OtaUpdater`); UI (`UiModel`, `OledScreen`, `LcdUi`, `LcdCanvas`,
`LcdFont`, `TouchCal`, `TouchCalRitual`, `WattyBird`, `WattyBirdRender`); control flow (`CpApply`,
`LoopDrain`); controls (`Shifter`, `Sb20ButtonMap`, `ObcSb20Map`, `ObcShifterSource`,
`AntControlsSource`); workouts (`WorkoutEngine`, `WorkoutPresets`, `WorkoutRuntime`); mocks
(`MockMeter`, `MockCrank`); `SourceCandidate`, `StatusLed`.

**Hardware seam `src/`:** `main.cpp` (1,511 lines; the only NimBLE/Arduino wiring), `ConfigStore`,
`WorkoutStore` (NVS); `ble/` = `BleCrankPeripheral`, `BleMeterClient`, `BleShifterClient`,
`FtmsErgClient`, `FtmsTrainerServer`; `net/` = `WifiLink`, `WifiCreds`, `DebugLog`, `OtaPull`, `ObcNet`,
`ProvisioningDisplay`; `disp/` = `LcdDisplay` (S3), `CydDisplay`, `GuitionDisplay` + `GuitionInitCmds`,
`OledDisplay`; `ui/` = `LvglUi.cpp` (1,045 lines), `LcdTheme.h` [generated], five Inter fonts
(`lv_font_conv` output, no script in the repo); probe mains for the C3 OLED, CYD, FTMS server/client.

**Vendored libs:** `lib/monocypher/` (ed25519 + BLAKE2b), `lib/esp_lcd_axs15231b/` (Espressif QSPI
panel driver for the Guition, display-only cut).

**Native test suites (10, 347 cases):** `test_proxy` (216), `test_webroutes` (38), `test_loopdrain`
(22), `test_uiproject` (20), `test_metercompare` (14), `test_touchcal` (10), `test_compareservice` (8),
`test_wattybird` (8), `test_ant_controls` (6), `test_lvglui` (5, env `native-lvgl`, renders the real
LVGL UI headless).

**PlatformIO envs (37):** `native`, `native-lvgl`; C3: `esp32c3-supermini` (CI link guard),
`esp32c3-wifi` (+ `-ota`), `esp32c3-oled` (+ `-ota`), `esp32c3-wifi-live` (+ `-ota`),
`esp32c3-oled-live` (the `flash.ps1` default), **`esp32c3-oled-live-ota` (the shipping C3 build)**,
`esp32c3-oled96-live` (+ `-ota`), `esp32c3-oled96sh-live` (+ `-ota`), `esp32c3-wifi-live-bench`
(+ `-ota`), `esp32c3-wifi-corrector-bench`, `esp32c3-ftms-server`, `esp32c3-ftms-ergclient`,
`c3-oled-probe`; CYD: `esp32cyd`, `esp32cyd-live`, `esp32cyd-live-bench`, `cyd-probe`; Waveshare S3:
`esp32s3-min`, `esp32s3-pio-min`, `esp32s3-pio`, `esp32s3-pio-live`, `esp32s3-pio-live-bench`,
`esp32s3-pio-ota`, `esp32s3-pio-live-ota`; Guition: `esp32-guition`, `esp32-guition-live`,
`esp32-guition-live-bench`, `esp32-guition-ota`, `esp32-guition-live-ota`. Bench envs
(`METER_MATCH_ANY_CPS=1`) and mock envs (`USE_MOCK_METER=1`) are refused by `flash.ps1` without
`-Force` (`tools/PioIni.ps1`).

## D. nRF firmware `firmware-nrf/`

Pure `lib/bridge/`: `Proto.h` (Bridge GATT packets), `AntBikePower.h`, `AntMasterScheduler.h`,
`ImuCapture.h`, `PeerRole.h`, `SourceRelay.h` (reaches the ESP32 pure headers via `lib_extra_dirs`).
Seam `src/`: `main.cpp` (1,395 lines), `BridgeService.h`, `BridgeConfigStore.h`, `ant/AntMasterChannel.h`
(S340-gated), `arduino_compat.h`, `board.h`. Native suites (3, 81 cases): `test_bridge` (40, golden
vectors generated from `ui-schema/bridge.json`), `test_peerrole` (23), `test_sourcerelay` (18). Envs:
`native`, `xiao-sense`, `feather-nrf52840`, `xiao-sense-s340` (needs the licensed SoftDevice under
`vendor/softdevice/`, gitignored, present in one checkout only). `ciq/`: the Garmin Connect IQ
"Bridge Remote" app; `BridgeBle.mc` hand-mirrors `GATT.md` with no golden-vector lock. `flash.ps1`
reads the bootloader's `INFO_UF2.TXT` and refuses a SoftDevice-layout mismatch.

## E. Generated artifacts and their guards

| Source | Generator | Artifact(s) | Guard |
|---|---|---|---|
| `design/tokens.json` | `design/gen_tokens.py` | token blocks in `web/index.html`, `firmware/lib/proxy/WebUi.h`, `LcdCanvas.h`, `firmware/src/ui/LcdTheme.h` | `test_tokens_sync.py`; `check_generated.py` |
| `web/index.html` (+ inlined `bridge-codec.js`) | `web/gen_spa_header.py` | `firmware/lib/proxy/WebSpa.h` | `test_spa_sync.py`; `check_generated.py` |
| `ui-schema/bridge.json` | `code/scripts/gen_bridge.py` | `ui-schema/bridge-golden.json`, `web/bridge-codec.js`, `firmware-nrf/test/test_bridge/bridge_golden_gen.h` | `check_generated.py`; nRF native suite; Node parity test in CI |
| `ui-schema/web-json.json` | `code/scripts/gen_webjson.py` | `ui-schema/web-json.md`; checks `WebJson.h` and `index.html` field names | `test_webjson_sync.py`; `check_generated.py` |
| git HEAD | `firmware/scripts/build_version.py` | build-time SHA and time defines | build-time, no guard needed |
| pio build | `code/scripts/build_factory_bin.py` | `firmware/webflash/manifest.json` (committed; version = a short SHA) | **none** |
| `design/fonts/inter/*.ttf` | `lv_font_conv` (command in each file header; no script) | `firmware/src/ui/fonts/lv_inter_*.c` | **none** |
| font8x8 | "regenerated mechanically" (no script) | `firmware/lib/proxy/LcdFont.h` | **none** |
| `docs/diagrams/*.mmd` | `scripts/render.sh` (local mmdc) | `docs/diagrams/*.svg` | **none** |
| `firmware-nrf/GATT.md` | hand-mirrored | `firmware-nrf/ciq/source/BridgeBle.mc` | **none** |

Pre-push hook `.githooks/pre-push` runs `check_generated.py` (installed by `tools/install-hooks.ps1`).

## F. CI `.github/workflows/tests.yml`

| Job | What it does |
|---|---|
| `changes` | path filter; the firmware matrix runs on every push and on PRs touching `firmware/`, `firmware-nrf/`, `web/`, `ui-schema/`, `tools/`, `.github/workflows/` (fails open) |
| `pytest` (3.10, 3.12, 3.14) | `pip install -e ".[dev,ota,mcp]"`; `ruff check src tests` plus the four generators; `pytest -q` |
| `firmware` | PlatformIO pinned from `tools/dev-env.lock`; `pio test -e native`; `tools/tests/Test-PioIni.ps1 -RequireReal`; `pio test -e native-lvgl`; `pio run -e esp32c3-supermini`, `esp32c3-wifi`, `esp32c3-oled-live-ota`, `esp32cyd`; nRF `pio test -e native`, `pio run -e xiao-sense`, `feather-nrf52840` |
| `bridge-parity` | `python code/scripts/check_generated.py`; `node web/test/bridge-codec.test.mjs` |

Not compiled in CI: all `esp32s3-pio*` and `esp32-guition*` envs (#323), `esp32cyd-live*`, the
`oled96*`, bench, FTMS and probe envs, and `xiao-sense-s340` (licensed SoftDevice).

## G. Captures `code/findings/captures/`

54 files, 43,125,668 bytes: 29 `.jsonl`, 11 `.pcap`, 5 `.txt`, 5 `.fit`, 1 `.json`, 1 `.gz`, plus the
README and `.gitkeep`. `code/captures.sqlite` (45.5 MB) is the derived index. The README indexes 15
files; 37 are unindexed (every `SHIFTER-probe-*`, `SNIFF-*`, the session-13 files, the Garmin FITs,
the session-8/9 evidence logs, `RIDE-*`, `CAL-*`, `F-ftms-*`, `MANIFEST-ride-*`); none indexed is missing.

## H. `tools/`, `web/`, `ui-schema/`, `design/`

`tools/`: `PioIni.ps1` (effective PlatformIO config + the ride guard) with `tests/Test-PioIni.ps1`,
`doctor.ps1`, `provision-dev-env.ps1` + `dev-env.lock`, `install-hooks.ps1`, `secrets-*.ps1`.
`web/`: the shared SPA `index.html`, generated `bridge-codec.js`, `gen_spa_header.py`, the Node
parity test, `deploy.sh` (GitHub Pages), `HTTP-API.md`, `README.md`. `ui-schema/`: `bridge.json`,
`bridge-golden.json`, `web-json.json`, `web-json.md` (generated). `design/`: `tokens.json` +
`gen_tokens.py`, 11 locked LCD mockup HTML files, 41 rendered PNGs + `render/_gen.py`, the Inter
fonts, the S3 case (Fusion script, STLs, renders), the UI design docs, the two remote-render recipes.

## I. Counts and oddities

- TODO/FIXME/XXX/HACK in source: 2 (`AntControlsSource.h` line 14, `firmware-nrf/src/main.cpp` line 1367).
- Version strings: Python `0.0.1`; firmware `SB20_FIRMWARE_VERSION "0.1.0"`; `lib/proxy/library.json`
  `0.1.0`; `webflash/manifest.json` a short SHA.
- Stale references: `firmware/platformio.ini` line 89 (`readboot.py`, missing); `design/render/_gen.py`
  (absolute repo and Chrome paths); example capture filenames in the docstrings of scripts 01, 02,
  03, 07 and 16 that never existed; `sb20proxy/ble/ftms.py` header "pending real-capture validation"
  while `G-sb20-ftms-erg*.jsonl` captures exist.
- Deliberate duplicates (recorded in `architecture-remediation.md` R7): the capture-side CPS decoder
  in `06_capture_ble.py`; `decode_fec` in `07_capture_multi.py`; `linfit` in `08_analyze_grid.py`.
