"""BleReplaySource — replay a captured BLE CPS stream as PowerReadings.

The BLE analogue of `sources.replay.ReplayFileSource` (which does ANT+): it reads the
`ble_notification` records for the Cycling Power Measurement characteristic, decodes
them with the CPS codec, and emits PowerReadings paced by the capture's monotonic_s.
Cadence is recovered from consecutive crank-revolution samples (as a head unit does).
Lets the BLE relay run off a real captured ride with no live meter attached.

The replay lifecycle (pacing, loop, start/stop) is shared with the ANT+ source via
`ScheduledReplaySource`; this class only parses the BLE CPS notifications.
"""

from __future__ import annotations

import json

from sb20proxy.reading import PowerReading
from sb20proxy.sources import ScheduledReplaySource

from .cps import CrankCadenceTracker, decode_cps_measurement


class BleReplaySource(ScheduledReplaySource):
    _source_prefix = "ble-replay"

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
