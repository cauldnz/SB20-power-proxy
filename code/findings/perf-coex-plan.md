# Plan — on-device load monitoring + the measure→improve→iterate loop (ESP32-C3)

> **Status:** planning doc (no code yet). Owner asked for this to be planned intensively after the
> ESP32-C3 was confirmed as the **beta-tester board** and after repeated coex/flashing pain.
> Scope: how to *see* what the C3 is doing under load, then a disciplined loop to make it
> faster/steadier on the real device. Companion to `forward-plan.md` §8 (perf/coex backlog bullet).

## TL;DR

We're flying blind. The C3 freezes intermittently under combined WiFi + dual-role BLE + OLED load
and we only learn about it when the boot-guard reboots it — with no record of *why*. The fix is, in
order: **(A) observe** (a `PerfMonitor` + `/stats` endpoint: loop timing, heap, reset reason),
**(B) catch the hang** (Task Watchdog + reset-reason + core-dump → the next freeze is diagnosable,
not blind), **(C) a repeatable load harness** (`perf_soak.py` so two firmwares are comparable),
then **(D) iterate** the efficiency backlog one reversible change at a time, each kept-or-reverted
on measured evidence. Everything pure-and-host-tested where it can be; the radio/timers are the
seam — same discipline as the rest of the firmware.

---

## 1. The problem (grounded)

The ESP32-C3 is a **single-core RISC-V** running, concurrently, on **one radio**:
- **WiFi** station (HTTP server `handleClient()`, ArduinoOTA, the `/ui` poller),
- **dual-role NimBLE** — central (continuous active scan → connect → subscribe to the meter) **and**
  peripheral (advertise "Stages 62144" → accept the SB20 → notify power+cadence ~1 Hz, answer the
  control point),
- the **0.42" OLED** over I²C @ 50 kHz, redrawn every 500 ms,
- the Arduino `loop()` doing all the above cooperatively (`proxy.loop()` → `wifi.handle()` → LED →
  OLED) with a `delay(5)`.

Observed (README + forward-plan §8): under load the LED + screen **freeze**, the boot-guard fires,
it reboots and reconnects; **heap stays flat**, so it's a **coex/blocking stall, not a leak**. A
real erg ride is the sustained worst case (central connected + peripheral connected to the SB20 +
WiFi observability + OLED, for many minutes). For a **beta-tester** board this must be boringly
reliable — and right now we can't even say *which* subsystem stalls.

**Coex reality on the C3:** WiFi and BLE time-share a single 2.4 GHz radio via software coexistence.
Under heavy WiFi (an OTA, a big HTTP poll) BLE gets starved → notify gaps / disconnects; under heavy
BLE (scan duty cycle) WiFi latency spikes. We can't fully observe coex from the API (see §11), so we
**infer** it from correlated symptoms — which is exactly why we need good loop/link instrumentation.

---

## 2. The iteration loop (the methodology — this is the core ask)

A tight, evidence-driven loop, each turn small and reversible:

```
        ┌─────────────────────────────────────────────────────────────┐
        │  1. HYPOTHESISE   "OLED redraw every 500ms is a stall source" │
        │  2. INSTRUMENT    ensure /stats exposes the metric that proves│
        │                   or kills the hypothesis (loop p95 / stalls) │
        │  3. IMPLEMENT     one small, reversible change (a build flag   │
        │                   or a few lines), tests stay green           │
        │  4. DEPLOY        OTA to the board (near the AP — weak signal  │
        │                   OTA is unreliable; USB when stable)         │
        │  5. LOAD-TEST     run the repeatable soak scenario (§8)        │
        │  6. MEASURE       capture /stats deltas → a row in            │
        │                   perf-results.md (commit, change, numbers)   │
        │  7. DECIDE        better on the metric + tests green → keep;   │
        │                   else git-revert. Record the verdict either  │
        │                   way (a refuted hypothesis is a result).     │
        └───────────────────────────── loop ───────────────────────────┘
```

Two preconditions make the loop *work* (steps 5–6 are worthless without them):
- **A stable metric set** you trust to mean "better/worse" (§4–§5).
- **A reproducible load scenario** so run-to-run noise doesn't swamp the change (§8).

