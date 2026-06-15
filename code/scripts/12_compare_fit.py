#!/usr/bin/env python3
"""Compare a head-unit's recorded FIT against the power stream the proxy broadcast.

Validates the broadcast -> radio -> head-unit -> FIT path end to end: a high
correlation + low mean error means the Garmin faithfully recorded what we sent.

Example:
  python scripts/12_compare_fit.py \
      --fit /mnt/f/GARMIN/Activity/2026-06-15-XX-XX-XX-Bike.fit \
      --capture findings/captures/A-stagesL-steady-20260614-165737.jsonl
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from sb20proxy.fitcompare import (  # noqa: E402
    compare_series,
    extract_capture_power,
    extract_fit_power,
)


def main() -> int:
    p = argparse.ArgumentParser(description="Compare a recorded FIT vs the broadcast capture")
    p.add_argument("--fit", type=Path, required=True, help="the head unit's recorded FIT")
    p.add_argument("--capture", type=Path, required=True, help="the capture we broadcast")
    p.add_argument("--tolerance", type=float, default=10.0, help="match tolerance in watts")
    args = p.parse_args()

    ref = extract_capture_power(args.capture)
    rec = extract_fit_power(args.fit)
    print(f"capture (broadcast) power samples: {len(ref)}   FIT power samples: {len(rec)}")

    result = compare_series(ref, rec, tolerance_w=args.tolerance)
    print("comparison:")
    for key, val in result.items():
        print(f"  {key}: {val}")

    corr = result.get("correlation", 0) or 0
    mae = result.get("mean_abs_err_w", 1e9)
    if result.get("matched_s", 0) >= 30 and corr > 0.95 and mae is not None and mae < 15:
        print("VERDICT: PASS — the FIT faithfully reproduces the broadcast power.")
        return 0
    print("VERDICT: REVIEW — alignment or fidelity below threshold (see numbers above).")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
