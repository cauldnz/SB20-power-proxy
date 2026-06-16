"""Replay a committed capture into LiveState, paced by the recorded timing.

The hardware-free feed: lets the whole dashboard + ride director be run and tested
with no ANT+ stick by replaying a real capture from findings/captures/. Handles
both multi-source captures (records tagged with `source`) and single-source ones
(via `default_source`), and both Bike-Power 0x10 and FE-C 0x19 power pages.
"""

from __future__ import annotations

import json
import threading
import time
from collections.abc import Callable
from pathlib import Path

from .state import LiveState

_POWER_PAGES = {0x10, 0x19}  # Bike Power "Power Only", FE-C "Specific Trainer Data"

# (monotonic_s, source label, power_w | None, cadence_rpm | None)
_PowerRec = tuple[float, str, int | None, int | None]


def _read_power_records(path: Path, default_source: str) -> list[_PowerRec]:
    out: list[_PowerRec] = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            if rec.get("kind") not in ("broadcast", "acknowledged"):
                continue
            data = rec.get("data") or {}
            page = data.get("page_no_toggle")
            if page is None and data.get("page") is not None:
                page = data["page"] & 0x7F
            if page not in _POWER_PAGES:
                continue
            t = rec.get("monotonic_s")
            if t is None:
                continue
            power = data.get("instantaneous_power_w")
            cadence = data.get("instantaneous_cadence_rpm")
            if power is None and cadence is None:
                continue
            out.append((float(t), rec.get("source") or default_source, power, cadence))
    return out


def replay_into(
    path: str | Path,
    state: LiveState,
    *,
    speed: float = 1.0,
    default_source: str = "stages",
    stop_event: threading.Event | None = None,
    on_done: Callable[[], None] | None = None,
    sleep: Callable[[float], None] = time.sleep,
) -> int:
    """Feed `path`'s power readings into `state`, paced by the capture's monotonic_s
    deltas (divided by `speed`). Returns the number of readings emitted. `sleep` and
    `stop_event` are injectable so the timing is testable without real waits."""
    if speed <= 0:
        raise ValueError(f"speed must be > 0, got {speed}")
    records = _read_power_records(Path(path), default_source)
    emitted = 0
    prev_t: float | None = None
    for t, source, power, cadence in records:
        if stop_event is not None and stop_event.is_set():
            break
        if prev_t is not None:
            delay = max(0.0, t - prev_t) / speed
            if delay > 0:
                sleep(delay)
        prev_t = t
        state.update(source, power, cadence)
        emitted += 1
    if on_done is not None:
        on_done()
    return emitted
