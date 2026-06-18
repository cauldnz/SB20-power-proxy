#!/usr/bin/env python3
"""perf_soak.py — load-soak the ESP32 proxy and record GET /stats over time.

The "measure" step of the perf iteration loop (findings/perf-coex-plan.md). Optionally drives the
BLE load (fake_meter as the central's source; crank_reader as a peripheral subscriber) and a
periodic HTTP poll (WiFi/coex load), samples /stats every --interval s for --duration s, logs each
sample to JSONL, and prints a summary. Compare two firmwares by running the SAME soak and diffing
the summaries; paste the verdict into findings/perf-results.md.

    # observe only (no BLE load) — quickest sanity:
    python scripts/perf_soak.py --host sb20proxy.local --duration 120

    # worst-case: central load (fake_meter) + peripheral load (crank_reader) + WiFi poll:
    python scripts/perf_soak.py --host sb20proxy.local --duration 600 --load \
        --crank-address 38:44:BE:45:E9:A6 --ui-poll --label "baseline"

Needs the PerfMonitor firmware (Phase A: GET /stats). Ctrl-C stops early (data kept).
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path


def _get(url: str, timeout: float = 4.0) -> str:
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.read().decode()


def _get_json(url: str, timeout: float = 4.0) -> dict:
    return json.loads(_get(url, timeout))


def main() -> None:
    ap = argparse.ArgumentParser(description="Load-soak the ESP32 and record /stats.")
    ap.add_argument("--host", default="sb20proxy.local")
    ap.add_argument("--duration", type=float, default=300.0, help="soak seconds")
    ap.add_argument("--interval", type=float, default=5.0, help="/stats poll seconds")
    ap.add_argument("--load", action="store_true", help="drive fake_meter (BLE central-side load)")
    ap.add_argument("--meter-watts", type=int, default=180)
    ap.add_argument("--meter-hz", type=float, default=2.0, help="meter notify rate (stress knob)")
    ap.add_argument("--crank-address", help="run crank_reader vs this addr (peripheral load)")
    ap.add_argument("--ui-poll", action="store_true", help="GET /ui each interval (WiFi/coex load)")
    ap.add_argument("--output", help="JSONL path (default findings/perf/soak-<ts>.jsonl)")
    ap.add_argument("--label", default="", help="freeform label recorded in the JSONL header")
    args = ap.parse_args()

    base = f"http://{args.host}"
    try:
        baseline = _get_json(base + "/stats")
    except Exception as e:  # noqa: BLE001
        print(f"cannot reach {base}/stats (need the Phase A firmware): {e}", file=sys.stderr)
        sys.exit(1)

    perf_dir = Path(__file__).resolve().parents[1] / "findings" / "perf"
    out = Path(args.output) if args.output else perf_dir / f"soak-{int(time.time())}.jsonl"
    out.parent.mkdir(parents=True, exist_ok=True)

    procs: list[subprocess.Popen] = []
    scripts = Path(__file__).resolve().parent
    if args.load:
        run_s = int(args.duration) + 15
        procs.append(subprocess.Popen(
            [sys.executable, str(scripts / "fake_meter.py"), "--watts", str(args.meter_watts),
             "--cadence", "85", "--balance", "--hz", str(args.meter_hz), "--duration", str(run_s)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        if args.crank_address:
            procs.append(subprocess.Popen(
                [sys.executable, str(scripts / "crank_reader.py"), "--address", args.crank_address,
                 "--seconds", str(run_s)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        print("started BLE load; giving it 8 s to connect...")
        time.sleep(8)

    try:
        _get(base + "/stats/reset")  # clean measurement window
    except Exception:  # noqa: BLE001
        pass

    samples: list[dict] = []
    start = time.time()
    last_reboots = baseline.get("reboot_count")
    reboot_seen = False
    try:
        header = {"kind": "soak_header", "host": args.host, "label": args.label,
                  "load": args.load, "ui_poll": args.ui_poll, "duration": args.duration,
                  "interval": args.interval, "started_unix": int(start),
                  "baseline_stats": baseline}
        with out.open("w") as f:
            f.write(json.dumps(header) + "\n")
            while time.time() - start < args.duration:
                time.sleep(args.interval)
                rec: dict = {"kind": "sample", "t": round(time.time() - start, 1)}
                try:
                    if args.ui_poll:
                        try:
                            _get(base + "/ui")
                        except Exception:  # noqa: BLE001
                            pass
                    st = _get_json(base + "/stats")
                    rec["stats"] = st
                    if st.get("reboot_count") != last_reboots:
                        rec["REBOOT"] = True
                        reboot_seen = True
                        last_reboots = st.get("reboot_count")
                except Exception as e:  # noqa: BLE001
                    rec["error"] = str(e)
                samples.append(rec)
                f.write(json.dumps(rec) + "\n")
                f.flush()
                st = rec.get("stats", {})
                flag = ""
                if rec.get("REBOOT"):
                    flag = f"  <<REBOOT reset={st.get('reset_reason')} sw={st.get('sw_reason')}"
                lc, wm = st.get("loop_count", 0), st.get("window_ms", 0)
                lps = round(lc * 1000 / wm) if wm else 0
                print(f"  t={rec['t']:6.0f}s  p95={st.get('loop_p95_us','?')}us "
                      f"max={st.get('loop_max_us','?')}us stalls50={st.get('stalls_50ms','?')} "
                      f"loops/s={lps} heap={st.get('free_heap','?')} frag={st.get('frag_pct','?')}%{flag}")
    except KeyboardInterrupt:
        print("\nstopped early (data kept).")
    finally:
        for p in procs:
            p.terminate()

    good = [s["stats"] for s in samples if "stats" in s]
    summary: dict = {"samples": len(good)}
    if good:
        last = good[-1]
        lc, wm = last.get("loop_count", 0), last.get("window_ms", 0)
        summary.update({
            "loop_p95_us_last": last.get("loop_p95_us"),
            "loop_max_us": max(s.get("loop_max_us", 0) for s in good),
            "stalls_50ms_total": last.get("stalls_50ms"),
            "stalls_200ms_total": last.get("stalls_200ms"),
            "loops_per_s": round(lc * 1000 / wm) if wm else 0,
            "min_free_heap": min(s.get("free_heap", 1 << 31) for s in good),
            "max_frag_pct": max(s.get("frag_pct", 0) for s in good),
            "reboots": reboot_seen,
            "reset_reason": last.get("reset_reason"),
            "sw_reason": last.get("sw_reason"),
        })
    with out.open("a") as f:
        f.write(json.dumps({"kind": "summary", **summary}) + "\n")
    print("\n=== SOAK SUMMARY ===")
    for k, v in summary.items():
        print(f"  {k}: {v}")
    print(f"\nJSONL: {out}")


if __name__ == "__main__":
    main()
