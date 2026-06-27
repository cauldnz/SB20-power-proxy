"""Tests for the .zwo/.fit -> device-JSON importers (sb20proxy.workout.importers)."""

from __future__ import annotations

import json

import pytest

from sb20proxy.workout import (
    WorkoutImportError,
    fit_steps_to_segments,
    from_zwo,
    to_device_json,
)
from sb20proxy.workout.importers import _fit_power

SAMPLE_ZWO = """<workout_file>
  <name>Test 2x8</name>
  <workout>
    <Warmup Duration="600" PowerLow="0.4" PowerHigh="0.7"/>
    <SteadyState Duration="300" Power="0.6" Cadence="85"/>
    <IntervalsT Repeat="2" OnDuration="480" OffDuration="120"
      OnPower="0.99" OffPower="0.5" Cadence="90"/>
    <FreeRide Duration="180"/>
    <Cooldown Duration="300" PowerLow="0.5" PowerHigh="0.3"/>
  </workout>
</workout_file>"""


def test_from_zwo_structure():
    w = from_zwo(SAMPLE_ZWO)
    assert w.name == "Test 2x8"
    labels = [s.label for s in w.segments]
    assert labels == [
        "Warm-up", "Steady", "Interval 1", "Recovery", "Interval 2", "Recovery",
        "Free ride", "Cool-down",
    ]
    # ramp -> average %FTP
    assert w.segments[0].pct_ftp == pytest.approx(0.55)   # (0.4+0.7)/2
    assert w.segments[0].duration_s == 600
    assert w.segments[7].pct_ftp == pytest.approx(0.40)   # (0.5+0.3)/2
    # steady carries its power + cadence
    assert w.segments[1].pct_ftp == pytest.approx(0.60)
    assert w.segments[1].cadence_rpm == 85
    # intervals expand with on/off power + on cadence
    assert w.segments[2].pct_ftp == pytest.approx(0.99)
    assert w.segments[2].cadence_rpm == 90
    assert w.segments[3].pct_ftp == pytest.approx(0.50)
    # free ride has no target at all
    free = w.segments[6]
    assert free.power_w is None and free.pct_ftp is None and free.zone is None


def test_to_device_json_shape():
    w = from_zwo(SAMPLE_ZWO)
    d = json.loads(to_device_json(w, 285))
    assert d["name"] == "Test 2x8"
    assert d["ftp_w"] == 285
    assert len(d["segments"]) == 8
    assert d["segments"][0] == {"t": 600, "label": "Warm-up", "pct_ftp": 0.55}
    assert d["segments"][2] == {"t": 480, "label": "Interval 1", "pct_ftp": 0.99, "cadence_rpm": 90}
    assert d["segments"][6] == {"t": 180, "label": "Free ride"}  # no target / cadence keys


def test_from_zwo_rejects_garbage():
    with pytest.raises(WorkoutImportError):
        from_zwo("not xml at all <<<")
    with pytest.raises(WorkoutImportError):
        from_zwo("<workout_file><name>x</name></workout_file>")  # no <workout> body


def test_fit_power_decode():
    assert _fit_power(90, 90) == {"pct_ftp": 0.9}        # 1..1000 -> %FTP
    assert _fit_power(80, 100) == {"pct_ftp": 0.9}       # midpoint
    assert _fit_power(1250, 1250) == {"power_w": 250}    # >1000 -> watts (value-1000)
    assert _fit_power(None, None) == {}                  # no target
    assert _fit_power(0, 0) == {}


def test_fit_steps_to_segments():
    steps = [
        {"duration_s": 300, "label": "WU", "pct_ftp": 0.55},
        {"duration_s": 0, "label": "skip-me"},            # zero duration dropped
        {"duration_s": 480, "label": "Effort", "power_w": 250, "cadence_rpm": 90},
        {"duration_s": 120, "label": "Free"},             # no target -> free
    ]
    segs = fit_steps_to_segments(steps)
    assert [s.label for s in segs] == ["WU", "Effort", "Free"]
    assert segs[0].pct_ftp == pytest.approx(0.55)
    assert segs[1].power_w == 250
    assert segs[1].cadence_rpm == 90
    assert segs[2].power_w is None and segs[2].pct_ftp is None
