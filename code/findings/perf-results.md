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

> Note: the **baseline** must be captured on the Phase A firmware (it serves `/stats`). At RSSI −81
> the OTA was unreliable — run the iterate loop with the board **near the access point**.
