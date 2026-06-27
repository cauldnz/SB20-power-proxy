# Forward Plan — SB20 Power Proxy (post Phase 0)

> **➡️ North star: [`pre-beta-plan.md`](pre-beta-plan.md)** (2026-06-22) — the goal-focused plan to ship the
> SB20 meter/crank proxy to ~10 collaborator-testers. This doc remains the **technical backlog** it draws on.

**Status: Phase 0 substantially complete and de-risked. This is the operational plan to get from
"we know it will work" to a working proxy.** Last updated 2026-06-16.

This doc operationalises [`phase-0-report.md`](phase-0-report.md) §6 and
[`05-implementation-phases.md`](../../05-implementation-phases.md). Read the report first for the
*why*; this is the *what next, in what order, blocked on what*.

> **The one thing to internalise:** the desk build and the next bike session are **independent**
> until they meet at the Phase 1 pairing test. Build at the keyboard first; then a single bike
> session clears every open item *and* runs the pairing proof in one visit.

---

## 0. The two lanes (what can run in parallel)

```
   LANE 1 — DESK / CODE (start now, no bike)          LANE 2 — BIKE (one ~90 min session)
   ───────────────────────────────────────           ──────────────────────────────────
   Phase 1A: build ANT+ master + replay               • Fresh CR2032 in the crank
     • page encoders (+ round-trip tests)             • Crank-length authority experiment
     • StagesAntTarget (openant master)               • External/Power-Erg simplicity probe
     • ReplayFileSource                               • Session G Part C: erg-on-BLE GATE
     • ProxyCore + 03_static_replay.py                • Session G Part A: BLE recon
     • bench loopback (stick, no bike)                • Firmware + R-crank battery check
   Phase 2 pre-build (parallel, optional):
     • AssiomaAntSource (RX/slave — well-trodden)
                    │                                                 │
                    └──────────────►  CONVERGE  ◄─────────────────────┘
                              Phase 1B: pairing test
                    (built code + real bike + cranks isolated, §3)
```

Everything in Lane 1 is **pure desk work** except the optional *bench loopback*, which needs an
ANT+ stick (and the built TX stack) but **not** the bike. The Phase 2 source is RX/slave — the
Phase 0 path — so it can be pre-built anytime during Lane 1. Everything in Lane 2 needs you
physically on the SB20. The lanes don't block each other — do Lane 1 this week, Lane 2 next ride.

**Recommended sequencing:** finish Lane 1 Phase 1A *before* the next bike session, so Phase 1B
(pairing test) rides along with the open-item run-cards in a single trip. **If Phase 1A isn't built
yet, just do Lane 2 run-cards 1–6 and defer Phase 1B to a later visit** (see §3).

---

## 1. Critical path & dependency graph

```
Phase 1A (desk build) ─┐
                       ├─► Phase 1B (bike pairing test) ─► Phase 2 (live Assioma) ─► Phase 3 ─► Phase 4
Lane 2 battery + setup ┘                                        ▲
                                                                │ (Phase 2 source is desk-buildable
                                                                │  in parallel with Phase 1B)
Lane 2 Session G Part C (erg-on-BLE GATE) ──► Track C (ESP32/BLE/twins)   [gated; only if Part C PASSES]
```

- **Phase 1B is the keystone.** If the SB20 accepts our spoofed master, the entire approach
  (ANT+ *and* BLE) is proven. Everything downstream assumes this passes.
- **Track C (ESP32/BLE) is gated on the bike go/no-go (Session G Part C).** Don't invest desk
  time in ESP32 firmware until that gate passes — except cheap prep (digital twins from the
  `G-*` captures we already have), which is optional.
- **Phase 2's source (`AssiomaAntSource`) is RX/slave** — the thing we've already done a dozen
  times in Phase 0 — so it can be **pre-built at the desk in parallel** with Phase 1B and just
  swapped in once Phase 1B passes.

---

## 2. LANE 1 — Desk build (Phase 1A): the ANT+ master + static replay

Goal: **implement and then stand up** the transmit side and prove it's well-formed **before**
touching the bike. *None of the modules below exist yet* — only the ABCs (`PowerReading`,
`PowerSource`, `PowerTarget`) and a `cli.py` stub are in the tree; the build order is the literal
next work. All of it is unit-testable against the captured `raw_hex`; no hardware until the
optional bench loopback.

