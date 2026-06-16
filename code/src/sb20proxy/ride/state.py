"""LiveState — the thread-safe handoff between the capture/replay feed and the web.

The capture runs in one thread (blocking openant `node.start()`, or a replay loop)
and pushes the latest reading per meter here; the HTTP handler thread reads a
snapshot to answer GET /api/live. A single lock guards everything. The clock is
injectable so the director timing is testable without wall-clock waits.

When the rider presses Start, the ride clock begins and (optionally) a `ride_start`
marker is written into the capture JSONL via `event_sink`, so the analysis can line
the workout segments up against the recorded data.
"""

from __future__ import annotations

import threading
import time
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

EventSink = Callable[[str, dict[str, Any]], None]


@dataclass
class MeterState:
    power_w: int | None = None
    cadence_rpm: int | None = None
    count: int = 0
    last_update_s: float | None = None


class LiveState:
    def __init__(
        self,
        *,
        mode: str = "",
        output: str = "",
        event_sink: EventSink | None = None,
        now_fn: Callable[[], float] = time.monotonic,
    ) -> None:
        self._lock = threading.Lock()
        self._now = now_fn
        self._meters: dict[str, MeterState] = {}
        self._messages = 0
        self._mode = mode
        self._output = output
        self._capture_running = True
        self._event_sink = event_sink
        self._ride_started_at: float | None = None

    # ---- written by the capture/replay thread ----

    def update(self, source: str, power_w: int | None, cadence_rpm: int | None) -> None:
        with self._lock:
            m = self._meters.setdefault(source, MeterState())
            if power_w is not None:
                m.power_w = power_w
            if cadence_rpm is not None:
                m.cadence_rpm = cadence_rpm
            m.count += 1
            m.last_update_s = self._now()
            self._messages += 1

    def set_capture_stopped(self) -> None:
        with self._lock:
            self._capture_running = False

    # ---- ride clock (driven by the rider via the web UI) ----

    def start_ride(self) -> None:
        sink = None
        with self._lock:
            if self._ride_started_at is None:
                self._ride_started_at = self._now()
                sink = self._event_sink
        if sink is not None:
            sink("ride_start", {"mode": self._mode})

    def stop_ride(self) -> None:
        sink = None
        with self._lock:
            was_running = self._ride_started_at is not None
            self._ride_started_at = None
            if was_running:
                sink = self._event_sink
        if sink is not None:
            sink("ride_stop", {})

    def ride_elapsed_s(self) -> float | None:
        with self._lock:
            if self._ride_started_at is None:
                return None
            return self._now() - self._ride_started_at

    # ---- read by the HTTP handler thread ----

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            now = self._now()
            meters = {
                name: {
                    "power_w": m.power_w,
                    "cadence_rpm": m.cadence_rpm,
                    "count": m.count,
                    "age_s": (round(now - m.last_update_s, 1)
                              if m.last_update_s is not None else None),
                }
                for name, m in self._meters.items()
            }
            elapsed = (None if self._ride_started_at is None
                       else round(now - self._ride_started_at, 2))
            return {
                "mode": self._mode,
                "output": self._output,
                "capture_running": self._capture_running,
                "messages": self._messages,
                "meters": meters,
                "ride_started": self._ride_started_at is not None,
                "ride_elapsed_s": elapsed,
            }
