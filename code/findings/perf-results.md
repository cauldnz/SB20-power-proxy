# Perf results — measured perf/coex iterations (append-only)

One row per experiment from the iterate loop in [`perf-coex-plan.md`](perf-coex-plan.md). Each is a
`perf_soak.py` run on a fixed scenario; compare a change against the row above it. Append, never
edit — a refuted hypothesis stays on the record.

How a row is produced:
```
python scripts/perf_soak.py --host sb20proxy.local --duration 600 --load \
    --crank-address <board-BLE-addr> --ui-poll --label "<what changed>"
```
Metrics (from `/stats`, cumulative over the soak window after `/stats/reset`):
- **loop p95 / max (µs)** — loop-period tail; the stall signal.
- **stalls 50ms / 200ms** — count of loop iterations over those thresholds (the freeze precursors).
- **min heap / max frag%** — memory health.
- **loops/sec** — CPU-headroom proxy (loop_count ÷ window; fewer under load = less headroom). Idle%
  isn't available (`ulTaskGetIdleRunTimeCounter` doesn't link in this Arduino FreeRTOS build).
- **reboots** — any reset during the soak + its reason (`reset_reason` / `sw_reason`).

| date | commit | change / scenario | loop p95 | loop max | stalls 50/200ms | loops/s | min heap | frag% | reboots | verdict |
|------|--------|-------------------|----------|----------|-----------------|---------|----------|-------|---------|---------|
| 2026-06-17 | Phase A | **baseline** — live OLED fw; load = central (fake_meter --balance 2Hz) + peripheral (crank_reader) + /ui poll; 80 s | 10ms | 96ms | 161 / 0 | 139 | 131672 | 20 | none | baseline. **~2.0 stalls/s >50ms ≈ the 500 ms OLED redraw rate** → OLED render is the prime suspect (soak-1781759959.jsonl) |
| 2026-06-17 | Phase D | **OLED redraw 500 ms → 1000 ms (1 Hz)** — same load/duration | 10ms | 94ms | 85 / 0 | 152 | 132176 | 18 | none | ✅ **KEEP**. Stalls halved 161→85 (2.0→1.06/s), loops/s 139→152 — **confirms the OLED I²C render is the loop-stall source**. Next: render-on-change (near-zero when idle) + attack the ~94 ms per-render cost itself (soak-1781760259.jsonl) |
| 2026-06-17 | 563c60b | **OLED render → dedicated task** (off the hot loop) + render-on-change | 10ms | 12ms | 0 / 0 | 166 | 127000 | 18 | none | ✅✅ **KEEP (big win)**. loop_max **96→12 ms (8×)**, stalls_50ms **161→0 (eliminated)**, loops/s 139→166, clean boot. The 94 ms I²C render now blocks the OLED task (which yields), not the loop — **this fixes the freeze root cause**. (soak-1781772692.jsonl) |
| 2026-06-17 | 563c60b | **stability** — 5-min loaded soak (same load) | 10ms | 12ms | 0 / 0 | 166 | 118984 | 25 | none | ✅ **ride-ready**: stall-free for 5 min, **heap stable ~122 k (no leak** — settles after BLE connect, then flat), no reboots. Only marginal note: frag occasionally 20–25% (largest block ~89 k, fine). A multi-hour soak would fully confirm long-ride heap, but the steady-state slope is ~zero. (soak-1781774761.jsonl) |
| 2026-09-23 | f6a711b | **observe only** — no BLE load, no `/ui` poll; 90 s | 10ms | 13.0ms | 0 / 0 | 166 | 115032 | 19 | none | ✅ **the idle path still holds 3 months on** — matches the 2026-06-17 dedicated-OLED-task row almost exactly (12 ms / 0 / 166). The freeze fix has not regressed. (soak-1790130089.jsonl) |
| 2026-09-23 | f6a711b | **BLE load only** — central (fake_meter) + peripheral (crank_reader), **no** `/ui` poll; 120 s | 10ms | 13.4ms | 0 / 0 | 166 | 114896 | 22 | none | ✅ **dual-role BLE is stall-free.** The hard part — one radio running central *and* peripheral — costs nothing measurable over idle. (soak-1790130193.jsonl) |
| 2026-09-23 | f6a711b | **`/ui` poll only** — HTTP load, **no** BLE load; 120 s | 10ms | **227ms** | **9 / 1** | 164 | 114848 | 29 | none | ⚠ **the web server is the ONLY loop-stall source**, isolated by elimination against the two rows above. Serving `/ui` (6.8 KB) takes ~345 ms wall-clock, `/app` (64.5 KB SPA) ~1.7 s — the Arduino `WebServer` sends synchronously from `loop()`. **Read the note below before acting on this.** (soak-1790130334.jsonl) |
| 2026-09-23 | f6a711b | **⭐ 30-min definition-of-done soak** — full load: central + peripheral + `/ui` poll; 1800 s | 10ms | 395ms | 211 / 5 | 164 | 113780 | 29 | **none** | ✅ **MEETS the §12 definition of done** — the first 30-min soak ever run (previous longest: 5 min). **Zero reboots**, heap flat at ~114 k for the full half hour (no leak), 164 loops/s. All 211 stalls are the web server (see the row above); the BLE stream is unaffected (note below). (soak-1790130777.jsonl) |

> Note: the **baseline** must be captured on the Phase A firmware (it serves `/stats`). At RSSI −81
> the OTA was unreliable — run the iterate loop with the board **near the access point**.

### 2026-09-23 — the stall count overstates the ride risk (measure the stream, not the loop)

The `/ui`-poll row looks alarming: 211 stalls >50 ms and a 395 ms loop max over the 30-min soak. It
is **not** a ride problem, and the loop-timing metric alone cannot tell you that — so this is
recorded to stop a future reader chasing it.

Measured directly at the BLE layer, subscribing to the board's CPS measurement characteristic the
way the SB20 does, and timestamping every notification:

| | notifications | median gap | p90 | max |
|---|---|---|---|---|
| quiet | 30 | 1.001 s | 1.101 s | 1.198 s |
| while serving 24 × `/app` (64.5 KB each) | 29 | 1.001 s | 1.100 s | **1.102 s** |

**Indistinguishable.** NimBLE notifications are dispatched from the BLE host task, not the Arduino
`loop()`, so a blocked loop does not delay the crank stream. The staleness watchdog's 6 s threshold
also sits far above the worst 395 ms block.

An earlier attempt to measure this by polling `/status` every 0.4 s suggested the opposite
("longest stall 1.14 s → 2.16 s"). That was an artifact of sampling a 1 Hz stream at 0.4 s
resolution — the instrument, not the board. **Measure the thing you care about (notification
spacing), not a proxy for it (a counter you poll).**

Consequence: **no firmware change is warranted for the web-server stalls.** A fix (chunking the
response, yielding mid-send) would add risk to the hot path for no measured benefit. Revisit only
if something that *does* run on `loop()` grows a latency requirement tighter than ~400 ms.

### 2026-09-23 — the harness could fail silently, and did

`fake_meter` printed `advertising=False` once at startup and then streamed `tx 180 W ...` forever
at nobody: a second (stale) instance held the WinRT peripheral role, so this one's publisher stayed
permanently `ABORTED`. The board saw no meter and the debugging went to the firmware. The catch is
that a *momentary* ABORTED on start is normal (decisions.md 2026-06-22) — the two are only
distinguishable by waiting. `fake_meter` now waits for the publisher to settle and **exits with an
actionable message** naming the likely cause. Guarded by `code/tests/test_fake_meter_guard.py`.

