"""ReplayFileSource — replay a captured JSONL stream as PowerReadings.

Phase 1 uses this to drive the proxy from a real Stages capture with no live
meter attached: it reads the committed JSONL, turns each power page into a
PowerReading, and emits them paced to the original capture timing. Phase 2's
AssiomaAntSource is the live analogue of the same seam.

This is the *decoded* replay path (PowerReading -> target re-encodes). The
*verbatim* page-replay path (re-broadcast the exact captured bytes) is a separate
concern handled target-side; see forward-plan.md §2.

The replay lifecycle (pacing, loop, start/stop) lives in the shared
`ScheduledReplaySource` base; this class only parses ANT+ page 0x10 records.
"""

from __future__ import annotations

import json

from sb20proxy.ant import PAGE_POWER_ONLY
from sb20proxy.reading import PowerReading
from sb20proxy.sources import ScheduledReplaySource


class ReplayFileSource(ScheduledReplaySource):
    """Emit PowerReadings from a captured JSONL file, paced by capture timing.

    Power comes from page 0x10 (Power-Only) records — the page that carries
    instantaneous power, cadence, balance and the accumulated-power counter.
    """

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
