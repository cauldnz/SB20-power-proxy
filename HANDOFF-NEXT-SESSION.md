# HANDOFF → next session (the hardware machine)

**Written 2026-07-10.** Pick this up on the machine that has the nRF USB dongles, ESP32 boards, nRF
devices, and the ANT+ stick. **Everything is committed + documented in-repo — nothing lives only in a
chat.** This doc is the *operational* "how to resume + build + verify here"; the technical plans are the
linked docs.

> ⭐ **Read order:** this doc → [`CLAUDE.md`](CLAUDE.md) → [`PROJECT-MAP.md`](PROJECT-MAP.md) →
> [`code/findings/architecture-remediation.md`](code/findings/architecture-remediation.md) (the live
> execution tracker). Then the specific docs in §3.

---

## 0. TL;DR

- All work is **merged to `main`** via **PR #251** (green CI: pytest 3.10/3.12 + firmware). It was on
  `feat/nrf-ant-spoof`; that PR was a linear superset of the OBC stack, so it **subsumed PRs #248 + #250**.
  **On the new box, just work off `main`.**
- The big win of *this* machine: it has hardware the desk box lacked, so the **hardware-gated
  verifications in §4 are now unblocked** — that's the highest-value thing to do first. Everything shipped
  so far is compile- + host-tested; the on-air / persistence / HTTP round-trips have been waiting for a
  board.

---

## 1. Get it building here (do this first)

```powershell
git fetch origin
git checkout main                         # the arc merged to main via PR #251
git pull

# Toolchain (pinned build/flash env) + pre-flight — see tools/README.md
tools\provision-dev-env.ps1
tools\doctor.ps1                          # the build/flash gate: PlatformIO, bleak, esptool, ports
```

```bash
# Python desk tooling + tests (from code/)
cd code
python -m venv .venv && .venv\Scripts\activate      # (or source on WSL)
pip install -e ".[dev,analysis,ble]"
pytest -q                                            # expect ~462 passed
ruff check src tests                                 # CI lint scope
```

**Sanity builds (all should pass with no hardware):**
| What | Command (cwd) |
|---|---|
| ESP32 core host tests | `pio test -e native` (in `firmware/`) |
| nRF core host tests | `pio test -e native` (in `firmware-nrf/`) — **28/28** |
| ESP32-C3 shippable build | `pio run -e esp32c3-oled-live-ota` (in `firmware/`) |
| nRF XIAO build | `pio run -e xiao-sense` (in `firmware-nrf/`) |
| nRF Feather build | `pio run -e feather-nrf52840` (in `firmware-nrf/`) |

