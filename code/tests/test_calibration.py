"""Calibration fitter: synthetic recovery (the proof) + a real dual-meter fit.

The headline test injects a KNOWN non-linear meter error, fits a profile from the
(dut, ref) pairs, and asserts the fitted transform recovers the true power — the same
correction that would run on the proxy / ESP32 bridge. A second test fits against the
real QUICK-multi capture (stages vs assioma) and checks the glitch filter.
"""

from __future__ import annotations

import pytest

from sb20proxy.calibration import (
    CalibrationProfile,
    fit_grid,
    fit_scale_offset,
    load_dual_meter_capture,
    load_profile,
    residual_watts,
)
from sb20proxy.reading import PowerReading
from sb20proxy.transform import GridTransform, ScaleOffsetTransform

QUICK_MULTI = "QUICK-multi-20260615-064037.jsonl"


def _dut_reads_high_nonlinear(true: float) -> float:
    """A DUT that reads ~10% high at 100 W, easing to ~2.5% high at 500 W."""
    frac = 1.10 - 0.0001875 * (true - 100.0)
    return true * frac


def _synthetic_pairs():
    # (dut_power, ref_power=true, cadence)
    return [(_dut_reads_high_nonlinear(t), float(t), 85.0) for t in range(60, 601, 5)]


def _apply_residual(profile, pairs):
    t = profile.to_transform()
    errs = []
    for dut, ref, _cad in pairs:
        out = t.apply(PowerReading(timestamp=0.0, power_w=round(dut), source_id="x"))
        errs.append(abs(out.power_w - ref))
    return max(errs), sum(errs) / len(errs)


# --------------------------- synthetic recovery ---------------------------

def test_grid_recovers_nonlinear_error():
    pairs = _synthetic_pairs()
    profile = fit_grid(pairs, target="dut", ref="ref", n_bins=6)
    assert profile.kind == "grid"
    assert len(profile.breakpoints) >= 4
    worst, mean = _apply_residual(profile, pairs)
    # the grid should pull a 10%/500-W error back to within a few watts
    assert mean < 4.0, f"mean residual {mean:.1f} W too high"
    assert worst < 12.0, f"worst residual {worst:.1f} W too high"


def test_scale_offset_recovers_linear_error():
    # DUT reads a constant 8% high.
    pairs = [(t * 1.08, float(t), 85.0) for t in range(80, 521, 10)]
    profile = fit_scale_offset(pairs, target="dut", ref="ref")
    assert profile.kind == "scale_offset"
    assert abs(profile.scale - 1.0 / 1.08) < 0.01
    _worst, mean = _apply_residual(profile, pairs)
    assert mean < 1.0


def test_residual_watts_reports_metrics():
    pairs = _synthetic_pairs()
    r = residual_watts(fit_grid(pairs, target="d", ref="r"), pairs)
    assert r["mean_w"] is not None and r["mean_pct"] is not None


# --------------------------- profile round-trip ---------------------------

def test_profile_save_load_roundtrip(tmp_path):
    profile = fit_grid(_synthetic_pairs(), target="dut", ref="ref")
    path = tmp_path / "cal.json"
    profile.save(path)
    loaded = load_profile(path)
    assert loaded.kind == profile.kind
    assert loaded.breakpoints == profile.breakpoints
    assert isinstance(loaded.to_transform(), GridTransform)


def test_scale_offset_profile_to_transform():
    p = CalibrationProfile(kind="scale_offset", target="d", ref="r", scale=0.9, offset=5.0)
    assert isinstance(p.to_transform(), ScaleOffsetTransform)


# ------------------------------ real data ------------------------------

def test_fit_against_real_quick_multi(captures_dir):
    pairs = load_dual_meter_capture(captures_dir / QUICK_MULTI, target="stages", ref="assioma")
    assert len(pairs) > 20, "expected plenty of overlapping stages/assioma seconds"
    # the 13567 W Assioma glitch must be filtered out
    assert all(10 <= dut <= 2000 and 10 <= ref <= 2000 for dut, ref, _c in pairs)
    profile = fit_grid(pairs, target="stages", ref="assioma")
    # factors should be a sane correction (within ±50%), not garbage
    factors = [f for _p, f in profile.breakpoints]
    assert factors and all(0.5 < f < 1.5 for f in factors), factors


def test_too_few_samples_raises():
    with pytest.raises(ValueError):
        fit_grid([(100.0, 100.0, 80.0)], target="d", ref="r")
