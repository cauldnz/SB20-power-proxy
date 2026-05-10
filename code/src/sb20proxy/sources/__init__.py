"""Source ABC — anything that can produce PowerReadings.

Concrete implementations:
- AssiomaAntSource (Phase 2): subscribes to Assioma's ANT+ broadcast
- ReplayFileSource (Phase 1): replays a captured JSONL stream
- GenericAntSource (Phase 4): any standard ANT+ Bike Power meter

Sources own their I/O lifecycle. ProxyCore wires source events to a target
via on_reading() callback.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Callable

from sb20proxy.reading import PowerReading

ReadingCallback = Callable[[PowerReading], None]


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