Guardrails: a perf change **must not** regress the functional suite (`pytest` + `pio test -e native`
+ the on-air smoke) — never trade correctness for a millisecond. Keep changes one-at-a-time so a
result is attributable.

---

## 3. What to measure (metrics taxonomy)

Confirmed available in the installed framework (`framework-arduinoespressif32`, espressif32
~6.7.0 / IDF 5.1) unless noted — verified by grep, 2026-06-17.

### 3a. Loop health — the #1 stall signal
- **Loop period** µs (p50 / p95 / max) and **stall count** (iterations whose period exceeds a
  threshold, e.g. > 50 ms and > 200 ms buckets). A cooperative `loop()` that stalls = something
  blocked (I²C, a synchronous WiFi op, a BLE callback). Source: `esp_timer_get_time()` (µs) at the
  top of each `loop()`; keep a rolling histogram + max + over-threshold counters.
- **Time-in-section** (optional, deeper): wrap `proxy.loop()`, `wifi.handle()`, `oled.render()` and
  attribute the worst offender.

### 3b. Memory
- `ESP.getFreeHeap()` (have it), **min-ever** `esp_get_minimum_free_heap_size()`, **largest free
  block** `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` → **fragmentation %** =
  `1 − largest/free`. Flat free-heap but shrinking largest-block = fragmentation creep.

### 3c. Tasks / stacks
- Per-task **stack high-water mark** `uxTaskGetStackHighWaterMark(task)` for the loop task, the
  NimBLE host task, the WiFi/`tiT` task. A near-zero margin = latent stack-overflow crash.

### 3d. CPU headroom
- **Idle counter** `ulTaskGetIdleRunTimeCounter()` (sampled deltas) → idle % = headroom on the
  single core. Or full `vTaskGetRunTimeStats()` for per-task CPU %. ✅ `configGENERATE_RUN_TIME_STATS`
  and `configUSE_TRACE_FACILITY` are **both = 1** in this framework's FreeRTOSConfig (verified
  2026-06-17) — so per-task CPU % works with **no extra sdkconfig**.

### 3e. The hang detector (highest value)
- **Task Watchdog (TWDT):** register the loop task with `esp_task_wdt_add()` and feed it each loop.
  On a stall it panics with a **backtrace naming the starved task** — turning today's blind
  boot-guard reboot into "the NimBLE host task starved for 5 s." (Note: `esp_task_wdt_reconfigure`
  is **absent** in this framework → use the `esp_task_wdt_init()`/`esp_task_wdt_add()` API.)
- **Reset reason** `esp_reset_reason()` on boot → distinguishes power-on / brownout / TWDT /
  panic / SW-reset (our boot-guard). Today every reboot looks the same; this says *why*.
- **Core dump to flash** `esp_core_dump_*` (partition + `build_flags`): on panic, persist a
  backtrace, read it back via `/stats` or `esptool coredump`. Critical because the C3's USB-serial
  is too flaky to catch a live panic.

### 3f. Link quality / coex symptoms
- WiFi **RSSI** (have it; now on the OLED). BLE counters: **notifies sent** (peripheral),
  **notifies received** (central), **disconnects**, **scan restarts**, **reconnects**. A coex
  problem shows as BLE drop/notify-gap rate rising with WiFi activity (an OTA, a `/ui` poll burst).

### 3g. Persistence across reboot (so evidence survives the freeze)
- Persist `last_reset_reason`, `min_free_heap_ever`, `max_loop_stall_ms`, `reboot_count` in **NVS**
  (like `WifiCreds`). A reboot must not erase the proof of what went wrong.

---

## 4. How to collect it — a pure, host-tested `PerfMonitor`

Mirror the project's seam discipline (pure logic host-tested; hardware behind a seam):

```cpp
// lib/proxy/PerfMonitor.h  — PURE, host-tested with a fake clock (no esp_timer, no FreeRTOS).
struct LoopStats { uint32_t count, stalls50, stalls200; uint32_t p50us, p95us, maxus; };
class PerfMonitor {
public:
    void sample(uint64_t now_us);        // call once per loop with the timestamp
    LoopStats loop() const;              // rolling window summary
    // heap/stack/idle are injected as plain numbers by the caller (the seam reads the real APIs)
};
```
- The histogram / percentile / threshold logic is unit-tested with synthetic timestamps (golden
  vectors), exactly like `Status.h`/`OledScreen.h`. `main.cpp` feeds it `esp_timer_get_time()` and
  the heap/stack/idle reads — those are the seam, exercised only on the bench.
