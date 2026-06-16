"""BleReplaySource — replay real captured BLE CPS notifications as PowerReadings."""

from __future__ import annotations

import asyncio
import json

import pytest

from sb20proxy.ble.source import BleReplaySource

CRANK = "G-crank62144-ble-20260615-065556.jsonl"


def _recording_sleeper(into: list[float]):
    async def _sleep(d: float) -> None:
        into.append(d)
    return _sleep


@pytest.mark.asyncio
async def test_ble_replay_emits_readings_from_real_capture(captures_dir):
    delays: list[float] = []
    src = BleReplaySource(captures_dir / CRANK, sleep=_recording_sleeper(delays))
    got = []
    src.on_reading(got.append)

    await src.start()
    await src.wait()

    assert src.reading_count > 20
    assert len(got) == src.reading_count
    assert all(0 <= r.power_w <= 2000 for r in got)
    assert all(r.source_id.startswith("ble-replay:") for r in got)
    assert all(got[i].timestamp <= got[i + 1].timestamp for i in range(len(got) - 1))
    # the crank's frames carry pedal balance (flags 0x2F) -> left_balance is populated
    assert any(r.left_balance is not None for r in got)


@pytest.mark.asyncio
async def test_ble_replay_speed_scales(captures_dir):
    base: list[float] = []
    fast: list[float] = []
    s1 = BleReplaySource(captures_dir / CRANK, sleep=_recording_sleeper(base))
    s1.on_reading(lambda r: None)
    await s1.start()
    await s1.wait()
    s2 = BleReplaySource(captures_dir / CRANK, speed=10.0, sleep=_recording_sleeper(fast))
    s2.on_reading(lambda r: None)
    await s2.start()
    await s2.wait()
    assert abs(sum(base) / 10.0 - sum(fast)) < 1e-6


def test_ble_replay_missing_file_raises(tmp_path):
    with pytest.raises(FileNotFoundError):
        BleReplaySource(tmp_path / "nope.jsonl")


def test_ble_replay_rejects_capture_without_power(tmp_path):
    f = tmp_path / "no-power.jsonl"
    f.write_text(json.dumps({"kind": "ble_notification", "char": "battery_level",
                             "data": {"raw_hex": "0e"}}))
    with pytest.raises(ValueError):
        BleReplaySource(f)


@pytest.mark.asyncio
async def test_ble_replay_decodes_power_matching_capture(captures_dir, tmp_path):
    """A synthetic CPS notification (real schema) replays to the exact power value."""
    rec = {"kind": "ble_notification", "char": "cycling_power_measurement", "monotonic_s": 1.0,
           "data": {"raw_hex": "2f00870058fe5b400e0f2b", "instantaneous_power_w": 135}}
    f = tmp_path / "one.jsonl"
    f.write_text(json.dumps(rec))
    src = BleReplaySource(f, sleep=lambda d: asyncio.sleep(0))
    got = []
    src.on_reading(got.append)
    await src.start()
    await src.wait()
    assert len(got) == 1
    assert got[0].power_w == 135 and got[0].left_balance == 0x58
