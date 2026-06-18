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

> Note: the **baseline** must be captured on the Phase A firmware (it serves `/stats`). At RSSI −81
> the OTA was unreliable — run the iterate loop with the board **near the access point**.
