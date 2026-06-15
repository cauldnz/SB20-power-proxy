"""ReplayFileSource — replay a captured JSONL stream as PowerReadings.

Phase 1 uses this to drive the proxy from a real Stages capture with no live
meter attached: it reads the committed JSONL, turns each power page into a
PowerReading, and emits them paced to the original capture timing. Phase 2's
AssiomaAntSource is the live analogue of the same seam.

This is the *decoded* replay path (PowerReading -> target re-encodes). The
*verbatim* page-replay path (re-broadcast the exact captured bytes) is a separate
concern handled target-side; see forward-plan.md §2.
"""

from __future__ import annotations

import asyncio
import json
from collections.abc import Awaitable, Callable
from pathlib import Path

from sb20proxy.ant import PAGE_POWER_ONLY
from sb20proxy.reading import PowerReading
from sb20proxy.sources import PowerSource, ReadingCallback

Sleeper = Callable[[float], Awaitable[None]]


class ReplayFileSource(PowerSource):
    """Emit PowerReadings from a captured JSONL file, paced by capture timing.

    Power comes from page 0x10 (Power-Only) records — the page that carries
    instantaneous power, cadence, balance and the accumulated-power counter.
    """

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
        path:      capture JSONL to replay.
        loop:      restart from the top when the file is exhausted.
        speed:     time multiplier (2.0 = twice as fast; large = ~no waiting).
        source_id: label stamped on each PowerReading (default 'replay:<file>').
        sleep:     awaitable delay fn (injectable for tests); defaults to asyncio.sleep.
        """
        if speed <= 0:
            raise ValueError(f"speed must be > 0, got {speed}")
        self._path = Path(path)
        self._loop = loop
        self._speed = speed
        self._source_id = source_id or f"replay:{self._path.name}"
        self._sleep: Sleeper = sleep or asyncio.sleep
        self._callbacks: list[ReadingCallback] = []
        self._task: asyncio.Task | None = None
        # Pre-parse into (delay_before_s, PowerReading) so start() is cheap and
        # the timing is testable without reading files mid-run.
        self._schedule: list[tuple[float, PowerReading]] = self._load()

    # ---- PowerSource API ----

    def on_reading(self, callback: ReadingCallback) -> None:
        self._callbacks.append(callback)

    async def start(self) -> None:
        if self._task is not None:
            raise RuntimeError("ReplayFileSource already started")
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

    # ---- introspection (handy for tests / the runnable script) ----

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
        with open(self._path) as fh:
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                rec = json.loads(line)
                if rec.get("kind") not in ("broadcast", "acknowledged"):
                    continue
                data = rec.get("data") or {}
                page = data.get("page")
                if page is None or (page & 0x7F) != PAGE_POWER_ONLY:
                    continue
                t = rec.get("monotonic_s")
                if t is None:
                    continue
                delay = 0.0 if prev_t is None else max(0.0, t - prev_t)
                prev_t = t
                schedule.append((delay, self._to_reading(t, data)))
        if not schedule:
            raise ValueError(
                f"{self._path}: no page 0x10 (power) records found to replay"
            )
        return schedule

    def _to_reading(self, t: float, data: dict) -> PowerReading:
        return PowerReading(
            timestamp=float(t),
            power_w=int(data["instantaneous_power_w"]),
            cadence_rpm=data.get("instantaneous_cadence_rpm"),
            crank_event_count=data.get("event_count"),
            accumulated_power=data.get("accumulated_power"),
            left_balance=data.get("pedal_power_balance"),
            source_id=self._source_id,
        )

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
