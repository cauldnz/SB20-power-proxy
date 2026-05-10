"""Target ABC — anything that consumes PowerReadings and emits them onto a wire.

Concrete implementations:
- StagesAntTarget (Phase 1+): ANT+ master pretending to be a Stages-style power meter
- StagesBleTarget (post-v1): BLE Cycling Power Service peripheral

Targets own their I/O lifecycle. ProxyCore calls push_reading() with the most
recent input; the target decides how often to actually broadcast (driven by the
underlying radio's master cadence, not by the rate of push_reading calls).
"""

from __future__ import annotations

from abc import ABC, abstractmethod

from sb20proxy.reading import PowerReading


class PowerTarget(ABC):
    """Abstract base for anything broadcasting power data on behalf of the proxy."""

    @abstractmethod
    async def start(self) -> None:
        """Open underlying I/O (radio channel) and begin broadcasting."""

    @abstractmethod
    async def stop(self) -> None:
        """Close underlying I/O."""

    @abstractmethod
    def push_reading(self, r: PowerReading) -> None:
        """Update the target's view of 'current power'.

        The target may broadcast at its own cadence regardless of how often this
        is called. Calling more often than the broadcast cadence is harmless; the
        most recent reading wins. Calling less often than the broadcast cadence
        means the target re-broadcasts the last reading repeatedly (and may need
        to detect 'stale data' if too many of these accumulate).
        """