**Flashing** (ports will differ on this machine — discover them; don't trust old COM numbers):
- ESP32-C3: `code/scripts/flash_c3.py` (auto-selects the newest esptool) or `firmware/flash.ps1`.
- nRF (XIAO / Feather, Adafruit UF2 bootloader): `pio run -e xiao-sense -t upload` (CDC, auto 1200-bps
  touch; double-tap RST for the UF2 drive if it wedges). BLE-OTA also works (buttonless DFU).

---

## 2. What shipped this arc (newest first)

| Commit | What |
|---|---|
| `a2a0631` | **R1a** — extract `firmware-nrf/src/BridgeConfigStore.h` (LittleFS persistence seam). main.cpp 1495→1418; behaviour-preserving. |
| `950300c` | The **architecture remediation plan** (findings-indexed checklist). |
| `d58426e` | **Wire-format parity tests** (ANT + OBC order) + fix the **SPA scale/offset "lying control"**. |
| `e84d189` | `forEachFormField` dedup (5 copies → 1) + `/setup/save` merge fix (was wiping mode/curve). |
| `d1cec05` | **Spoof/corrector mode selector** in the shared SPA, coherent across platforms + BLE/ANT + the ESP32 `POST /config`. |
| `5a2c8ff` | **BLE SB20 crank-spoof mode** ported to the nRF (Stages 0x2F framing + identity + 442 calibrate). |
| (earlier) | nRF P1 ANT codec, P2 board seam + Feather env, and the OBC stack it's based on. |

Everything above is **compile- + host-verified**, and **merged to `main` via PR #251 with green CI**
(pytest 3.10/3.12 + firmware). The on-air / on-hardware behaviour is §4.

---

## 3. The durable docs (read these — the knowledge is here, not in chat)

- **[`code/findings/architecture-remediation.md`](code/findings/architecture-remediation.md)** — ⭐ the
  R1–R4 structural cleanup checklist. **R1a done; R1b (`BridgeService`) is next.** Tick boxes as slices
  ship.
- **[`code/findings/nrf-roadmap.md`](code/findings/nrf-roadmap.md)** — nRF completeness (BLE spoof ✅;
  ANT+ spoof S340-gated; P4 radio seam).
- **[`code/findings/decisions.md`](code/findings/decisions.md)** — the **2026-07-10** entries: the BLE
  spoof port, the spoof UI, and the architecture audit (the evidence behind the remediation plan).
- **[`firmware-nrf/GATT.md`](firmware-nrf/GATT.md)** — the Bridge GATT contract (Config char `0002` incl.
  the `spoof` bit + the reboot caveat).
- **[`web/HTTP-API.md`](web/HTTP-API.md)** — the ESP32 SPA HTTP surface (incl. the new `POST /config`).
- **[`design/spoof-ui-mockup.png`](design/spoof-ui-mockup.png)** — the spoof/corrector UI in all four
  platform states.

---

## 4. Hardware verifications NOW UNBLOCKED — do these first (highest value)

The desk box could only compile + host-test. **This machine can verify the parts that were gated.** In
rough priority:

- [ ] **R1a persistence round-trip (nRF).** Flash `xiao-sense`. Set a config / curve / button binding
  (via the web app over Web Bluetooth, or the serial console), **reboot**, confirm they survive. This is
  the LittleFS glue R1a moved that isn't host-testable. Quick, high-confidence.
- [ ] **ESP32 SPA-over-HTTP — the "unverified until U4" path.** Flash `esp32c3-oled-live-ota`, join WiFi,
  open `http://<board-ip>/app`. Verify end-to-end: (a) the **spoof/corrector toggle** actually
  `POST`s `/config` + persists (reboot to apply the identity); (b) **Scale/Offset are hidden** on the
  ESP32 (curve-only, the `scalarCorrection` gate); (c) `/setup/save` now **preserves mode + curve** (set
  corrector+curve, change the source via `/setup`, confirm it didn't revert to spoof / wipe the curve).
  This validates a whole path that is only compile-verified today.
- [ ] **nRF BLE spoof identity (partial R3 — no SB20 required).** Flash the spoof-mode build (config
  `spoof`=on, or serial `SPOOF1` + reboot). Use the **sniffer dongle** or **nRF Connect** on a phone to
  confirm it advertises as `Stages 62144` with the Stages proprietary service in the scan response, emits
  the 0x2F measurement, and answers the `0x10` enhanced-offset with company-id **442** + the captured mfg
  data. **Full R3** (pair a real SB20 + complete calibrate/zero) still needs the SB20 bike.
- [ ] **Wire-format parity** — already host/CI-green (`code/tests/test_wire_format_parity.py`); no
  hardware needed, but good to run.
- [ ] **ANT+ stick** — enables the **Python** ANT tooling (`scripts/01_capture_stages.py`,
  `03_static_replay.py --radio ant`, `04_run_proxy.py`, `16_scan_ant.py`). NOTE: the **nRF** ANT spoof
  stays **S340-gated** — it needs the licensed S340 SoftDevice + ANT network key downloaded from
  thisisant.com (owner login) into `firmware-nrf/vendor/softdevice/`. Until then, ANT on the nRF is a
  compile-only seam.

---

## 5. Recommended next dev work

1. **Run §4** — turn the compile-verified work into hardware-verified work; log results into
   `decisions.md` and tick the boxes here.
2. **R1b — extract `BridgeService`** (`~350` lines of GATT callbacks out of nRF `main.cpp`) per
   `architecture-remediation.md`. Its own branch/PR; host-test + compile, then bench-verify the GATT
   round-trip on the nRF you now have. Then R1c (`ImuRecorder`), R1d (the radio seam) — the plan doc has
   the order + rationale.
3. (Done — the arc merged to `main` via PR #251 with green CI. Separate open PR **#242** (a docs retro)
   is unrelated and still open if you want it.)

---

## 6. Gotchas carried from this session (save yourself the debugging)

- **nRF builds at `gnu++11`, ESP32 at `gnu++17`.** ODR-using a class-static `constexpr` array on the nRF
  needs an out-of-line def the shared header can't give → use literal-index constant reads (see
  `cpWriteCb` + decisions.md 2026-07-10). `firmware-nrf` to `gnu++17` is a deferred follow-up.
- **ESP32 has NO ANT.** ANT is the nRF's differentiator and is S340-gated (above).
- **The nRF `main.cpp` is mid-refactor** — R1a moved persistence to `BridgeConfigStore`; it still holds
  the Bridge GATT service, IMU, and the radio glue (R1b–R1d). Expect to keep extracting.
- **Concurrent Claude sessions share this repo** — `git fetch` before touching anything; the working-tree
  change to `sessions/session-10-spin-bike-ui-tryout.md` is **not ours** (leave it).
- Flashing: esptool ≥4.11 for the C3 USB-JTAG (`flash_c3.py` handles it); NVS/LittleFS survive a flash
  (so config persists across reflashes — good for the §4 round-trip tests).

---

*When you finish a verification or a slice, tick its box (here for §4, in
`architecture-remediation.md` for R1x), and promote any durable finding to `decisions.md`.*
