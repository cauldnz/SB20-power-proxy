"""Tests for the decoded replay path: ReplayFileSource + ProxyCore wiring.

ReplayFileSource is exercised against a REAL committed capture (timing verified
via an injected sleeper, no wall-clock waiting). ProxyCore is exercised with
fakes. The loop-mode test uses a tiny synthetic file that mirrors the real JSONL
schema exactly (synthetic values, observed schema).
"""

from __future__ import annotations

import asyncio
import json
from pathlib import Path

import pytest

from sb20proxy.core import ProxyCore
from sb20proxy.reading import PowerReading
from sb20proxy.sources import PowerSource
from sb20proxy.sources.replay import ReplayFileSource
from sb20proxy.targets import PowerTarget

CAPTURE = (
    Path(__file__).resolve().parents[1]
    / "findings" / "captures" / "A-stagesL-steady-20260614-165737.jsonl"
)


# --------------------------- ReplayFileSource ---------------------------

@pytest.mark.asyncio
async def test_replay_emits_power_readings_in_capture_order():
    delays: list[float] = []

    async def fake_sleep(d: float) -> None:
        delays.append(d)

    src = ReplayFileSource(CAPTURE, sleep=fake_sleep)
    got: list[PowerReading] = []
    src.on_reading(got.append)

    await src.start()
    await src.wait()

    assert src.reading_count > 100, "A-steady should yield many 0x10 power records"
    assert len(got) == src.reading_count
    # Values are sane and timestamps are non-decreasing (real capture order).
    assert all(0 <= r.power_w <= 2000 for r in got)
    assert all(got[i].timestamp <= got[i + 1].timestamp for i in range(len(got) - 1))
    assert all(r.source_id.startswith("replay:") for r in got)
    # First record emits immediately (no sleep); total slept == capture span.
    assert len(delays) <= src.reading_count - 1
    assert abs(sum(delays) - (got[-1].timestamp - got[0].timestamp)) < 1e-6


def _recording_sleeper(into: list[float]):
    async def _sleep(d: float) -> None:
        into.append(d)
    return _sleep


async def _drain(src: ReplayFileSource) -> None:
    src.on_reading(lambda r: None)
    await src.start()
    await src.wait()


@pytest.mark.asyncio
async def test_replay_speed_scales_delays():
    base: list[float] = []
    fast: list[float] = []
    await _drain(ReplayFileSource(CAPTURE, sleep=_recording_sleeper(base)))
    await _drain(ReplayFileSource(CAPTURE, speed=10.0, sleep=_recording_sleeper(fast)))
    assert abs(sum(base) / 10.0 - sum(fast)) < 1e-6


def test_replay_rejects_file_without_power_pages(tmp_path):
    f = tmp_path / "no-power.jsonl"
    f.write_text(json.dumps({"kind": "broadcast", "monotonic_s": 0.0,
                             "data": {"page": 0x50, "manufacturer_id": 69}}) + "\n")
    with pytest.raises(ValueError):
        ReplayFileSource(f)


def test_replay_missing_file_raises(tmp_path):
    with pytest.raises(FileNotFoundError):
        ReplayFileSource(tmp_path / "nope.jsonl")


@pytest.mark.asyncio
async def test_replay_loops(tmp_path):
    # Synthetic file, REAL schema (kind / data.page / monotonic_s / 0x10 fields).
    recs = [
        {"kind": "broadcast", "monotonic_s": 0.0,
         "data": {"page": 0x10, "instantaneous_power_w": 100, "instantaneous_cadence_rpm": 80,
                  "event_count": 1, "accumulated_power": 100, "pedal_power_balance": 50}},
        {"kind": "broadcast", "monotonic_s": 1.0,
         "data": {"page": 0x10, "instantaneous_power_w": 110, "instantaneous_cadence_rpm": 81,
                  "event_count": 2, "accumulated_power": 210, "pedal_power_balance": 50}},
    ]
    f = tmp_path / "mini.jsonl"
    f.write_text("\n".join(json.dumps(r) for r in recs))

    src = ReplayFileSource(f, loop=True, sleep=lambda d: asyncio.sleep(0))
    got: list[PowerReading] = []
    src.on_reading(got.append)
    await src.start()
    for _ in range(50):
        if len(got) >= 5:
            break
        await asyncio.sleep(0)
    await src.stop()

    assert len(got) >= 5, "loop=True should re-emit past the end of the file"
    assert got[0].power_w == 100 and got[2].power_w == 100, "should wrap to the top"


# ------------------------------- ProxyCore -------------------------------

class _FakeSource(PowerSource):
    def __init__(self, readings: list[PowerReading]) -> None:
        self._readings = readings
        self._cbs: list = []
    def on_reading(self, cb) -> None:
        self._cbs.append(cb)
    async def start(self) -> None:
        for r in self._readings:
            for cb in self._cbs:
                cb(r)
    async def stop(self) -> None:
        pass


class _FakeTarget(PowerTarget):
    def __init__(self) -> None:
        self.received: list[PowerReading] = []
        self.started = False
        self.stopped = False
        self.started_before_first_reading: bool | None = None
    async def start(self) -> None:
        self.started = True
    async def stop(self) -> None:
        self.stopped = True
    def push_reading(self, r: PowerReading) -> None:
        if not self.received:
            self.started_before_first_reading = self.started
        self.received.append(r)


@pytest.mark.asyncio
async def test_proxycore_forwards_every_reading_in_order():
    readings = [PowerReading(timestamp=float(i), power_w=100 + i, source_id="fake")
                for i in range(5)]
    src = _FakeSource(readings)
    tgt = _FakeTarget()
    core = ProxyCore(src, tgt)

    await core.start()

    assert tgt.received == readings
    assert tgt.started_before_first_reading is True  # target ready before data
    assert core.stats.readings_forwarded == 5
    assert core.stats.last_reading == readings[-1]

    await core.stop()
    assert tgt.stopped
