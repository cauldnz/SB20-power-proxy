"""ProxyCore — wire one source to one target and keep simple liveness stats.

The whole proxy is: a PowerSource produces PowerReadings, ProxyCore forwards each
to a PowerTarget, the target broadcasts at its own cadence. ProxyCore owns the
async lifecycle of both and records enough to answer "are we still getting data?".

Deliberately minimal (per the project's "don't optimise prematurely" rule):
recovery/dropout policy and config live in later phases.
"""

from __future__ import annotations

from dataclasses import dataclass

from sb20proxy.reading import PowerReading
from sb20proxy.sources import PowerSource
from sb20proxy.targets import PowerTarget
from sb20proxy.transform import IdentityTransform, PowerTransform


@dataclass
class ProxyStats:
    readings_forwarded: int = 0
    last_reading: PowerReading | None = None        # the corrected reading sent on
    last_source_reading: PowerReading | None = None  # the raw reading from the meter


class ProxyCore:
    """Owns a (source, target) pair and forwards readings from one to the other.

    An optional `transform` corrects each reading between source and target — this
    is where meter-to-meter calibration lives (default: pass-through).
    """

    def __init__(
        self,
        source: PowerSource,
        target: PowerTarget,
        transform: PowerTransform | None = None,
    ) -> None:
        self._source = source
        self._target = target
        self._transform = transform or IdentityTransform()
        self._stats = ProxyStats()
        self._source.on_reading(self._forward)

    def _forward(self, reading: PowerReading) -> None:
        corrected = self._transform.apply(reading)
        self._stats.readings_forwarded += 1
        self._stats.last_source_reading = reading
        self._stats.last_reading = corrected
        self._target.push_reading(corrected)

    async def start(self) -> None:
        # Target first so it is ready to receive before the source emits.
        await self._target.start()
        await self._source.start()

    async def stop(self) -> None:
        # Source first so no reading arrives at an already-closed target.
        await self._source.stop()
        await self._target.stop()

    @property
    def stats(self) -> ProxyStats:
        return self._stats
