"""Canonical event types passed between sources and targets.

Sources convert their native protocol to PowerReading; targets convert
PowerReading to their native protocol. This keeps the source/target seam clean.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class PowerReading:
    """A single power measurement, normalised across input sources.

    Fields use Optional for things not all sources can provide.
    """

    timestamp: float
    """Monotonic seconds (time.monotonic) at which this reading was received."""

    power_w: int
    """Instantaneous power in watts."""

    cadence_rpm: int | None = None
    """Cadence in RPM, if available."""

    crank_event_count: int | None = None
    """Cumulative crank event count, if available — improves cadence accuracy."""

    accumulated_power: int | None = None
    """Cumulative accumulated-power counter, if available (ANT+ page 0x10 field)."""

    left_balance: int | None = None
    """Left-side power balance as 0..100, if available."""

    source_id: str = ""
    """Identifier of the source that emitted this reading. e.g. 'assioma:12345'."""
