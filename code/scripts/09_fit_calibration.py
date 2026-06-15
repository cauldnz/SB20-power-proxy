#!/usr/bin/env python3
"""Fit a meter-to-meter calibration profile from a dual-meter capture.

Consumes a 07_capture_multi.py recording of the DUT meter (e.g. the XCadey) and a
reference meter (e.g. the Assioma) on one clock, and writes a JSON correction profile
that `04_run_proxy.py --profile <file>` applies in real time (on the Pi or the ESP32
jersey-pocket bridge). Linear if the error is ~constant; a power-curve grid if not.

Example:
  python scripts/09_fit_calibration.py \
      --input findings/captures/QUICK-multi-20260615-064037.jsonl \
      --target stages --ref assioma \
      --output findings/calibration-stages-vs-assioma.json
"""

from __future__ import annotations

import argparse
import glob
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from sb20proxy.calibration import (  # noqa: E402
    fit_grid,
    fit_scale_offset,
    load_dual_meter_capture,
    residual_watts,
)


def main() -> int:
    p = argparse.ArgumentParser(description="Fit a meter-to-meter calibration profile")
    p.add_argument("--input", required=True, help="dual-meter capture JSONL (glob ok)")
    p.add_argument("--target", default="stages", help="meter being corrected (DUT)")
    p.add_argument("--ref", default="assioma", help="reference meter")
    p.add_argument("--mode", choices=["auto", "grid", "linear"], default="auto")
    p.add_argument("--bins", type=int, default=6, help="power bins for the grid fit")
    p.add_argument("--output", type=Path, help="write the chosen profile JSON here")
    args = p.parse_args()

    matches = sorted(glob.glob(args.input))
    if not matches:
        print(f"no file matched: {args.input}", file=sys.stderr)
        return 2
    path = Path(matches[-1])
    pairs = load_dual_meter_capture(path, target=args.target, ref=args.ref)
    print(f"# calibration {args.target} -> {args.ref}  ({path.name})")
    print(f"matched active seconds: {len(pairs)}")
    if len(pairs) < 20:
        print("too few matched seconds (<20) — capture a longer dual-meter ride.", file=sys.stderr)
        return 1

    linear = fit_scale_offset(pairs, target=args.target, ref=args.ref)
    grid = fit_grid(pairs, target=args.target, ref=args.ref, n_bins=args.bins)
    r_lin = residual_watts(linear, pairs)
    r_grid = residual_watts(grid, pairs)
    print(f"linear:  scale={linear.scale}  offset={linear.offset} W  -> residual {r_lin}")
    print(f"grid:    {grid.breakpoints}")
    print(f"         -> residual {r_grid}")

    if args.mode == "linear":
        chosen = linear
    elif args.mode == "grid":
        chosen = grid
    else:  # auto: prefer the grid only if it beats linear by >1 W mean error
        lin_e = r_lin["mean_w"] if r_lin["mean_w"] is not None else 1e9
        grid_e = r_grid["mean_w"] if r_grid["mean_w"] is not None else 1e9
        chosen = grid if (grid.kind == "grid" and grid_e < lin_e - 1.0) else linear

    print(f"\nchosen: {chosen.kind}")
    if args.output:
        chosen.save(args.output)
        print(f"wrote {args.output}")
    else:
        print("(no --output given; profile not saved)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