**Verified foundation (checked against the installed openant 1.3.4 in WSL, not assumed):**
`Channel.Type.BIDIRECTIONAL_TRANSMIT` exists. Open a master via
`node.new_channel(ctype=Channel.Type.BIDIRECTIONAL_TRANSMIT)` → `set_id(62144, 0x0B, 5)` →
`set_period(8182)` → `set_rf_freq(57)` → `enable_extended_messages(1)` → `open()`. Drive the rotation
from the `on_broadcast_tx_data` callback (fires each channel period — return the next 8-byte page)
and call `send_broadcast_data([8 bytes])`. The bike's manual-zero **request** arrives on the master
channel as acknowledged data via `on_acknowledge_data` (verified method present). Prior-art pattern:
`dhague/vpower` (study the architecture, it's permissive; **do not** copy GPL-3 qz code).

### Build order (each step independently testable)

| # | Module (new file) | What it does | Desk test |
|---|---|---|---|
| 1 | `src/sb20proxy/ant/pages.py` | `encode_*()` for pages 0x10/0x12/0x13 — the **inverse** of `01_capture_stages.py:decode_page`. | **Round-trip gate:** for every `raw_hex` in `A-stagesL-steady`, on **protocol bytes 0–7 only** (the capture's `raw_hex` is 13 bytes — strip the 5-byte ext tail `flag+devnum+devtype+transtype`), `encode(decode(bytes0_7)) == bytes0_7`. Pure software. |
| 2 | `src/sb20proxy/ant/common_pages.py` | `encode_0x50/0x51/0x52` (mfr 69, model 3, hw 3, fw 1.8.2, serial 11821518) and the **`0x01` calibration response** `[01, AC, FF, FF, FF, FF, offset_LE]`. | Round-trip (bytes 0–7) against the captured commons + the C0 calibration bytes. |
| 3 | `src/sb20proxy/sources/replay.py` `ReplayFileSource` | Reads the capture JSONL; paces by `monotonic_s` deltas (delay before record N = `rec[N].monotonic_s − rec[N-1].monotonic_s`; first record emits immediately). **Two modes:** (a) *verbatim* — yield `raw_hex` bytes 0–7 as-is (preserves the real page interleave for free); (b) *decoded* — yield `PowerReading` and let the target re-encode. | Unit test: record count + cumulative timing vs the source file; both modes. |
| 4 | `src/sb20proxy/targets/stages_ant.py` `StagesAntTarget` | openant master on 62144 driven by `on_broadcast_tx_data`. **Verbatim mode** just emits the source's bytes (no scheduler needed — Phase 1's fast path). **Decoded mode** runs a page-rotation scheduler (≈53% 0x12 / 27% 0x10 / 17% 0x13 per Session A, with a `[0x50,0x51,0x52]` commons burst every ~30 s) — needed for Phase 2. On the bike's zero-reset request (`on_acknowledge_data`), inject the `01 AC FF FF FF FF <offset_LE>` response as a **broadcast** page 0x01 for the next few cycles (this is how the real crank replied in Phase 0; fall back to `send_acknowledged_data` if the bike rejects it — confirm in 1B). | Bench loopback (below). Logic-test the decoded-mode scheduler against the captured page distribution. |
| 5 | `src/sb20proxy/core.py` `ProxyCore` | Owns one source + one target; wires `source.on_reading → target.push_reading`; tracks last-reading-age, dropouts, calibration events. | Wiring test: a `FakeSource` emitting 5 varied `PowerReading`s into a `FakeSink` via `ProxyCore` — assert all 5 arrive in order, unchanged. |
| 6 | `scripts/03_static_replay.py` | Runnable entry point. **CLI:** `--input <jsonl>` (required) · `--spoof-id <int>` (default 62144; use a distinct id for the on-bike proof, see §3) · `--mode verbatim\|decoded` (default verbatim) · `--dry-run` (no radio) · `--duration <s>` (optional cap, else whole file) · `--loop` (repeat). | `--dry-run` smoke test in CI. |
| 7 | `tests/` (+ `conftest.py`) | Round-trip, scheduler, wiring, replay-timing. Add a 10 s excerpt fixture of `A-stagesL-steady`. | `pytest` green, no hardware. |

**Staging tip:** ship **verbatim raw replay first** (step 3a → minimal target → bike) to answer
"is our air signal even well-formed?" with the least new code. Then do **decoded replay** (3b +
the encoders) to exercise the `PowerReading` path that Phase 2 depends on. This separates "is the
TX valid?" from "is the encoder correct?" — two failure modes you don't want tangled at the bike.

### Progress (2026-06-15 desk session) — Phase 1A code-complete; software loopback PASSES

**The whole pipeline is built, tested against real captures, and runs end-to-end in software
(no stick, no bike). 35 tests, ruff clean, CI on every push.**
- ✅ **Step 1+2 — codec** (`ant/pages.py`): `encode_page`/`decode_page` for all 7 pages.
  *Consolidated the planned two files into one.* Round-trip gate green over **all 3,209 real
  captured pages**.
- ✅ **Step 3 — `ReplayFileSource`** + ✅ **Step 5 — `ProxyCore`**: the decoded replay path.
- ✅ **Step 4 — `StagesAntTarget`** (`targets/stages_ant.py`): decoded mode builds page 0x10 from
  the live `PowerReading` (no torque accumulators — that was the verbatim-vs-decoded finding) +
  periodic `[0x50,0x51,0x52]` identity commons; verbatim mode re-broadcasts captured pages exactly;
  a zero-reset request triggers the broadcast `01 AC … offset` response.
- ✅ **The radio seam + digital twins** (`ant/master.py`, `twins.py`): `AntMaster` ABC with a
  pure-software **`LoopbackMaster`** (in-process "air") and a **`BikeTwin`** (software SB20). The
  real-stick adapter `OpenAntMaster` (`ant/openant_master.py`) is the only piece left for the bench
  (verified by API surface, runtime not unit-testable).
- ✅ **Step 6 — `03_static_replay.py`**: `--radio loopback` (default, no hardware) drives a
  `BikeTwin` and prints what it sees; `--radio ant` for the real stick.

**Loopback verified** (`tests/test_loopback.py` + a live run): `ReplayFileSource → ProxyCore →
StagesAntTarget → LoopbackMaster → BikeTwin` — the twin sees real replayed power, the Stages
identity (mfr 69, serial 11821518), and a working zero-reset handshake (offset 903). This *is* the
digital-twin bench: from here we add more twins (a meter source twin, a BLE display twin) and test
the proxy fully without riding.

**Only hardware step remaining:** confirm `OpenAntMaster`'s runtime on a real stick (the hardware
loopback below), then the SB20 pairing test (Phase 1B, `NEXT-BIKE-SESSION.md` §7).

### Two loopbacks

- **Software loopback (done):** `LoopbackMaster` + `BikeTwin` already prove the page bytes,
  scheduling, identity, and calibration handshake end-to-end with no hardware — run any time via
  `pytest` or `03_static_replay.py --radio loopback`. This is the primary regression net and the
  digital-twin foundation.
