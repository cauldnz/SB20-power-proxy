"""Tests for the FIT-vs-broadcast comparison core (schema-independent + real capture).

The FIT-reading half (extract_fit_power) needs fitparse + a real Garmin file, so it's
verified manually against the actual recording, not in CI. Here we test the capture
extraction (real data) and the alignment/fidelity maths (synthetic).
"""

from __future__ import annotations

from sb20proxy.fitcompare import compare_series, extract_capture_power

STEADY = "A-stagesL-steady-20260614-165737.jsonl"


def _ramp_series(powers, start_t=0.0):
    # 1 Hz (t, power, cadence) samples
    return [(start_t + i, float(p), 80) for i, p in enumerate(powers)]


def test_extract_capture_power_real(captures_dir):
    samples = extract_capture_power(captures_dir / STEADY)
    powered = [p for _t, p, _c in samples if isinstance(p, (int, float))]
    assert len(powered) > 100
    assert all(0 <= p <= 2000 for p in powered)
    # timestamps are monotonic non-decreasing
    ts = [t for t, _p, _c in samples]
    assert ts == sorted(ts)


def test_compare_identical_series_perfect():
    ramp = list(range(60, 240))  # non-periodic -> unambiguous alignment
    r = compare_series(_ramp_series(ramp), _ramp_series(ramp))
    assert r["offset_s"] == 0
    assert r["mean_abs_err_w"] < 0.5
    assert r["correlation"] > 0.99
    assert r["pct_within_tol"] == 100.0


def test_compare_detects_time_offset():
    ramp = list(range(60, 260))
    ref = _ramp_series(ramp)
    rec = _ramp_series(ramp[40:160])  # a 120 s window starting 40 s in
    r = compare_series(ref, rec)
    assert r["offset_s"] == 40
    assert r["correlation"] > 0.99
    assert r["mean_abs_err_w"] < 0.5


def test_compare_flags_scaled_recording():
    ramp = list(range(60, 260))
    ref = _ramp_series(ramp)
    rec = _ramp_series([p * 1.2 for p in ramp])  # recorded 20% high (would be a real fault)
    r = compare_series(ref, rec, tolerance_w=5)
    assert r["pct_within_tol"] < 60.0  # most samples out of tolerance


def test_compare_empty():
    assert compare_series([], [])["matched_s"] == 0
