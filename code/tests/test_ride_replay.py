"""The replay feed, exercised against a REAL committed capture (real-data-first)
plus a synthetic multi-source file that mirrors the 07_capture_multi schema."""

from __future__ import annotations

import json

from sb20proxy.ride.replay import replay_into
from sb20proxy.ride.state import LiveState

STEADY = "A-stagesL-steady-20260614-165737.jsonl"


def test_replay_real_capture_feeds_state(captures_dir):
    s = LiveState(mode="replay", now_fn=lambda: 0.0)
    delays: list[float] = []
    n = replay_into(captures_dir / STEADY, s, default_source="stages",
                    sleep=delays.append)  # record delays instead of sleeping
    assert n > 100, "A-steady should yield many power records"
    snap = s.snapshot()
    assert snap["meters"]["stages"]["count"] == n
    assert snap["messages"] == n
    assert 0 <= snap["meters"]["stages"]["power_w"] <= 2000
    # paced by capture timing: first reading immediate, so one fewer delay than readings
    assert len(delays) <= n - 1
    assert sum(delays) > 0  # the capture spans real time


def test_replay_speed_scales_the_pacing(captures_dir):
    base: list[float] = []
    fast: list[float] = []
    replay_into(captures_dir / STEADY, LiveState(now_fn=lambda: 0.0), sleep=base.append)
    replay_into(captures_dir / STEADY, LiveState(now_fn=lambda: 0.0),
                speed=10.0, sleep=fast.append)
    assert abs(sum(base) / 10.0 - sum(fast)) < 1e-3


def test_replay_multi_source_and_fec(tmp_path):
    recs = [
        {"kind": "broadcast", "monotonic_s": 0.0, "source": "stages",
         "data": {"page": 0x10, "instantaneous_power_w": 200, "instantaneous_cadence_rpm": 85}},
        {"kind": "broadcast", "monotonic_s": 0.1, "source": "assioma",
         "data": {"page": 0x10, "instantaneous_power_w": 190, "instantaneous_cadence_rpm": 85}},
        {"kind": "broadcast", "monotonic_s": 0.2, "source": "bike_fec",
         "data": {"page": 0x19, "instantaneous_power_w": 205, "instantaneous_cadence_rpm": 84}},
        {"kind": "session_end", "monotonic_s": 0.3, "messages_logged": 3},
    ]
    f = tmp_path / "multi.jsonl"
    f.write_text("\n".join(json.dumps(r) for r in recs))

    s = LiveState(now_fn=lambda: 0.0)
    n = replay_into(f, s, sleep=lambda d: None)
    assert n == 3  # session_end is skipped
    snap = s.snapshot()
    assert set(snap["meters"]) == {"stages", "assioma", "bike_fec"}
    assert snap["meters"]["assioma"]["power_w"] == 190
    assert snap["meters"]["bike_fec"]["power_w"] == 205  # FE-C 0x19 decoded too


def test_replay_single_source_uses_default_label(tmp_path):
    rec = {"kind": "broadcast", "monotonic_s": 0.0,
           "data": {"page": 0x10, "instantaneous_power_w": 123, "instantaneous_cadence_rpm": 80}}
    f = tmp_path / "single.jsonl"
    f.write_text(json.dumps(rec))
    s = LiveState(now_fn=lambda: 0.0)
    replay_into(f, s, default_source="stages", sleep=lambda d: None)
    assert s.snapshot()["meters"]["stages"]["power_w"] == 123
