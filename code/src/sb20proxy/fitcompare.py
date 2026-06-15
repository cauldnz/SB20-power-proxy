"""Compare a recorded FIT against the power stream we broadcast.

The proxy broadcasts a captured ride; a head unit (Garmin) records it to a FIT.
This overlays the FIT's power/cadence on the capture's, auto-aligns them in time
(the recording starts at an arbitrary point in the broadcast), and reports how
faithfully the broadcast → radio → head-unit → FIT path reproduced the numbers.

- `extract_capture_power` reads the broadcast source (a capture JSONL) — real data,
  tested.
- `compare_series` does the alignment + fidelity maths — schema-independent, tested.
- `extract_fit_power` reads a Garmin FIT via fitparse (an [analysis] extra). Its FIT
  field names are confirmed against a real Garmin file, not assumed.
"""

from __future__ import annotations

import json
import statistics
from collections import defaultdict
from pathlib import Path

# (seconds_from_start, power_w_or_None, cadence_or_None)
Sample = tuple


def extract_capture_power(path: str | Path) -> list[Sample]:
    """Power/cadence vs time from a capture JSONL (page 0x10), paced by monotonic_s."""
    out: list[Sample] = []
    with open(path) as fh:
        for line in fh:
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            if rec.get("kind") not in ("broadcast", "acknowledged"):
                continue
            data = rec.get("data") or {}
            if (data.get("page") or 0) & 0x7F != 0x10:
                continue
            t = rec.get("monotonic_s")
            if t is None:
                continue
            out.append((float(t), data.get("instantaneous_power_w"),
                        data.get("instantaneous_cadence_rpm")))
    return out


def extract_fit_power(path: str | Path) -> list[Sample]:
    """Power/cadence vs time from a Garmin FIT 'record' messages (needs fitparse)."""
    import fitparse  # [analysis] extra; imported lazily

    fit = fitparse.FitFile(str(path))
    rows: list[tuple[float, object, object]] = []
    t0 = None
    for msg in fit.get_messages("record"):
        vals = {f.name: f.value for f in msg.fields}
        ts = vals.get("timestamp")
        if ts is None:
            continue
        epoch = ts.timestamp() if hasattr(ts, "timestamp") else float(ts)
        if t0 is None:
            t0 = epoch
        rows.append((epoch - t0, vals.get("power"), vals.get("cadence")))
    return rows


def _resample_1hz(series: list[Sample]) -> dict[int, float]:
    """Per-second mean power, keyed by whole seconds from the series start."""
    powered = [(t, p) for t, p, _c in series if isinstance(p, (int, float))]
    if not powered:
        return {}
    t0 = min(t for t, _ in powered)
    buckets: dict[int, list[float]] = defaultdict(list)
    for t, p in powered:
        buckets[int(t - t0)].append(p)
    return {s: statistics.mean(v) for s, v in buckets.items()}


def _pearson(xs: list[float], ys: list[float]) -> float:
    n = len(xs)
    if n < 2:
        return 0.0
    mx, my = statistics.mean(xs), statistics.mean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    syy = sum((y - my) ** 2 for y in ys)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys, strict=True))
    if sxx <= 0 or syy <= 0:
        return 0.0
    return sxy / (sxx * syy) ** 0.5


def compare_series(reference: list[Sample], recorded: list[Sample],
                   *, tolerance_w: float = 10.0) -> dict:
    """Align `recorded` onto `reference` (best whole-second offset) and score fidelity.

    reference = what we broadcast (capture); recorded = the head unit's FIT.
    """
    ref = _resample_1hz(reference)
    rec = _resample_1hz(recorded)
    if not ref or not rec:
        return {"matched_s": 0, "note": "empty series"}

    best = None
    for offset in range(0, max(ref) + 1):
        errs, pairs = [], []
        for sec, rec_p in rec.items():
            ref_p = ref.get(sec + offset)
            if ref_p is not None:
                errs.append(abs(rec_p - ref_p))
                pairs.append((ref_p, rec_p))
        if len(pairs) < 30:
            continue
        mae = statistics.mean(errs)
        if best is None or mae < best[0]:
            best = (mae, offset, pairs)

    if best is None:
        return {"matched_s": 0, "note": "no overlapping window >=30 s"}

    mae, offset, pairs = best
    within = sum(1 for r, rp in pairs if abs(r - rp) <= tolerance_w)
    return {
        "offset_s": offset,
        "matched_s": len(pairs),
        "mean_abs_err_w": round(mae, 2),
        "pct_within_tol": round(100.0 * within / len(pairs), 1),
        "tolerance_w": tolerance_w,
        "correlation": round(_pearson([r for r, _ in pairs], [rp for _, rp in pairs]), 4),
    }
