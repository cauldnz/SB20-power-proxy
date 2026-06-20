"""Source ABC — anything that can produce PowerReadings.

Concrete implementations:
- AssiomaAntSource (Phase 2): subscribes to Assioma's ANT+ broadcast
- ReplayFileSource (Phase 1): replays a captured JSONL stream
- GenericAntSource (Phase 4): any standard ANT+ Bike Power meter

Sources own their I/O lifecycle. ProxyCore wires source events to a target
via on_reading() callback.
"""

from __future__ import annotations

import asyncio
from abc import ABC, abstractmethod
from collections.abc import Awaitable, Callable
from pathlib import Path

from sb20proxy.reading import PowerReading

ReadingCallback = Callable[[PowerReading], None]
Sleeper = Callable[[float], Awaitable[None]]


class PowerSource(ABC):
    """Abstract base for anything emitting PowerReadings."""

    @abstractmethod
    async def start(self) -> None:
        """Open underlying I/O (radio channel, file, etc.) and begin emitting readings."""

    @abstractmethod
    async def stop(self) -> None:
        """Close underlying I/O."""

    @abstractmethod
    def on_reading(self, callback: ReadingCallback) -> None:
        """Register a callback to be invoked for each new reading.

        Multiple callbacks may be registered. Implementations should call all of them
        for each reading. Callbacks are invoked synchronously in whatever thread/task
        produced the reading; they should be lightweight and not block.
        """


class ScheduledReplaySource(PowerSource):
    """Base for file-replay sources: pre-parse a capture into a (delay_before_s, PowerReading)
    schedule, then emit it paced by the original timing (× `speed`, optionally `loop`-ing). All the
    lifecycle is here; a subclass only implements `_load()` for its wire format (ANT+ page 0x10, BLE
    CPS notifications, …) and sets `_source_prefix` for its default `source_id` label.
    """

    _source_prefix = "replay"

    def __init__(
        self,
        path: str | Path,
        *,
        loop: bool = False,
        speed: float = 1.0,
        source_id: str | None = None,
        sleep: Sleeper | None = None,
    ) -> None:
        """
        path:      capture file to replay.
        loop:      restart from the top when the file is exhausted.
        speed:     time multiplier (2.0 = twice as fast; large = ~no waiting).
        source_id: label stamped on each PowerReading (default '<prefix>:<file>').
        sleep:     awaitable delay fn (injectable for tests); defaults to asyncio.sleep.
        """
        if speed <= 0:
            raise ValueError(f"speed must be > 0, got {speed}")
        self._path = Path(path)
        self._loop = loop
        self._speed = speed
        self._source_id = source_id or f"{self._source_prefix}:{self._path.name}"
        self._sleep: Sleeper = sleep or asyncio.sleep
        self._callbacks: list[ReadingCallback] = []
        self._task: asyncio.Task | None = None
        # Pre-parse so start() is cheap and the timing is testable without reading files mid-run.
        self._schedule: list[tuple[float, PowerReading]] = self._load()

    @abstractmethod
    def _load(self) -> list[tuple[float, PowerReading]]:
        """Parse the capture into [(delay_before_s, PowerReading), …]."""

    def on_reading(self, callback: ReadingCallback) -> None:
        self._callbacks.append(callback)

    async def start(self) -> None:
        if self._task is not None:
            raise RuntimeError(f"{type(self).__name__} already started")
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
        """Await natural completion (only returns if loop=False and the file ends)."""
        if self._task is not None:
            await self._task

    @property
    def reading_count(self) -> int:
        return len(self._schedule)

    @property
    def readings(self) -> list[PowerReading]:
        return [r for _delay, r in self._schedule]

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
