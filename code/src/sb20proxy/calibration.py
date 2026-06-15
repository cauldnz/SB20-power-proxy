"""Meter-to-meter calibration: fit a correction profile from a dual-meter capture.

A calibration ride records two meters on one clock (`07_capture_multi.py`): the meter
to be corrected (the DUT — e.g. the XCadey on the velodrome bike) and a reference
(e.g. the Assioma). This module fits a profile mapping **DUT power → reference-
equivalent power**, which the proxy / ESP32 bridge then applies as a `PowerTransform`:

- ~linear error  → `ScaleOffsetTransform`
- non-linear across the power curve → `GridTransform` (piecewise power→factor)

The profile serialises to JSON so it can be shipped to the Pi or the ESP32.
"""

from __future__ import annotations

import json
import statistics
from collections import defaultdict
from dataclasses import asdict, dataclass, field
from pathlib import Path

from sb20proxy.transform import GridTransform, PowerTransform, ScaleOffsetTransform

# Plausibility filter — reject meter glitches (a real Assioma 0x10 startup artifact of
# 13567 W was seen in QUICK-multi). Sprints can exceed 1000 W, so keep the ceiling high.
MIN_POWER_W = 10.0
MAX_POWER_W = 2000.0

# (dut_power, ref_power, cadence)
Pair = tuple


@dataclass
class CalibrationProfile:
    """A fitted DUT→reference correction. `to_transform()` makes it runnable."""

    kind: str                      # "grid" | "scale_offset"
    target: str                    # label of the meter being corrected (DUT)
    ref: str                       # label of the reference meter
    breakpoints: list = field(default_factory=list)  # [[dut_power, factor], ...] (grid)
    scale: float = 1.0
    offset: float = 0.0
    meta: dict = field(default_factory=dict)

    def to_transform(self) -> PowerTransform:
        if self.kind == "grid":
            return GridTransform([(float(p), float(f)) for p, f in self.breakpoints])
        return ScaleOffsetTransform(scale=self.scale, offset=self.offset)

    def to_dict(self) -> dict:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: dict) -> CalibrationProfile:
        return cls(**d)

    def save(self, path: str | Path) -> None:
        Path(path).write_text(json.dumps(self.to_dict(), indent=2))


def load_profile(path: str | Path) -> CalibrationProfile:
    return CalibrationProfile.from_dict(json.loads(Path(path).read_text()))


def load_transform(path: str | Path) -> PowerTransform:
    return load_profile(path).to_transform()


def load_dual_meter_capture(path: str | Path, *, target: str, ref: str) -> list[Pair]:
    """Matched per-second (dut_power, ref_power, cadence) from a 07_capture_multi file.

    Powers are per-second means, filtered to a plausible range (drops glitches).
    """
    acc: dict[str, dict[int, dict[str, list]]] = defaultdict(
        lambda: defaultdict(lambda: {"p": [], "c": []})
    )
    with open(path) as fh:
        for line in fh:
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            if rec.get("kind") not in ("broadcast", "acknowledged"):
                continue
            src = rec.get("source")
            if src not in (target, ref):
                continue
            data = rec.get("data") or {}
            if data.get("page_no_toggle") not in (0x10, 0x19):
                continue
            power = data.get("instantaneous_power_w")
            cadence = data.get("instantaneous_cadence_rpm")
            sec = int(rec.get("monotonic_s", 0))
            if isinstance(power, (int, float)):
                acc[src][sec]["p"].append(power)
            if isinstance(cadence, (int, float)):
                acc[src][sec]["c"].append(cadence)

    per: dict[str, dict[int, tuple]] = {}
    for src, secs in acc.items():
        per[src] = {
            sec: (
                statistics.mean(v["p"]) if v["p"] else None,
                statistics.mean(v["c"]) if v["c"] else None,
            )
            for sec, v in secs.items()
        }

    tgt, rf = per.get(target, {}), per.get(ref, {})
    pairs: list[Pair] = []
    for sec in sorted(set(tgt) & set(rf)):
        dp, dc = tgt[sec]
        rp, rc = rf[sec]
        cad = dc if dc else rc
        if dp is None or rp is None:
            continue
        if not (MIN_POWER_W <= dp <= MAX_POWER_W) or not (MIN_POWER_W <= rp <= MAX_POWER_W):
            continue
        pairs.append((dp, rp, cad))
    return pairs


def fit_scale_offset(pairs: list[Pair], *, target: str, ref: str) -> CalibrationProfile:
    """Least-squares ref = offset + scale*dut."""
    if len(pairs) < 3:
        raise ValueError("need >=3 matched samples to fit")
    duts = [p[0] for p in pairs]
    refs = [p[1] for p in pairs]
    mx, my = statistics.mean(duts), statistics.mean(refs)
    sxx = sum((x - mx) ** 2 for x in duts)
    sxy = sum((x - mx) * (y - my) for x, y in zip(duts, refs, strict=True))
    scale = sxy / sxx if sxx else 1.0
    offset = my - scale * mx
    return CalibrationProfile(
        kind="scale_offset", target=target, ref=ref,
        scale=round(scale, 4), offset=round(offset, 2),
        meta={"n_samples": len(pairs)},
    )


def fit_grid(pairs: list[Pair], *, target: str, ref: str, n_bins: int = 6) -> CalibrationProfile:
    """Piecewise correction: bin by DUT power, factor = mean(ref/dut) per bin."""
    if len(pairs) < 3:
        raise ValueError("need >=3 matched samples to fit")
    duts = [p[0] for p in pairs]
    lo, hi = min(duts), max(duts)
    if hi <= lo:
        return fit_scale_offset(pairs, target=target, ref=ref)
    width = (hi - lo) / n_bins
    bins: dict[int, list[Pair]] = defaultdict(list)
    for dut, ref_p, _cad in pairs:
        idx = min(int((dut - lo) / width), n_bins - 1)
        bins[idx].append((dut, ref_p))
    breakpoints = []
    for idx in sorted(bins):
        members = bins[idx]
        if len(members) < 3:
            continue
        center = statistics.mean(d for d, _ in members)
        factor = statistics.mean(r / d for d, r in members if d > 0)
        breakpoints.append([round(center, 1), round(factor, 4)])
    if len(breakpoints) < 2:
        # too sparse for a curve — fall back to a single linear correction
        return fit_scale_offset(pairs, target=target, ref=ref)
    return CalibrationProfile(
        kind="grid", target=target, ref=ref, breakpoints=breakpoints,
        meta={"n_samples": len(pairs), "n_bins": len(breakpoints)},
    )


def residual_watts(profile: CalibrationProfile, pairs: list[Pair]) -> dict:
    """Mean/median |corrected_dut − ref| in watts and %, for picking a profile."""
    from sb20proxy.reading import PowerReading
    transform = profile.to_transform()
    errors = []
    pct = []
    for dut, ref_p, _cad in pairs:
        reading = PowerReading(timestamp=0.0, power_w=round(dut), source_id="cal")
        corrected = transform.apply(reading).power_w
        err = abs(corrected - ref_p)
        errors.append(err)
        if ref_p > 0:
            pct.append(100.0 * err / ref_p)
    return {
        "mean_w": round(statistics.mean(errors), 2) if errors else None,
        "median_w": round(statistics.median(errors), 2) if errors else None,
        "mean_pct": round(statistics.mean(pct), 2) if pct else None,
    }
