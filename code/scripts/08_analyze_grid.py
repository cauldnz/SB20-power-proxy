#!/usr/bin/env python3
"""Analyse a multi-source capture (07_capture_multi.py output).

Three jobs, all from one same-clock JSONL:

  1. Open-question #7 — does the SB20 rescale crank power? Compares the bike's
     FE-C instantaneous power against the Stages crank's, second by second. A
     ratio of ~1.00 with tight spread => pass-through (direct Assioma feed will
     just work). A consistent non-unity ratio => a single bike factor to
     compensate.

  2. Calibration surface — Stages/Assioma power ratio binned by power x cadence.

  3. Grid-design guidance — regresses the ratio against cadence, power, and
     torque (P/cadence) and reports which explains it best. If torque alone
     explains it (the day-1 result suggested so), the calibration ride is a
     ~1-D torque sweep (few cells); if power and cadence matter independently,
     it needs a 2-D grid. Drives "how many cells do we actually need".

Usage:
    python 08_analyze_grid.py --input ../findings/captures/CAL-multi-*.jsonl
"""

from __future__ import annotations

import argparse
import glob
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Any


def load(path: Path):
    """Return per-second means keyed by int(monotonic_s) for each source."""
    # src -> sec -> {'p':[...], 'c':[...]}
    acc: dict[str, dict[int, dict[str, list]]] = defaultdict(lambda: defaultdict(lambda: {"p": [], "c": []}))
    sources_seen: set[str] = set()
    with open(path) as f:
        for line in f:
            try:
                r = json.loads(line)
            except json.JSONDecodeError:
                continue
            if r.get("kind") not in ("broadcast", "acknowledged"):
                continue
            src = r.get("source")
            if not src:
                continue
            sources_seen.add(src)
            d = r.get("data") or {}
            pg = d.get("page_no_toggle")
            # instantaneous power: bike-power page 0x10, FE-C page 0x19
            if pg in (0x10, 0x19):
                p = d.get("instantaneous_power_w")
                c = d.get("instantaneous_cadence_rpm")
                sec = int(r.get("monotonic_s", 0))
                if isinstance(p, (int, float)):
                    acc[src][sec]["p"].append(p)
                if isinstance(c, (int, float)):
                    acc[src][sec]["c"].append(c)
    # collapse to per-second means
    out: dict[str, dict[int, tuple]] = {}
    for src, secs in acc.items():
        out[src] = {}
        for sec, v in secs.items():
            pm = statistics.mean(v["p"]) if v["p"] else None
            cm = statistics.mean(v["c"]) if v["c"] else None
            out[src][sec] = (pm, cm)
    return out, sources_seen


def linfit(xs: list[float], ys: list[float]):
    """Least-squares y=a+bx; return (a, b, r2)."""
    n = len(xs)
    if n < 3:
        return None
    mx, my = statistics.mean(xs), statistics.mean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    if sxx == 0:
        return None
    b = sxy / sxx
    a = my - b * mx
    syy = sum((y - my) ** 2 for y in ys)
    r2 = (sxy ** 2 / (sxx * syy)) if syy > 0 else 0.0
    return a, b, r2