- `renderPerfJson(const PerfSnapshot&)` — pure, host-tested — produces the `/stats` payload (the
  same pattern as `renderStatusJson`).

## 5. How to expose it — `GET /stats`

- Add a route in `WifiLink::startStationServer_()` next to `/` and `/log`, served from a
  `PerfProvider` lambda (like the existing `StatusProvider`). Keep the JSON small (coex-friendly).
- `/ui` gains a "Perf" panel (or a sibling `/perf` page) that polls `/stats` — all rendering on the
  phone, same as the existing dashboard.
- Surface the **persisted** fields (last reset reason, min-heap-ever, max-stall, reboot count) so a
  reboot's cause is visible in the browser without serial.

---

## 6. Catching the hang (the diagnostic upgrade) — do this EARLY

Today: freeze → boot-guard → reboot → no record. Target: freeze → **named backtrace + reset reason
persisted** → next page-load tells us which task starved and where. Concretely:
1. `esp_reset_reason()` on boot → persist + show on `/stats`.
2. TWDT on the loop task (feed each loop) → a stall produces a backtrace instead of a silent hang.
3. Core-dump-to-flash on panic → readable backtrace after the fact.
This is the single highest-leverage step: it converts "it froze again, dunno why" into data.

---

## 7. (covered in §3 — kept numbering aligned with the rollout in §12)

---

## 8. The repeatable load harness — `scripts/perf_soak.py`

Measurement is meaningless unless two runs are comparable. Define a **fixed worst-case scenario**
reproducible at the desk (no bike), and a one-command driver:

- **Scenario (the "soak"):** simultaneously —
  - `fake_meter.py` (central load: the ESP32 is connected + receiving notifies; `--balance --hz N`
    to dial stress),
  - `crank_reader.py` (peripheral load: a central connected + reading our notifies),
  - WiFi active with `/stats` polled every *N* s **and** a periodic bulk GET (an OTA-sized transfer
    stand-in) to stress coex,
  - OLED rendering,
  - for a fixed **duration** (e.g. 10 min).
- **Driver** `scripts/perf_soak.py`: orchestrates the above, samples `/stats` on an interval, logs
  every sample to **JSONL** (canonical, per project discipline), and prints a summary: loop
  p50/p95/max, stall counts, min-heap, fragmentation, idle %, BLE drops, and **any reboot**
  (reset-reason). Output → `findings/perf/soak-<ts>.jsonl` + a one-line verdict.
- **Stress knobs** to amplify/shorten: `fake_meter --hz` high, OLED at full rate, concurrent `/ui`
  pollers. A short high-stress run that reliably reproduces a stall is worth more than a calm hour.

This gives steps 5–6 of the loop a single command and a comparable artifact.

## 9. The hypothesis backlog (efficiency levers — prioritised, each testable)

Ordered by expected leverage ÷ risk. Each is one reversible change → soak → keep/revert.

1. **OLED cost.** Redraw on-change or at 1 Hz (not 500 ms); time-budget the I²C (it's bounded by
   `Wire.setTimeOut(50)`, but frequent 50 ms stalls add up); consider moving the panel to a
   low-priority task or a non-blocking state machine; **pause OLED during OTA**. *(Cheapest, likely
   real — the I²C bus is the prime suspect for the loop stall + freeze.)*
2. **BLE scan duty cycle.** Continuous active scan (`scan->start(0)`) is coex-heavy; on the *station*
   path, use a windowed scan (interval/window) and back off once connected. We already stop on
   match, but the post-disconnect rescan is continuous.
3. **BLE connection parameters.** Longer connection interval + larger MTU on both links = fewer
   radio events contending with WiFi. Advertising interval similar.
4. **WiFi power-save mode.** `WiFi.setSleep()` / `esp_wifi_set_ps(WIFI_PS_MIN_MODEM)` vs `NONE` —
   modem-sleep changes coex sharing; measure latency vs stall trade-off both ways.
