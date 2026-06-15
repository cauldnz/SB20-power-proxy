"""PowerTransform — an optional correction applied between source and target.

The proxy reads a meter, optionally corrects the number, then broadcasts it. The
correction is where meter-to-meter calibration lives:

- `IdentityTransform` — pass-through (the default; we just relay the meter).
- `ScaleOffsetTransform` — the quantitative version of a "swag" offset:
  corrected = power*scale + offset. Good when the error is ~linear.
- `GridTransform` — a piecewise-linear power→factor curve for a NON-linear error
  across the power curve (the interesting case for the XCadey velodrome use). Built
  from a meter-vs-reference calibration ride (see `08_analyze_grid.py`).

A transform runs the same in software (CI / loopback), on the Pi proxy, or on the
ESP32 jersey-pocket bridge.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import replace

from sb20proxy.reading import PowerReading


class PowerTransform(ABC):
    @abstractmethod
    def apply(self, reading: PowerReading) -> PowerReading:
        """Return a (possibly corrected) reading."""


class IdentityTransform(PowerTransform):
    """No correction — relay the meter as-is."""

    def apply(self, reading: PowerReading) -> PowerReading:
        return reading


class ScaleOffsetTransform(PowerTransform):
    """corrected = round(power * scale + offset), clamped at 0. Linear correction."""

    def __init__(self, *, scale: float = 1.0, offset: float = 0.0) -> None:
        self._scale = scale
        self._offset = offset

    def apply(self, reading: PowerReading) -> PowerReading:
        corrected = max(0, round(reading.power_w * self._scale + self._offset))
        return replace(reading, power_w=corrected)


class GridTransform(PowerTransform):
    """Piecewise-linear power-dependent correction.

    `breakpoints` is a list of (reported_power, factor) pairs; the correction factor
    is linearly interpolated between them (held flat outside the range), and
    corrected = round(reported * factor). This models a non-linear meter error along
    the power curve — the output of a calibration ride against a reference meter.
    """

    def __init__(self, breakpoints: list[tuple[float, float]]) -> None:
        if not breakpoints:
            raise ValueError("GridTransform needs at least one (power, factor) breakpoint")
        self._bp = sorted(breakpoints, key=lambda pf: pf[0])

    def factor(self, power: float) -> float:
        bp = self._bp
        if power <= bp[0][0]:
            return bp[0][1]
        if power >= bp[-1][0]:
            return bp[-1][1]
        for (p0, f0), (p1, f1) in zip(bp, bp[1:], strict=False):
            if p0 <= power <= p1:
                t = (power - p0) / (p1 - p0) if p1 > p0 else 0.0
                return f0 + t * (f1 - f0)
        return 1.0

    def apply(self, reading: PowerReading) -> PowerReading:
        corrected = max(0, round(reading.power_w * self.factor(reading.power_w)))
        return replace(reading, power_w=corrected)