def main() -> int:
    ap = argparse.ArgumentParser(description="Analyse a multi-source calibration capture")
    ap.add_argument("--input", required=True, help="multi-source JSONL (glob ok)")
    ap.add_argument("--ref", default="assioma", help="reference source label (default assioma)")
    ap.add_argument("--target", default="stages", help="source being corrected (default stages)")
    args = ap.parse_args()

    matches = sorted(glob.glob(args.input))
    if not matches:
        print(f"no file matched: {args.input}"); return 2
    path = Path(matches[-1])
    data, srcs = load(path)
    print(f"# Calibration analysis — {path.name}")
    print(f"sources present: {sorted(srcs)}\n")

    tgt = data.get(args.target, {})
    ref = data.get(args.ref, {})
    fec = data.get("bike_fec", {})

    # ---- (1) #7: bike FE-C vs Stages crank --------------------------------
    print("## Open-question #7 — does the bike rescale crank power?")
    if not fec:
        print("  no `bike_fec` source in this capture — run 07_capture_multi.py with --fec-id.\n")
    else:
        ratios = []
        for sec in sorted(set(tgt) & set(fec)):
            cp = tgt[sec][0]; fp = fec[sec][0]
            if cp and fp and cp > 40 and fp > 40:
                ratios.append(fp / cp)
        if ratios:
            m = statistics.mean(ratios); sd = statistics.pstdev(ratios)
            print(f"  matched seconds: {len(ratios)}")
            print(f"  bike_FEC / crank power = {m:.3f} ± {sd:.3f}")
            if abs(m - 1.0) < 0.02 and sd < 0.05:
                print("  => VERDICT: pass-through (no meaningful scaling). Direct Assioma feed "
                      "should land erg targets on true Assioma watts. #7 effectively closed.")
            else:
                print(f"  => VERDICT: the bike applies ~{m:.3f}x to crank power. Compensate by "
                      f"feeding Assioma watts / {m:.3f} (one bike factor, not a per-meter model).")
        else:
            print("  not enough overlapping FE-C + crank samples.")
    print()

    # ---- (2)+(3) calibration: target/ref ratio vs cadence/power/torque ----
    print(f"## Calibration — {args.target}/{args.ref} ratio")
    pairs = []  # (power_ref, cadence, ratio, torque)
    for sec in sorted(set(tgt) & set(ref)):
        tp, tc = tgt[sec]; rp, rc = ref[sec]
        cad = tc if tc else rc
        if not (tp and rp) or tp < 40 or rp < 40 or not cad or cad < 30:
            continue
        torque = rp / cad  # proportional to true torque (P/omega)
        pairs.append((rp, cad, tp / rp, torque))
    if len(pairs) < 20:
        print(f"  only {len(pairs)} matched active seconds — capture more before trusting this.\n")
        return 0
    print(f"  matched active seconds: {len(pairs)}")
    rr = [p[2] for p in pairs]
    print(f"  overall ratio: mean={statistics.mean(rr):.3f} median={statistics.median(rr):.3f} "
          f"sd={statistics.pstdev(rr):.3f}\n")

    # which dimension explains the ratio? (R^2 of single-variable linear fit)
    print("  what drives the ratio (single-variable R^2 — higher = better predictor):")
    for name, idx in (("cadence", 1), ("power", 0), ("torque P/cad", 3)):
        fit = linfit([p[idx] for p in pairs], rr)
        if fit:
            a, b, r2 = fit
            print(f"    {name:14s}: R^2={r2:.3f}  (ratio = {a:.3f} + {b:+.5f}*{name.split()[0]})")
    print()

    # ratio surface: power band x cadence band
    print("  ratio surface (rows=power band W, cols=cadence band rpm):")
    pbands = [(0, 180), (180, 280), (280, 400), (400, 2000)]
    cbands = [(40, 70), (70, 90), (90, 120)]
    hdr = "    power\\cad  " + "".join(f"{lo}-{hi:<4}" for lo, hi in cbands)
    print(hdr)
    for plo, phi in pbands:
        row = f"    {plo:4d}-{phi:<5d}"
        for clo, chi in cbands:
            sub = [p[2] for p in pairs if plo <= p[0] < phi and clo <= p[1] < chi]
            row += f"{statistics.mean(sub):.3f}({len(sub):>3}) " if len(sub) >= 3 else "  --     "
        print(row)
    print()

    # ---- cell-count guidance ----------------------------------------------
    fit_t = linfit([p[3] for p in pairs], rr)
    fit_c = linfit([p[1] for p in pairs], rr)
    fit_p = linfit([p[0] for p in pairs], rr)
    print("## Grid-design guidance")
    best = max((("torque", fit_t), ("cadence", fit_c), ("power", fit_p)),
               key=lambda kv: kv[1][2] if kv[1] else -1)
    bname, bfit = best
    if bfit and bfit[2] > 0.5 and bname in ("torque", "cadence"):
        print(f"  The ratio is mostly 1-D in {bname} (R^2={bfit[2]:.2f}). A calibration ride can be "
              f"a {bname.upper()} SWEEP — ~5-6 cells spanning the range likely suffices; a full 2-D "
              f"grid is overkill. Verify residuals are flat across power.")
    elif fit_p and fit_c and fit_p[2] > 0.3 and fit_c[2] > 0.3:
        print("  Both power and cadence carry independent signal — keep the 2-D grid "
              "(3 powers x 3-4 cadences). Re-run this after the grid ride to prune cells.")
    else:
        print("  Signal is weak/noisy in all single dimensions — collect more samples per cell "
              "(longer holds) before deciding the grid resolution.")
    print("\n(Tip: re-run on subsets of cells to test how few reproduce the full surface.)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