- **Hardware loopback — ✅ PASSED (two sticks, 2026-06-15):** `03_static_replay.py --radio ant
  --usb-index 0 --spoof-id 62145` transmitted the spoofed crank; `10_bike_twin.py --usb-index 1
  --device-id 62145` received it as a **`BikeTwin` over a real ANT+ slave** — real power, the Stages
  identity (mfr 69, with `--commons-every` tuned), and the **zero-reset handshake over the air**.
  The `usb_select` shim pins each Node to a specific stick (openant grabs the first otherwise). **A
  single stick can't hear its own TX** (half-duplex), so the on-air RX needs a second receiver: a 2nd
  stick (used here), a phone ANT+ app / Garmin, or `pytest --run-hardware` for a single-stick TX
  smoke. Shutdown is hardened (openant's `node.stop()` could hang). **The whole radio stack is now
  hardware-validated — the only thing left is the SB20 itself (Phase 1B).**

### The twin library (the bench-testing foundation)

`twins/` is built on a **transport seam** so one twin runs three ways without changing its logic:
- `LoopbackTransport` — pure software, in CI (no openant);
- `AntSlaveTransport` — a real ANT+ stick (on-air loopback, or vs a real device);
- (later) a BLE transport for the ESP32/BLE path.

`DeviceTwin` (base) + `BikeTwin` (the SB20/display consumer) are the first members. **Roadmap:** a
`PowerMeterTwin` source twin (feed the proxy synthetic or real meter data and assert what the bike
consumes), a trainer/FE-C twin, and a BLE display twin — each pure-software in CI, real-radio on a
stick, and able to sit opposite a real device. This is what lets us bench-test Phase 2+ as digital
twins without riding.

**Phase 1A exit:** ✅ reached — 35 unit tests pass, ruff clean, and the software loopback runs the
whole pipeline (codec → replay → core → target → loopback → twin) green, including the calibration
handshake. The only things left to prove need hardware: `OpenAntMaster` on a real stick (hardware
loopback), then the SB20 pairing test (Phase 1B).

---

## 3. LANE 2 — One bike session (open items + Phase 1B)

All of these need you on the SB20. Bundle them into a single visit. See the take-to-the-bike card
[`../../NEXT-BIKE-SESSION.md`](../../NEXT-BIKE-SESSION.md) for the step-by-step. Summary and order:

| Order | Run-card | Why it matters | Gate / output | ~min |
|---|---|---|---|---|
| 1 | **Fresh CR2032 in the L crank** | It's at 14%; a dropout mid-session ruins any capture. Gates everything. | Crank battery ≥95%. | 8 |
| 2 | **Firmware + R-crank battery check** | Phase 1 pre-flight; R-crank battery never measured. | FW noted in `decisions.md`; R-crank level known. | 5 |
| 3 | **Crank-length scaling experiment** | Change the **in-meter (StagesPower app)** length 172.5→165 and watch the crank's **broadcast** watts at matched effort. Explains the measurement history. *Moot for the proxy, high value for understanding.* | Broadcast drops ~4.3% (165/172.5) → in-meter length scales the broadcast (history = the length change); no change → investigate. Record values. | 12 |
| 4 | **External / Power-Erg simplicity probe** | If the SB20 erg-controls off an external meter *without* a crank spoof, Phase 1 gets far simpler. | Feature exists & erg responds = simpler path; else crank spoof confirmed necessary. | 12 |
| 5 | **Session G Part C — erg-on-BLE GATE** | **Go/no-go for the entire ESP32/BLE direction.** Flip "Pair with Bluetooth" ON, set erg targets, confirm the bike holds power. *(Independent of Phase 1B — the ANT+ pairing proof is not blocked on this.)* | PASS → Track C viable; FAIL → ANT+/Pi is the only path. | 15 |
| 6 | **Session G Part A — BLE recon** | Captures the crank's BLE GATT/CPS/control-point surface = the template for a BLE impersonator (re-confirm the crank-length + offset-compensation reads). *(Only if Part C showed BLE erg works; skip if it failed.)* | GATT dump + crank-length read + offset-comp response logged. | 15 |
| 7 | **Phase 1B — pairing test** *(only if Lane 1 / Phase 1A is built)* | **The keystone proof.** Pair the SB20 to our spoofed master; confirm power displays and **erg reacts** to the replay. | SB20 shows replayed watts + erg responds → impersonation works. | 20 |

**Resolving the battery/isolation tension (row 1 vs row 7):** row 1 puts a *fresh* battery in the L
crank, but Phase 1B needs the live cranks not to collide on-air with our spoof. **Cleanest fix —
spoof a distinct test id** (e.g. `--spoof-id 62145`) for the proof and pair the SB20 to *that* in
the app; the live crank on 62144 is then irrelevant and you keep the fresh battery in. (Any id with
the full Stages contract — mfr 69, the page mix, the calibration response — validates the proof; the
specific number only matters when the production proxy later impersonates 62144 with the real cranks
removed.) Fallback if you'd rather test the real id: pull the L-crank battery again for the test and
reinsert after.

**If Phase 1A isn't built yet:** do rows 1–6 (clears every non-Phase-1 open item); Phase 1B rides
along on the *next* visit once the code is ready. **If Phase 1A is built:** do all 7 in one trip.

**Live monitoring:** I can read the capture JSONL off the machine while you ride (WSL path
`\\wsl.localhost\Ubuntu-24.04\...`), so you don't need terminal interactivity — just narrate in
chat ("started length test", "flipped BLE on", "pairing now").

**Phase 1B common failure modes (budget for one iteration):**
- Pairs but no power → 0x10 accumulated-power / event-count rollover encoding. Check round-trip.
- Pairs, power shows, erg dead → smoothing/latency on the bike side; or our page mix/cadence is off.
- Pairing fails after zero-reset → calibration response not accepted. **Note the subtlety:** in
  Phase 0 the crank's response arrived as a *broadcast* page 0x01 (`kind="broadcast"`), so emit the
  `01 AC FF FF FF FF <offset_LE>` response that way **first** (inject into the rotation for a few
  cycles after the request); if the bike still won't settle, try `send_acknowledged_data` as a
  fallback. This is the single most likely thing to need a hardware iteration.

---

## 4. Phase 2 — Live generic-meter proxy ✅ built in software; needs a stick to go live

**Built and proven in software (no hardware), 45 tests green.** The source is **generic** — any
standard ANT+ Bike Power meter (Assioma / Rotor InPower / XCadey / …), not Assioma-specific (the
Phase 4 goal, delivered early).

- ✅ `sources/ant_power.py` **`AntPowerSource`** — page 0x10 → `PowerReading` over the receiver
  transport (`AntSlaveTransport` real / `LoopbackTransport` CI). The live analogue of `ReplayFileSource`.
- ✅ `twins/meter.py` **`PowerMeterTwin`** — the producer twin; broadcasts power with an optional
  `error(power, cadence)` model so a meter discrepancy can be injected and a correction *verified in
  software*.
- ✅ `transform.py` — `PowerTransform` seam in `ProxyCore` (default pass-through); see §4a.
- ✅ `scripts/04_run_proxy.py` — `--radio loopback` runs rider→meter→proxy→bike-twin in software;
  `--radio ant` runs it live (`--meter-id` → `--spoof-id`).
- **Full software loop tested:** `PowerMeterTwin → AntPowerSource → ProxyCore → StagesAntTarget →
  BikeTwin` relays power, and a +10 % meter error is recovered to true power by the correction.
- **To go live (hardware):** `--radio ant` with a real meter + stick into the SB20; measure
  end-to-end latency (meter RX → crank TX) and run a 1-hour endurance ride.
- **Exit:** within ±2 W / 1%; latency <250 ms (target <100 ms); 1 h unattended; `phase-2-report.md`.

### 4a. Meter-to-meter calibration & the jersey-pocket bridge (the XCadey use case)

Goal: replace the XCadey app's "swag" offset with a **quantitative, power-dependent** correction,
and (if the error is non-linear) carry it on a small ESP32 bridge so the velodrome bike reads true
power with the XCadey alone. The pieces are mostly already in hand:

1. **Capture** both meters on one clock — `07_capture_multi.py` (XCadey as DUT + a reference, e.g.
   the Assiomas mounted temporarily, or the SB20). Ride a **power-grid** (steady holds across the
   power × cadence space; the old `RIDE-CARD` grid).
2. **Analyze** — `08_analyze_grid.py` already computes the ratio surface / regression across
   power × cadence.
3. **Fit** ✅ **done** — `09_fit_calibration.py` (+ `src/sb20proxy/calibration.py`) turns the
   dual-meter capture into a JSON correction profile: `fit_scale_offset` (linear) or `fit_grid`
   (piecewise power→factor), auto-picking grid only if it beats linear, with residual reporting and
   a meter-glitch filter. *Verified on real data:* stages reads ~9% high vs assioma on QUICK-multi.
4. **Apply** ✅ **done** — `04_run_proxy.py --profile <file>` loads the profile as the `ProxyCore`
   transform. Verified in software: inject a known non-linear error into a `PowerMeterTwin`, fit,
   and the correction recovers true power to <4 W mean.
5. **Deploy** — the same transform runs on the **ESP32 jersey-pocket bridge** (Track C / BLE):
   XCadey → corrected power → bike computer, in real time. *(The remaining track.)*

So 4 of the 5 steps are built and tested; only the ESP32 deployment remains. A real velodrome
calibration would just need a **proper power-grid ride** (longer steady holds → cleaner curve than
the 149 s QUICK-multi sample).

This is also why the power-grid stays interesting even though the SB20 path doesn't need it: it's the
calibration **product** for any meter pair, testable end-to-end as digital twins before a single ride.

Note: because the SB20 is pass-through (proven), **erg target = Assioma watts by construction** —
no calibration model on the delivery path. The power×cadence grid stays research-only.

---

## 5. Phase 3 — Robustness & packaging (desk)

Entry: Phase 2 endurance test passed. All desk work:
- Hardcoded values → TOML config (schema in `04-architecture.md`); `--validate-config`.
- systemd unit for autostart on a Pi.
- Rolling JSONL of in/out to `findings/proxy-runs/`.
- Recovery: Assioma dropout → stop broadcasting (clean drop, not stale watts); SB20 reconnect → continue.
- Status indicator (terminal/LED). Update `code/README.md` install steps.
- **Exit:** owner runs a 4-week block with only start/stop intervention; "v1" tagged.

---

## 6. Phase 4 — Distributable (desk + one volunteer)

Entry: Phase 3 + 1 week of real training. Generic ANT+ source (any Bike Power meter); optional BLE
input/target; Pi image (`setup.sh`/packer); non-expert README (shopping list, install, FAQ);
replay-based CI tests (no hardware). **Exit:** another SB20 owner installs from the README unaided.

- **Firmware install for non-technical users / bare-ESP32 buyers — ESP Web Tools.** Flash from the
  browser over Web Serial, no CLI/esptool: a static page with `<esp-web-install-button>` + a
  per-chip `manifest.json` of the firmware `.bin`s. LTS Design *already runs one* for the Respooler
  product (https://flash.lts-design.com/), so this is "replicate that page" for the proxy firmware —
  optionally with Improv-Serial WiFi provisioning (pairs with our captive portal). Caveats:
  Chrome/Edge/Opera desktop only (no Safari/Firefox/iOS), and it flashes via esptool-js (the same
  Web-Serial reset that wedged the C3 USB-JTAG on 2026-06-16/17 — test on a real C3, consider
  pre-flashed boards). For the production-deployment + sales review.

---

## 7. Track C — ESP32 / BLE / digital twins (PARALLEL, gated)

**Gate: Session G Part C (erg-on-BLE) must PASS.** Until then, do not invest in ESP32 firmware.

- **Cheap desk prep allowed now (optional):** build **digital twins** from the `G-*` BLE captures
  we already have (advert + GATT + CPS + calibration) so the BLE proxy can be developed without
  riding. Scaffold the NimBLE+OTA+OLED ESP32 firmware (`esp32_bridge_spec.md`) — it's both the
  Part B capture tool and the eventual BLE peripheral.
- **After Part C passes:** Session G Part B (impersonation capture), then the BLE peripheral build.
- BLE peripheral spec is already in `phase-0-report.md` §2 (CPS 0x1818, measurement flags 0x2F,
  control-point 0x2A66, no bonding needed).

### Progress (2026-06-17) — dual-role BLE proxy works on real hardware (both directions)

The ESP32 BLE bring-up landed as an **initial cut**, ahead of the Part C gate — because it's
validated against the **Python harness** (a WinRT CPS peripheral as the meter, a bleak central as
the consumer), which needs no SB20. So this de-risks the firmware/codec/relay without spending the
bike session. See `decisions.md` 2026-06-17 and `code/scripts/BLE-LOOP.md`.

- **Firmware:** the dual-role proxy (`BleMeterClient` central → `ProxyCore` → `BleCrankPeripheral`)
  now runs in a flashable build (`esp32c3-{wifi,oled}-live`, `USE_MOCK_METER=0`). Central matches
  the meter by **CPS service UUID** (Windows advertises no name), **skips `SPOOF_NAME`** advertisers
  (never read FROM the crank we impersonate — fixes latching onto a sibling board / the real
  Stages crank), **relays cadence** (Crank Revolution Data → recovered rpm), and has a **staleness
  watchdog** that drops a silent link and rescans (self-heal, callback-independent).
- **Observability:** web UI `/ui` shows `METER IN → CRANK OUT`; `/` JSON carries `src_*` (received)
  + `power_w`/`cadence_rpm` (broadcast). OLED shows live power/cadence/IP.
- **Verified on hardware:** goal #1 (ESP32 received 177 W / 90 rpm from `fake_meter`), goal #2 +
  full chain (Python `crank_reader` read the relayed 177 W / 90 rpm off the spoofed crank),
  connect→meter-gone→reconnect lifecycle. Flashed by **OTA** (worked at RSSI −73).
- **Still ahead:** **FTMS** (Indoor Bike Data receive + erg `Set Target Power`) is the next layer —
  not in this cut (no FTMS in firmware yet). And the real test is still **Session G Part C** (does
  the *SB20* accept our spoofed CPS crank for erg) — the harness proves the firmware, not the bike.

---

## 8. Open research items (not on any critical path)

> **✅ RESOLVED — [SB20 power-topology](sb20-power-topology.md) (session 7, 2026-06-22).** The SB20 reports
> the **Stages crank 1:1**, and both the SB20 and the Stages crank read **~11 % high vs the Assioma (≈1.11×)** —
> overturning session-4's "~½–⅔ / ~30 % low" (a transient spot-read, not the steady ratio). Settled by the
> simultaneous multi-device capture now indexed in `captures.sqlite` (see `decisions.md` 2026-06-22). Erg
> targets therefore track the Stages crank (~11 % above the Assioma); no spoof-feed mystery remains. Open
> follow-up is only the Garmin `.FIT` cross-check against the streams already in `captures.sqlite`.

From `phase-0-report.md` §5 — track, don't block on:
- Crank-length scaling (closed by Lane 2 run-card #3).
- ANT+ vs BLE offset semantics (903 vs ~0) — ✅ **RESOLVED (decisions.md 2026-06-19).** Same calibrated
  state in two representations: ANT+ page-0x01 reports the **raw zero-offset** (903); BLE Start/Enhanced
  Offset Compensation returns the **post-comp residual** (~0, captured `200c010000`). Our ESP32 is the BLE
  crank, so it answers **0** (`SPOOF_CAL_OFFSET` 903→0, PR #7); the ANT+/openant path keeps 903.
- Does the SB20 BLE pair accept any CPS peripheral or only Stages-branded? — needs hardware after Part C.
- Power×cadence calibration grid — research-only; the proxy eliminates the discrepancy without it.
- **Captive-portal SSID picker UX (ESP32 firmware)** — ✅ **done (2026-06-16).** The portal now
  renders a **tap-list picker** instead of the old `<input list=…>` + `<datalist>` (whose dropdown
  is unreliable on iOS Safari, so it read as "type it blind"). *Decision:* tap-list over `<select>`
  — `<select>` is native/reliable but can't show signal/lock and has no inline manual entry; a
  tap-list of buttons shows rich rows and is what ESPHome/Tasmota/WLED portals use, and inline JS
  runs fine in both the iOS Captive Network Assistant and the Android captive webview. Each scanned
  network is a tappable row that JS drops into the SSID field and jumps to the password, with a
  **visible SSID text field kept as the no-JS / hidden-network fallback**. The scan is enriched —
  **RSSI** (sorted strongest-first), **mesh/multi-AP dedup** (keep the strongest per SSID), and an
  **open/secured padlock** (`WiFi.encryptionType`) — and a **"Rescan"** link runs an **async**
  `scanNetworks(true)` (no captive-DNS stall) with a meta-refresh that polls until results land.
  Scan model widened `vector<string>` → `struct ScannedNet{ssid; rssi; secured}` flowing
  `WifiLink` → `renderProvisioningPage`; dedup/sort/escape/render stay pure and are covered by
  extended `test_portal_page_*` host tests (green in CI; this Windows box has no native x86
  toolchain, but the test TU type-checks clean under GCC and the firmware build is green). Firmware
  build `esp32c3-ota` ✅. Files: `firmware/src/net/WifiLink.{h,cpp}` (scan + `/rescan`),
  `firmware/lib/proxy/Provisioning.h` (model + render), `firmware/test/test_proxy/test_main.cpp`.
- **Captive-portal password field — suppress the "strong password" overlay** — ✅ **done
  (2026-06-16).** The key the rider types is an *existing* WiFi credential, but a `type=password`
  field made iOS Safari (and the Captive Network Assistant webview) pop the "Use Strong Password"
  generator + save prompt over it (`autocomplete='off'` is ignored for password fields). *Decision:*
  render a **masked TEXT field** (`type='text'` + CSS `-webkit-text-security:disc` +
  `autocomplete='off' autocapitalize='off' autocorrect='off' spellcheck='false'`) rather than rely
  on `autocomplete='current-password'` — a non-password field is never classified as a credential,
  so no overlay can fire, and `-webkit-text-security` is supported by WebKit + Blink (every
  captive-portal browser: iOS Safari, iOS CNA webview, Android Chrome). A **Show/Hide toggle**
  (`revealPass()`) replaces the missing native reveal. Covered by a new
  `test_portal_page_password_not_a_credential_field` host test (full reasoning in
  `decisions.md` 2026-06-16). On-device behaviour across the three browsers is a bench check.
  Files: `firmware/lib/proxy/Provisioning.h`, `firmware/test/test_proxy/test_main.cpp`.
- **ESP32 perf + WiFi/BLE/OLED coex stability tuning** — backlog. The C3 runs WiFi + dual-role
  NimBLE + the 0.42" OLED (I2C @ 50 kHz) on one radio/bus and intermittently hangs/crashes under
  load (LED + screen freeze → boot-guard reboots → reconnects; heap stays flat, so a coex/blocking
  stall, not a leak — the README's flagged "later tuning job"). *Tasks:* (1) lower the OLED refresh —
  it renders every 500 ms in `src/main.cpp`'s loop; the screen is debug-only, so 1 Hz (or
  render-on-change) is plenty and cuts the I2C/coex load; (2) capture a crash backtrace (serial /
  persisted `/log`) to tell a task-watchdog block (likely the OLED I2C) from a coex panic;
  (3) reduce contention — throttle BLE adv/notify, never let the OLED I2C block the loop, pause the
  OLED during OTA. Shell hardening; does not block the core proxy work. (Chip filed.) **Now has a
  detailed plan:** [`perf-coex-plan.md`](perf-coex-plan.md) — the measure→improve→iterate loop
  (PerfMonitor + `/stats`, Task-Watchdog + reset-reason to catch the hang, a `perf_soak.py` load
  harness, and a prioritised efficiency backlog), grounded in the framework's verified instrumentation
  APIs. Elevated now that the C3 OLED is the beta-tester board ([[esp32-c3-oled-beta-board]]).
- **FTMS layer ("fitness device data" + trainer erg control)** — the next BLE layer; both halves
  (a) FTMS **Indoor Bike Data (0x2AD2)** as a power source, and (b) **Set Target Power** (0x2AD9 op
  0x05) → the SB20's Control Point for erg ("actually control the trainer power", from the Python
  ride-director). **Real-data-first, so it's phased on a capture:** the SB20 is itself an FTMS machine
  but we have **zero captured FTMS payloads** → built `code/scripts/capture_ftms.py` (codec-free; logs
  raw Indoor Bike Data + a guarded Set-Target-Power erg recon) and added it to `NEXT-BIKE-SESSION.md`
  §5b. **The codec / `FtmsMeterClient` / `fake_trainer.py` / `ftms_erg.py` build is GATED on that ride
  capture** (golden vectors from real bytes, like CPS). Approved plan covers F1–F3; erg acceptance is
  the Session G Part C question the capture answers.
- **Zero stale `src_*`/`power_w` on meter disconnect (cosmetic)** — ✅ **DONE.** `ProxyCore::reset()`
  zeroes `lastSource_`/`lastOutput_`, and `main.cpp` calls it on the connected→disconnected edge
  (`static bool wasConnected; if (wasConnected && !nowConnected) proxy.reset();`), so `/status`
  stops showing stale `src_*`/`power_w` once the meter drops (`source` already flips to `searching`).
- **Distinct advertised identity for the live crank vs a mock/sibling board** — backlog. Two boards
  both advertise `Stages 62144`, so `crank_reader` must target by `--address`. Optional: vary
  `SPOOF_NAME` per board (or append a short id) so they're distinguishable by name on the bench.
- **Read the SB20 shifter buttons over BLE** — backlog (future, *after* the power-meter proxy;
  owner idea 2026-06-18). The SB20 has shifter buttons (virtual shifting); can we read presses over
  BLE? *Cheap near-term capture, addable to any bike session:* connect to the **SB20 itself** — it
  advertises as `Stages Bike 0105`, addr `E4:AA:5A:D6:0E:D4`, services FTMS `0x1826` + CSC `0x1816`
  (from the 2026-06-17 scan) — with `06_capture_ble.py`, **actuate each shifter button**, and watch
  for BLE notifications / a control- or HID-like characteristic that changes on a press. If a press
  emits a packet → decode it (a new capture-driven protocol doc) and expose/relay it. Button
  hardware context: PedalSmart.blog's shifter-button repair material (`06-prior-art-and-references.md`).
- **Single surviving right-crank proxy** — ✅ **the desk pieces BUILT (2026-06-23, PRs #81–#90):** the
  web `/setup` picker pins the surviving crank as the source, the single-sided ×2 doubles it, and the
  Crank-identity picker re-broadcasts as the master — the full crank-rescue flow, no rebuild. *Remaining:*
  the on-bike proof. Original note: promising use case (owner idea 2026-06-18, seen working
  by accident in bike-session 2). On the SB20 the LEFT crank is the master; if it fails the rider
  loses power entirely. The proxy can read the surviving RIGHT crank (`Stages 4963`,
  `e3:25:39:38:92:71`) and rebroadcast it as the spoofed master (`Stages 62144`), restoring full
  function on a single crank — tidier + more full-featured than the mechanical / re-pairing
  workarounds (cf. PedalSmart.blog's single-failed-crank post). Depends on the meter-source pinning
  item below; document as a first-class use case in `01-project-brief.md`. Single-sided caveat: a
  right-only crank usually doubles right-leg power to estimate total.
- **Meter-source pinning (choose which meter to READ)** — ✅ **BUILT (2026-06-23, PR #81/#84):**
  `BleMeterClient::setMatch()` pins the source by address from NVS/the `/setup` picker, so the relay is
  deterministic. Original note: `BleMeterClient` matched too broadly:
  with several meters in range it bounced between `ASSIOMA17039L` (`e6:20:90:8c:f3:fe`),
  `ASSIOMA22428R` (`cc:d2:a0:d6:5c:9d`), and `Stages 4963` (`e3:25:39:38:92:71`) in bike-session 2,
  so the relayed source was non-deterministic. Pin the source by address (or configured name) so the
  relay is deterministic and the single-right-crank use case can target `4963` on purpose. (Distinct
  from the spoof-side `--address` item above, which is about which crank we *impersonate*.)
- **Device discovery + pairing + identity, from the ESP32 web UI** — ✅ **BUILT (2026-06-23, PRs #82–#89).**
  The `/setup` UI does exactly this: BLE-scan → pick the source → choose the advertised crank identity
  (name + serial) → NVS. Source + identity are no longer `Config`-hardcoded (defaults still seed first
  boot). See `pre-beta-plan.md` Phase 1. Original note (owner, 2026-06-19; serves
  the meter-to-meter proxy and the SB20 spoof). Today the source meter and the advertised identity are
  **hardcoded in `Config`** (`METER_NAME_FILTER`, `SPOOF_NAME`). Generalise to a **UI flow**: BLE-scan
  nearby power meters → pick the one to READ → choose the advertised identity (a *spoof* like "Stages
  62144" for the SB20, or our **own product identity** for the meter-corrector use case — product name
  TBD) → persist to NVS. Turns the firmware from a dev-config build into a user-configurable product.
  Pieces: a firmware BLE scan, a `/ui` device picker, NVS storage. See `meter-to-meter-proxy.md`.
- **Ride Director uplift — steerable session engine** — ✅ **BUILT (Phases 1–6, desk-complete; owner,
  2026-06-20).** The phone Ride Director is now the rider's session interface, steered by an agent over
  an HTTP **control API** (read live state + mutate the plan + push messages in real time), on a
  **dynamic versioned plan** model with **power-zone / %FTP** workouts (Coggan 7-zone). Host-tested,
  no bike (PRs #38–#42). Reference: [`ride-director.md`](ride-director.md); brief:
  [`ride-director-uplift-plan.md`](ride-director-uplift-plan.md). Zone-workout library also built (PR #44:
  endurance / sweet-spot / over-unders / VO2). **Remaining (gated):** wire the exposed `erg_setpoint_w`
  → the FTMS *Set Target Power* write once Session 4 §C confirms the SB20 ergs off a third party
  (auto-set Power-Zone workouts); on-bike use.
- **Stages↔Assioma calibration-grid experiment** — ✅ **harness built (owner, 2026-06-21); ride pending
  (Session 4 §D).** The `calgrid` workout (power spine 40→110 % FTP @ 90 rpm + cadence rows + coast,
  %FTP, desk-verified) drives the rider; `ride_web.py --live --workout calgrid` does the paired ANT+
  capture → `09_fit_calibration.py --target stages --ref assioma` (verified on the QUICK-multi sample:
  stages ~9 % high, grid beats linear) + `08_analyze_grid.py` for cadence-dependence. Turnkey run sheet
  in [`sessions/session-04-…`](../../sessions/session-04-enhanced-offset-and-brake-levers.md) §D. Use the
  new Ride Director to run a **power calibration grid** between the two meters already on the SB20 (native
  Stages L crank + Favero Assioma). *Why (not just redundant):* (a) a full dress-rehearsal of the **Session-5
  meter-to-meter fit pipeline** (XCadey→Assioma) with a *known* delta to confirm the fit recovers it,
  de-risking the track-bike calibration; (b) quantifies the Stages-vs-Assioma delta and — the useful
  part — whether it's flat (→ `fit_scale_offset`) or **power/cadence-dependent** (→ `fit_grid`). *Already
  in hand:* `ride_web.py --live --stages-id … --assioma-id … --output` (paired ANT+ capture while driving
  a workout) → `09_fit_calibration.py --target stages --ref assioma` / `08_analyze_grid.py`. *New artifact
  needed:* a **grid workout** — a %FTP power spine held steady (~2 min/step so both meters settle: e.g.
  40/55/70/85/100/110% FTP @ 90 rpm) + **cadence rows** at anchor powers (70% @ {60,75,105} rpm, 90% @
  {60,105}) + a coast for the zero. *Bonus the uplift unlocks:* drive it as a **live agent-operator** via
  the control API — watch `Stages − Assioma` on `/api/control/state`, `extend` a step until settled, push
  Δ-feedback banners, skip once a point is clean. Runs on the **SB20 bike** (both meters on it) + the
  ANT+ stick → a bike session. Builds on the `RIDE-CARD` / power-grid note in the meter-to-meter section.
- **FTMS protocol implementation** — ✅ **BUILT (F1–F6, spec-built; owner, 2026-06-21).** The FTMS
  (`0x1826`) bike protocol: codec (Python `ftms.py` + firmware `Ftms.h`), the **erg-control client**
  wired to the Ride Director's `erg_setpoint_w` + the shifter-nudges-watts mapper, and the trainer-server
  role. **On-air server seam PASS** on real hardware (PRs #48–#53). Reference:
  [`ftms-protocol.md`](ftms-protocol.md); plan: [`ftms-implementation-plan.md`](ftms-implementation-plan.md).
  **Remaining (gated):** Session 4 §C (`capture_ftms.py --erg`) is the real-capture gate that validates
  the spec-built codec + answers does-the-SB20-erg-off-a-third-party; the client-seam on-air test is
  bench-deferred (ESP↔ESP or host WinRT server); wire `erg_setpoint_w` → the FTMS write into the runtime
  (measure C3 BLE coex when stacked on the CPS spoof + WiFi).
- **Power-topology Phase 2 + multi-transport observability** — ✅ **tooling built (owner, 2026-06-21).**
  The session-4 finding (`sb20-power-topology.md`): the SB20 erg reads ~1.3× below the Assioma
  (single-sided refuted) — mechanism open. The fix is a **simultaneous multi-device capture** (every
  meter + the SB20 FTMS, over BLE *and* ANT+, one clock) → reconcile. Built: **`capture_ble_multi.py`**
  (paired BLE) + the SQLite layer now surfaces BLE CPS power keyed by device + `--basis iso` cross-capture
  reconcile. **Run-sheet + the ANT+/WSL permission fix + the nRF-sniffer plan:
  [`traffic-observability.md`](traffic-observability.md).** Remaining (next ride): run Phase 2; order an
  **nRF52840 dongle** for passive app↔SB20 sniffing (the one thing connect-and-subscribe can't see).
- **Forward L/R power balance through the proxy (dual pedals → SB20 → Stages-app balance display)** —
  backlog (owner, 2026-06-22). The Stages app shows **left/right balance**, so the spoof should carry the
  **Assioma duals' real L/R split** rather than an implicit 50/50. **Most of the wiring already exists:** the
  spoof CPS measurement **flags `0x2F`** set **bit 0 (Pedal Power Balance Present)** + **bit 1 (reference)**,
  so the balance **byte is already on air** (UINT8, ½-% units) — today almost certainly a fixed placeholder.
  The work: **read the source balance** (the Assioma duals report it — BLE CPS `0x2A63` balance byte, or the
  ANT+ Bike-Power "Pedal Power Balance" field) in `BleMeterClient`/`AntPowerSource`, thread it through
  `ProxyCore`/`PowerReading` → `Cps.h` so the spoofed crank emits the **real** split, then confirm the
  SB20/Stages app renders it. **Ground it from the session-7 ride capture** — `RIDE-ant-ride-20260622`
  carries the real **Stages crank `62144`** *and* **Assioma `17039`**, both dual-sided, on ANT+ — so extract
  both balance fields → golden vectors (real-data-first). Pairs with the single-right-crank +
  meter-source-pinning items above.
  **NB — the ESP32 is BLE-only**, so the *source* read is over **Bluetooth**, not ANT+: read the Assioma's
  L/R from the **master/left pedal's CPS `0x2A63`** balance byte (the DUO reports combined power + balance
  there), **or** read **both pedals** over BLE (each advertises — `ASSIOMA…L` / `ASSIOMA…R`) and derive it.
  The ANT+ mention above is only for the *Python* proxy / for grounding the balance **value**; the firmware
  path is `BleMeterClient` only. Remaining grounding for the BLE read: a quick **Assioma BLE-CPS capture**
  (the ESP, or a BLE central) to confirm whether the balance comes from one pedal's `0x2A63` or needs both.

---

## 9. "Do this next" — the concrete recommendation

1. **At the desk, now:** build Phase 1A (§2) in the build order given. Start with the **page
   encoder round-trip gate** — it's pure software, needs only the committed capture, and it's the
   real-data-first foundation everything else stands on.
2. **In parallel at the desk:** pre-build the Phase 2 `AssiomaAntSource` (it's just RX).
3. **Next bike visit:** run the Lane 2 session (§3 / `NEXT-BIKE-SESSION.md`). If Phase 1A is done,
   Phase 1B (the keystone pairing proof) rides along.
4. **Decision point after the bike:** Session G Part C result decides whether Track C (ESP32/BLE)
   opens. Phase 1B result greenlights Phase 2.

---

## 10. SB20 spoof — make the app's "Zero Reset" actually zero the Assioma (session 8)

> **✅ FIRMWARE IMPLEMENTED 2026-06-25** (branch `feat/zero-reset-forward-to-assioma` → PR) — pure-core
> `CpResult.requestSourceZero` flag + `BleMeterClient::requestZeroOffset()` (writes Start Offset Compensation
> `0x0C` to the source's CP `0x2A66`) + the safe peripheral-hook → `loop()`-drain wiring + a host test;
> ESP32 build green. **Remaining: the on-air confirm** — does the Assioma actually zero when the Stages app
> calibrates? — a bike step that folds into the canonical reflash + a calibrate test. Design as-built below.

Session 8 closed the calibrate **handshake** — the Stages app's zero-reset now *completes* against the spoof
(`decisions.md` 2026-06-25) — by replaying the real crank's `0x10` reply (company-id **442** + the captured
mfg-data, which encodes a **static** `901/951`). That's **cosmetically complete but functionally a no-op**:
the button finishes, but nothing is actually calibrated.

**Owner intent (2026-06-25):** keep the static reply (we pass the Assioma's real power straight through, so
the displayed offset is moot, and out-of-normal values risk erroring the app). The worthwhile feature is to
make the app's **"Zero Reset" perform a real zero-offset on the Assioma**:
- When the SB20/Stages app writes Enhanced Offset Compensation `0x10` to our spoof's CP, **forward an
  offset-compensation/zero command to the real Assioma** over the existing BLE-central link
  (`BleMeterClient`) — the Assioma exposes a Cycling Power Control Point (`0x2A66`); issue its Start/Enhanced
  Offset Compensation (`0x0C`/`0x10`) and read back the result.
- Reply to the SB20 with the real-crank-shaped success (keep company-id 442 + a sane offset/mfg-data so the
  app doesn't error), ideally **gated on the Assioma's real zero completing**.
- **Real-data-first:** capture the Assioma's own `0x10`/`0x0C` reply first
  (`06_capture_ble.py --control-point … --name ASSIOMA…`) to ground the forwarded command + the success
  shape **before** building it.
- **Care:** the Assioma must be **unloaded/still** during the zero (cranks stationary), and any offset
  surfaced to the app must stay within the range the Stages app accepts.

---

## 11. SB20 spoof — bidirectional CRANK-LENGTH bridge (sessions 3/8/9; owner 2026-06-25)

The crank-spoof's calibrate/zero-reset **handshake** is closed (#136) and the zero-reset now forwards to the
Assioma (§10, #138) — **but crank length is still a gap** (do NOT call the spoof "done"). Today the spoof
answers the standard CP Request-Crank-Length (`0x05`) with a **static 172.5 mm** (the captured real-crank
value), and the Stages app's crank-length *set* never reaches us over standard CP (`0x04`) — so the app
shows **`--`** (session 3 "A2", re-confirmed sessions 8/9). **Owner's goal:** make the spoof a
**transparent, bidirectional crank-length bridge** between the Stages app and the real meter — like the
zero-reset pass-through, but BOTH ways:

- **App → meter:** accept the crank length the SB20/Stages app sets → **forward it to the source meter**
  (Assioma or other) so the real meter computes power with the correct crank length.
- **Meter → app:** **read the source meter's crank length** → present it to the Stages app (real value, not
  `--`, and not the static 172.5 mm).

**Why it's harder than the zero-reset (capture first — real-data-first):**
- The Stages app does NOT set crank length over standard CP `0x04` (no such write reached our ESP in
  sessions 8/9 → the `--`); it uses a **non-standard/proprietary path** — likely the Stages proprietary
  char **`d445fe02`** (the `fe02` token char). **Capture what the app writes when you change crank length**
  (against the real crank and/or our spoof) before building the intercept.
- The Assioma likely takes crank-length config over its **Nordic-UART service** (`6e400001…`, seen in
  `G-assioma17039-ble-zero-20260615`), NOT standard CP `0x04`. **Capture the Favero-app ↔ Assioma
  crank-length exchange** to ground the forward (and check whether other meters differ).
- So this is **two proprietary protocols to reverse, both directions** — bigger than the zero-reset (which
  rode standard CP). Stage it: (1) capture both sides; (2) do **meter→app read-back first** (lower risk —
  just answer the app's crank-length read with the source's real value); (3) then the **app→meter forward**.

**Pattern:** §10 (zero-reset) + §11 (crank length) are the same idea — the spoof as a **transparent config
bridge** between the Stages app and the real power meter. Other config (cadence source, etc.) fits the mold.

### Capture recipe — run this BEFORE building (real-data-first; folds into a bike session)

Three grounding captures; the app→device writes are **proprietary**, so the primary tool is the **nRF
sniffer** ([`nrf-sniffer.md`](nrf-sniffer.md) — `sniff_ble.py`, start it BEFORE the app connects), not a
connect-and-subscribe central. Stage the nRF rig (doctor.ps1 green) at the desk first.

1. **App → crank, crank-length SET (the `--` mystery).** nRF-sniff the **Stages app ↔ real Stages L crank**
   link, then in the app change crank length (e.g. 172.5 → 165 mm). Expect the write on the Stages
   proprietary char **`d445fe02`** (the `fe02` token), NOT standard CP `0x04`.
   ```bash
   python code/scripts/sniff_ble.py --scan-only --duration 12          # get the crank's adv MAC
   python code/scripts/sniff_ble.py --device <crankMAC> --duration 300 \
       --output code/findings/captures/SNIFF-stages-cranklen-$(date +%Y%m%d-%H%M).pcap
   # ...now change crank length in the Stages app; then analyze the pcap (Wireshark / pcap_sqlite).
   ```
   Cross-check: with the **spoof** paired, watch the ESP `/log` while changing length — confirms whether
   ANY write reaches us (sessions 8/9 saw none on standard CP → the proprietary path).
2. **Crank-length read-back (meter → app).** Connect to the real crank and ask what length it reports —
   grounds the lower-risk first stage (just answer the app's read with the source's real value):
   ```bash
   python code/scripts/06_capture_ble.py --address <crankMAC> --control-point request-crank-length \
       --output code/findings/captures/G-stagesL-cranklen-readback-$(date +%Y%m%d-%H%M).jsonl
   ```
3. **Favero app → Assioma, crank-length SET (the forward target).** nRF-sniff the **Favero app ↔ Assioma**
   link while changing crank length there — expect it over the Assioma's **Nordic-UART service**
   (`6e400001…`, seen in `G-assioma17039-ble-zero-20260615`), NOT standard CP `0x04`.

Commit the pcaps/JSONL to `findings/captures/`, note them in the session + `decisions.md`, then build the
bridge in the staged order above (read-back first, then the app→meter forward). **Don't write the codec
before these land** (capture-before-code).

---

## 12. Debug — why changing BOTH crank ids fails to pair (session 8) — ✅ MOSTLY RESOLVED (session 9, 2026-06-26)

**✅ Resolved (session 9):** the rule is **symmetric — the SB20 needs BOTH configured crank IDs findable on
air.** L=`62145`(present) + R=`4964`(absent) → fail (SB20 dropped our ESP, `disconnect reason=531`, never
re-attempted); L=`42146`(absent) + R=`4963`(present) → *same* fail; both present → connects. So it is **not**
"needs a right crank" (session 8's inference) — an absent L *or* R aborts the whole pairing. **Implication:**
sole-source (Assioma only) needs our ESP to answer *both* IDs → the 2nd-phantom-right path below. **Still
open:** app- vs bike-gatekeeper (which entity refuses) — needs an nRF sniff of the app↔bike link (nRF wasn't
available session 9). **Next-ride variant (owner):** known-good `62145`/`4963` but with the right crank's
**battery pulled** — does a *known* id that's offline behave like an *unknown* one? (isolates "unknown id"
from "id present-but-not-on-air").

**Prior art — PedalSmart.blog ("SB20: one of my power meter cranks have…", 2024-12; owner-shared 2026-06-27).**
A non-technical owner write-up (no IDs/protocol — understanding only, nothing to clean-room) that
corroborates two things and reframes the sole-source path:
- **Master-slave confirmed:** *"Right Crank measures power and sends it to the Left Crank. The Left Crank
  adds its measurements and sends a single message with consolidated L:R data."* The L crank is the
  aggregator — what our spoof impersonates.
- **The SB20 has a native single-crank mode:** the documented dead-crank fix is **pull the dead crank's
  battery** + **select "Stages Bike" as the power source**, after which *"the SB20 will automatically
  compensate … by **doubling** readings from the good power meter."* (Validates our `singleSidedDouble`
  ×2 — the bike really does double a lone crank.)
- **Testable hypothesis it implies (next ride):** session 9's "needs BOTH ids findable" was measured with
  the app set to **two** crank ids. The blog's path is a **single-crank configuration**. So the "both
  findable" rule may be **specific to dual-crank config** — in single-crank mode the SB20 may need only
  **one** findable crank. If true, **crank-rescue gets simpler**: configure the bike for single-crank
  (one battery out + bike-as-source), have our spoof be that one surviving/master crank, and rely on the
  bike's native doubling — instead of the 2nd-phantom-right path. **Add to the next bike session:** set the
  app to single-crank and confirm one findable spoof crank pairs (vs the dual-config two-findable rule).

**Original question (session 8):** setting the Stages app to **L=`62145` (our ESP) + R=`4964` (a phantom, nothing
on air)** → the app reported **"pairing failed"** and the SB20 **never connected to our ESP** (`/log` saw
nothing). Changing only **R→`4963`** (the real right crank, present) → connected immediately. So an **absent
right id aborted the whole pairing** — the bike didn't even attempt the present left. We *inferred* "the bike
needs a findable right crank" but never proved the **mechanism**.

**Why it matters:** it gates whether a true **sole-source** setup (Assioma only, no real right crank) is
possible — and the shape of any workaround. (Pairs with the single-right-crank / sole-source backlog.)

**Hypotheses (session-8 data can't distinguish them — both produce "our ESP sees nothing"):**
- **App-gatekeeper:** the Stages app scans for both ids, can't find `4964`, declares "pairing failed", and
  never pushes the config to the bike → the bike never tries either.
- **Bike-gatekeeper:** the app pushes both ids; the SB20 scans, can't find R, and aborts before connecting L
  (i.e. it requires *both* configured cranks findable).

**Debug plan (cheap — rides along with session 9; we're pairing the SB20→ESP there anyway):**
1. Variants, watching `/log` on the ESP for *any* connect attempt:
   - L=`62145` / R=`4964` (absent) → reproduce; does the ESP see a connect attempt at all?
   - L=`62145` / R=`4963` (present) → connects (control).
   - L=`<absent id>` / R=`4963` (present) → does the bike connect to the present R alone, or also abort?
     (tests "both must be findable" vs "only the right matters").
2. If still ambiguous, an **nRF sniff** of the app↔bike link (sniff BEFORE the pairing attempt — see
   §Lessons in PLAYBOOK) shows whether the bike ever *receives* the phantom config → app- vs bike-gatekeeper.

**If bike-gatekeeper (needs both findable):** a sole-source workaround to test is the **ESP advertising a
SECOND, phantom-right peripheral** so both configured ids resolve to us. **If app-gatekeeper:** explore a
different app pairing flow. Either way: capture-first, then decide.

## 13. MCP workout builder + driver — ✅ DESK-COMPLETE (2026-06-26); on-bike drive pending

> **✅ BUILT 2026-06-26** (PRs #165 + the MCP-server PR) — see
> [`mcp-workout-server.md`](mcp-workout-server.md). The desk core + driver + FastMCP server are
> done and host-tested (no bike): `sb20proxy.workout` (spec builder + `WorkoutSession`),
> `sb20proxy.mcp.driver.ErgDriver` (async drive loop with **Reset-on-teardown** safety), and
> `sb20proxy.mcp.server` (tools: build_workout / list_workouts / start / stop / skip / goto /
> extend / set_target / message / set_profile / status / start_drive / stop_drive / drive_status),
> run via `scripts/mcp_workout_server.py` (the `[mcp]` extra). **Remaining:** the on-bike
> MCP-driven ride; a persisted named-workout library + adapt presets; observability/log surfacing;
> a true pause/resume (needs a `LiveState` clock-freeze). Original idea + shape below.

**Idea:** an **MCP server** that exposes the SB20 erg as agent-drivable tools — Claude (or any MCP client)
can **compose a structured workout** from a spec / natural language ("6×90 s @ 430 W, 3 min recovery") and
**drive it live** on the bike over FTMS, with live status + a safe stop. The productized form of the
session-9 ad-hoc driver.

**Why:** today, driving an erg workout is a hand-run script; as an MCP it becomes a first-class capability the
day-driver can invoke on command — compose on the fly, adapt mid-effort (shorten/extend, bump targets), and
log it. This is the concrete **"agent directs the human"** inversion-of-control pattern (see the ride-wizard /
blog-idea note): the assistant runs the session, the rider just pedals.

**Shape (build on the validated FTMS infra — `ble/ftms_erg.py`, `ble/ftms.py`, `ride_control.py`, `ride/`):**
- Tools: `build_workout(spec) -> plan` (segments: warmup / intervals / recovery / ramps / cooldown, by watts
  or %FTP / zone — `test_ride_zones.py` already has zones), `drive_workout(plan)` + `start` / `pause` /
  `stop`, `set_target(w)` (manual override), `workout_status()` (segment, target, elapsed, live power/cadence).
- **Safety is non-negotiable:** every stop / disconnect / error path resets erg to neutral (never leave the
  rider grinding at target) — the same try/finally rule as the seed script.
- Surface segment transitions + live status to observability (`sb20proxy.obs`) and commit a workout-log capture.
- A workout **library** (named sessions) + adapt-on-the-fly ("make the next recovery 8 min easy").

**Seed:** `code/scripts/ftms_workout.py` (built session 9) is the prototype to refactor behind the MCP server.
Real-data-first: the FTMS erg path is already byte-validated (session 4; `G-sb20-ftms-erg-*.jsonl`).

## 14. On-device workout engine — structured workout over a route, deterministic execution (owner 2026-06-27)

> **Design captured:** [`on-device-workout-engine.md`](on-device-workout-engine.md). PLANNED — no code yet.

**Idea:** set a structured workout **on the device** via a `/workout` route, persist it, and have the
**device itself execute it deterministically** (own the clock + the erg setpoint, drive the SB20 over
FTMS). This is the on-device counterpart to §13's desk/MCP driver, and it's what backs the LCD
workout/erg screen (`design/sb20-lcd-workout-v1.html`).

**Format decision (the owner's "FIT but binary → JSON" question):** the **on-device canonical format
is compact JSON, 1:1 with the existing Python `Segment`** (`ride/director.py`) — ArduinoJson parses it,
the LCD profile chart renders from it, and it earns **golden-vector parity tests against the Python
director**. **FIT/ZWO are desk-side import formats** (convert with `fitparse` / a ZWO-XML reader in
`workout/` → emit the JSON → POST). Do NOT parse FIT on the MCU.

**Reuse — do NOT rebuild:** `ride/director.py` (`Segment`/`Workout`/`RidePlan`/`Cursor`/`DirectorState`
— the reference executor to port), `ride/workouts.py` (Coggan zones), `Ftms.h` + the erg client (the
target write). The firmware executor is a **pure port** behind the radio seam (host-tested, no board).

**Phases (each = a PR; host-tested in-commit):**
1. **Desk importers** — `workout/from_zwo()` + `from_fit()` → canonical JSON, against committed sample
   `.zwo`/`.fit` fixtures (real-data-first); a small exporter from the existing `Workout`.
2. **Firmware executor** — `firmware/lib/proxy/WorkoutEngine.h` (pure port of `DirectorState`, no
   radio); golden-vector parity tests vs the Python director.
3. **Routes + persistence** — `WifiLink` `/workout` GET/POST + `{start,pause,skip,stop}`; persist to
   flash; pure render/parse host-tested like `ConfigPage.h`.
4. **Wire to FTMS** — per-segment target → Set-Target-Power; bench-gated (coex on the C3 when stacked
   on the CPS spoof + WiFi), then the on-bike drive. **Safety:** every stop/disconnect/error resets erg
   to neutral (the §13 try/finally rule).
