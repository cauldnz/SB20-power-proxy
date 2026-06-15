"""Unit tests for the correction transforms (meter-to-meter calibration)."""

from __future__ import annotations

import pytest

from sb20proxy.reading import PowerReading
from sb20proxy.transform import (
    GridTransform,
    IdentityTransform,
    ScaleOffsetTransform,
)


def _r(power: int) -> PowerReading:
    return PowerReading(timestamp=0.0, power_w=power, source_id="t")


def test_identity_passes_through():
    r = _r(250)
    assert IdentityTransform().apply(r) is r


def test_scale_offset():
    t = ScaleOffsetTransform(scale=0.5, offset=10.0)
    assert t.apply(_r(200)).power_w == 110  # 200*0.5 + 10
    assert t.apply(_r(0)).power_w == 10


def test_scale_offset_clamps_at_zero():
    t = ScaleOffsetTransform(scale=1.0, offset=-50.0)
    assert t.apply(_r(20)).power_w == 0


def test_scale_offset_preserves_other_fields():
    t = ScaleOffsetTransform(scale=2.0)
    r = PowerReading(timestamp=1.5, power_w=100, cadence_rpm=90, source_id="m")
    out = t.apply(r)
    assert out.power_w == 200
    assert out.cadence_rpm == 90
    assert out.timestamp == 1.5
    assert out.source_id == "m"


def test_grid_transform_endpoints_and_interpolation():
    # reported -> factor: 100->1.00, 200->0.90, 400->0.80
    t = GridTransform([(100, 1.00), (200, 0.90), (400, 0.80)])
    assert t.apply(_r(100)).power_w == 100         # at a breakpoint
    assert t.apply(_r(200)).power_w == 180         # 200 * 0.90
    assert t.apply(_r(150)).power_w == round(150 * 0.95)  # halfway -> factor 0.95
    assert t.apply(_r(300)).power_w == round(300 * 0.85)  # halfway 200..400 -> 0.85


def test_grid_transform_holds_flat_outside_range():
    t = GridTransform([(100, 1.10), (300, 0.90)])
    assert t.apply(_r(50)).power_w == round(50 * 1.10)    # below -> first factor
    assert t.apply(_r(500)).power_w == round(500 * 0.90)  # above -> last factor


def test_grid_transform_requires_breakpoints():
    with pytest.raises(ValueError):
        GridTransform([])
