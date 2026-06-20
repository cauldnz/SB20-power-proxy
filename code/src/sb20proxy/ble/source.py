"""BleReplaySource — replay a captured BLE CPS stream as PowerReadings.

The BLE analogue of `sources.replay.ReplayFileSource` (which does ANT+): it reads the
`ble_notification` records for the Cycling Power Measurement characteristic, decodes
them with the CPS codec, and emits PowerReadings paced by the capture's monotonic_s.
Cadence is recovered from consecutive crank-revolution samples (as a head unit does).
Lets the BLE relay run off a real captured ride with no live meter attached.
"""

from __future__ import annotations

import asyncio
import json
from collections.abc import Awaitable, Callable
from pathlib import Path

from sb20proxy.reading import PowerReading
from sb20proxy.sources import PowerSource, ReadingCallback

from .cps import CrankCadenceTracker, decode_cps_measurement

Sleeper = Callable[[float], Awaitable[None]]


class BleReplaySource(PowerSource):
    def __init__(
        self,
        path: str | Path,
        *,
        loop: bool = False,
        speed: float = 1.0,
        source_id: str | None = None,
        sleep: Sleeper | None = None,
    ) -> None:
        if speed <= 0:
            raise ValueError(f"speed must be > 0, got {speed}")
        self._path = Path(path)
        self._loop = loop
        self._speed = speed
        self._source_id = source_id or f"ble-replay:{self._path.name}"
        self._sleep: Sleeper = sleep or asyncio.sleep
        self._callbacks: list[ReadingCallback] = []
        self._task: asyncio.Task | None = None
        self._schedule: list[tuple[float, PowerReading]] = self._load()

    # ---- PowerSource API ----

    def on_reading(self, callback: ReadingCallback) -> None:
        self._callbacks.append(callback)

    async def start(self) -> None:
        if self._task is not None:
            raise RuntimeError("BleReplaySource already started")
        self._task = asyncio.create_task(self._run())

    async def stop(self) -> None:
        if self._task is None:
            return
        self._task.cancel()
        try:
            await self._task
        except asyncio.CancelledError:
            pass
        self._task = None

    async def wait(self) -> None:
        if self._task is not None:
            await self._task

    @property
    def reading_count(self) -> int:
        return len(self._schedule)

    @property
    def readings(self) -> list[PowerReading]:
        return [r for _delay, r in self._schedule]

    # ---- internals ----

    def _load(self) -> list[tuple[float, PowerReading]]:
        if not self._path.exists():
            raise FileNotFoundError(f"capture file not found: {self._path}")
        schedule: list[tuple[float, PowerReading]] = []
        prev_t: float | None = None
        crank = CrankCadenceTracker()
        with open(self._path) as fh:
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                rec = json.loads(line)
                if (rec.get("kind") != "ble_notification"
                        or rec.get("char") != "cycling_power_measurement"):
                    continue
                data = rec.get("data") or {}
                raw = data.get("raw_hex")
                t = rec.get("monotonic_s")
                if not raw or t is None:
                    continue
                m = decode_cps_measurement(bytes.fromhex(raw))
                cadence: int | None = None
                if m.cumulative_crank_revs is not None and m.last_crank_event_time is not None:
                    cadence = crank.update(m.cumulative_crank_revs, m.last_crank_event_time)
                delay = 0.0 if prev_t is None else max(0.0, t - prev_t)
                prev_t = t
                schedule.append((delay, PowerReading(
                    timestamp=float(t), power_w=m.power_w, cadence_rpm=cadence,
                    crank_event_count=m.cumulative_crank_revs,
                    left_balance=m.pedal_balance, source_id=self._source_id,
                )))
        if not schedule:
            raise ValueError(f"{self._path}: no CPS power notifications to replay")
        return schedule

    def _emit(self, reading: PowerReading) -> None:
        for cb in self._callbacks:
            cb(reading)

    async def _run(self) -> None:
        while True:
            for delay, reading in self._schedule:
                if delay > 0:
                    await self._sleep(delay / self._speed)
                self._emit(reading)
            if not self._loop:
                return