5. **Drop blocking work from `loop()`.** Audit for `delay()`, synchronous scans (the portal's
   sync `scanNetworks()` is fine in portal but must never run on the station path), and the
   `Serial.printf` mock-ramp logging (disable in production builds).
6. **Task priorities.** NimBLE host task vs Arduino loop task vs WiFi — nudge priorities/affinity
   (single core, so it's priority + duty-cycle, not pinning).
7. **Coex preference bias.** `esp_coex_preference_set(ESP_COEX_PREFER_BT|BALANCE|WIFI)` exists
   (verified). During a ride the BLE links are the product and WiFi is just observability, so
   biasing toward BT may cut notify-gaps — test `PREFER_BT` vs `BALANCE` under soak.
8. **HTTP payload size.** `/`, `/ui`, `/stats` already small; keep them so — a fat poll contends
   with BLE.

## 10. Comparing results — `findings/perf-results.md` (append-only)

A table, one row per experiment: date · commit · the change · soak config · loop p95 / max ·
stalls · min-heap · frag% · idle% · BLE drops · reboots · **verdict (keep/revert + why)**. Like
`decisions.md`, it's append-only — refuted hypotheses stay on the record so we don't re-try them.

## 11. Risks / API caveats / unknowns

- **`esp_task_wdt_reconfigure` is absent** here → use `esp_task_wdt_init()`/`esp_task_wdt_add()`.
  (Run-time stats are confirmed enabled, §3d — not a risk after all.)
- **Coex tuning is a *bias*, not a meter.** `esp_coex_preference_set()` exists (a WiFi/BT
  preference knob — §9.7), but there's no rich runtime coex telemetry exposed through Arduino, so we
  still **infer** contention from correlated BLE-drop / loop-stall symptoms. Don't promise a "coex %".
- **Core-dump + custom partitions** interact with our existing `min_spiffs.csv` (two OTA slots) —
  fitting a coredump partition needs a partition-table review (don't disturb the NVS-survives-OTA
  invariant from `decisions.md` 2026-06-16).
- **Observer effect:** instrumentation must be cheap (a few µs/loop, no heap churn, no blocking) or
  it becomes the stall it's measuring.
- **Weak-signal OTA** (just bit us, RSSI −77) makes the deploy step flaky — run perf iteration with
  the board **near the AP**, or USB when the JTAG is stable; the on-OLED RSSI helps here.

## 12. Phased rollout (first concrete steps)

- **Phase A — Observe.** `PerfMonitor` + `renderPerfJson` + `/stats` (loop timing, heap/frag,
  stack HWM, idle%), `esp_reset_reason()` persisted to NVS, `/ui` perf panel. Host-test the pure
  parts. *Outcome: we can finally see load + know why it last rebooted.*
- **Phase B — Catch the hang.** TWDT on the loop task + core-dump-to-flash + persisted last-stall.
  *Outcome: the next freeze produces a named backtrace, not a blind reboot.*
- **Phase C — Harness.** `scripts/perf_soak.py` + `findings/perf-results.md` baseline row.
  *Outcome: a one-command, comparable load test.*
- **Phase D — Iterate.** Work §9 top-down: OLED rate → scan duty cycle → conn params → WiFi PS,
  each a soak + a results row + keep/revert. *Outcome: measured, defensible reliability gains.*

**Definition of done (for the beta board):** a 30-min soak at realistic erg-ride load with **zero
reboots**, loop p95 under a few ms, stable min-heap, and BLE notify-gap rate within target — all
visible on `/stats` and reproducible via `perf_soak.py`.

## 13. Open design questions for the next planning pass

- Do we want a dedicated FreeRTOS task for the OLED (decouple I²C from the hot loop) vs a
  time-budgeted state machine in `loop()`? (Task = cleaner isolation; costs a stack + a sync point.)
- Is per-task CPU % worth a custom sdkconfig, or is idle% + loop-stall enough signal?
- Should `perf_soak.py` drive the **real SB20** (not just `crank_reader`) for a true-load number,
  gated on Session G Part C? The desk soak is the fast loop; the bike is the acceptance test.
