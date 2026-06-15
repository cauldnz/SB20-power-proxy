"""PowerMeterTwin — a software ANT+ Bike Power meter (the producer side).

Broadcasts a power stream (page 0x10) as an ANT+ master, fed by push_reading, plus a
periodic manufacturer commons. It's the counterpart to BikeTwin: where BikeTwin
consumes, PowerMeterTwin produces.

The key feature for calibration work is `error` — a callable (true_power, cadence)
-> reported_power that models a meter's discrepancy. With it, a PowerMeterTwin can
broadcast power with a KNOWN non-linear error, so a correction transform can be
verified in software (does the proxy recover the true power?) before any hardware.
"""

from __future__ import annotations

from collections.abc import Callable

from sb20proxy.ant import encode_manufacturer_info, encode_power_only
from sb20proxy.ant.master import AntMaster
from sb20proxy.reading import PowerReading
from sb20proxy.targets import PowerTarget

ErrorModel = Callable[[float, "int | None"], float]


class PowerMeterTwin(PowerTarget):
    """A software power meter broadcasting page 0x10 via an AntMaster."""

    def __init__(
        self,
        master: AntMaster,
        *,
        manufacturer_id: int = 1,
        commons_every: int = 120,
        error: ErrorModel | None = None,
        name: str = "meter-twin",
    ) -> None:
        self._master = master
        self._manufacturer_id = manufacturer_id
        self._commons_every = commons_every
        # default: report the true power unchanged
        self._error: ErrorModel = error or (lambda power, cadence: power)
        self.name = name
        self._current: PowerReading | None = None
        self._event_count = 0
        self._accum_power = 0
        self._since_commons = 0
        master.set_tx_provider(self._next_page)

    def push_reading(self, r: PowerReading) -> None:
        self._current = r

    async def start(self) -> None:
        self._event_count = 0
        self._accum_power = 0
        self._since_commons = 0
        await self._master.open()

    async def stop(self) -> None:
        await self._master.close()

    def _next_page(self) -> bytes:
        if self._commons_every and self._since_commons >= self._commons_every:
            self._since_commons = 0
            return encode_manufacturer_info(
                hw_revision=1, manufacturer_id=self._manufacturer_id, model_number=1
            )
        self._since_commons += 1
        return self._power_page()

    def _power_page(self) -> bytes:
        r = self._current
        true_power = max(0, int(r.power_w)) if r else 0
        cadence = r.cadence_rpm if r else None
        reported = max(0, int(round(self._error(true_power, cadence))))
        self._event_count = (self._event_count + 1) & 0xFF
        self._accum_power = (self._accum_power + reported) & 0xFFFF
        return encode_power_only(
            event_count=self._event_count,
            instantaneous_power_w=reported & 0xFFFF,
            accumulated_power=self._accum_power,
            cadence_rpm=cadence,
        )
