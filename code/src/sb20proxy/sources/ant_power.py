"""AntPowerSource — a generic ANT+ Bike Power source (Phase 2).

Subscribes to a power meter's broadcasts and emits PowerReadings. It is meter-
agnostic: any standard ANT+ Bike Power device (Assioma, Rotor InPower, XCadey, ...)
broadcasts page 0x10, so the same source works for all of them — only the device id
differs.

It receives via a transport (the same seam the twins use): `AntSlaveTransport` for a
real stick, `LoopbackTransport` for software / CI testing against a `PowerMeterTwin`.
This is the live analogue of `ReplayFileSource`.
"""

from __future__ import annotations

import time

from sb20proxy.ant import PAGE_POWER_ONLY, decode_page
from sb20proxy.reading import PowerReading
from sb20proxy.sources import PowerSource, ReadingCallback
from sb20proxy.twins.transport import TwinTransport


class AntPowerSource(PowerSource):
    """Emit PowerReadings from a power meter's ANT+ broadcasts (page 0x10)."""

    def __init__(self, transport: TwinTransport, *, source_id: str = "meter") -> None:
        self._transport = transport
        self._source_id = source_id
        self._callbacks: list[ReadingCallback] = []
        transport.set_page_handler(self._on_page)

    def on_reading(self, callback: ReadingCallback) -> None:
        self._callbacks.append(callback)

    async def start(self) -> None:
        await self._transport.open()

    async def stop(self) -> None:
        await self._transport.close()

    def _on_page(self, page: bytes) -> None:
        decoded = decode_page(bytes(page))
        if (decoded.get("page") or 0) & 0x7F != PAGE_POWER_ONLY:
            return  # power comes from 0x10; other pages (torque/commons) are ignored here
        reading = PowerReading(
            timestamp=time.monotonic(),
            power_w=int(decoded["instantaneous_power_w"]),
            cadence_rpm=decoded.get("instantaneous_cadence_rpm"),
            crank_event_count=decoded.get("event_count"),
            accumulated_power=decoded.get("accumulated_power"),
            left_balance=decoded.get("pedal_power_balance"),
            source_id=self._source_id,
        )
        for callback in self._callbacks:
            callback(reading)
