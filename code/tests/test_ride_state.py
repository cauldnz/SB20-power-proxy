"""LiveState — thread-safe handoff with an injectable clock (no wall-clock waits)."""

from __future__ import annotations

from sb20proxy.ride.state import LiveState


class FakeClock:
    def __init__(self) -> None:
        self.t = 0.0

    def __call__(self) -> float:
        return self.t


def test_update_and_snapshot_per_meter():
    clk = FakeClock()
    s = LiveState(mode="replay", output="cap.jsonl", now_fn=clk)
    clk.t = 1.0
    s.update("stages", 200, 85)
    clk.t = 2.0
    s.update("stages", 210, 86)
    s.update("assioma", 190, 85)

    snap = s.snapshot()
    assert snap["mode"] == "replay" and snap["output"] == "cap.jsonl"
    assert snap["meters"]["stages"]["power_w"] == 210
    assert snap["meters"]["stages"]["cadence_rpm"] == 86
    assert snap["meters"]["stages"]["count"] == 2
    assert snap["meters"]["assioma"]["power_w"] == 190
    assert snap["messages"] == 3
    assert snap["ride_started"] is False


def test_partial_updates_keep_last_known():
    clk = FakeClock()
    s = LiveState(now_fn=clk)
    s.update("stages", 200, 85)
    s.update("stages", None, 90)  # power missing this packet
    assert s.snapshot()["meters"]["stages"]["power_w"] == 200  # kept
    assert s.snapshot()["meters"]["stages"]["cadence_rpm"] == 90  # updated


def test_age_s():
    clk = FakeClock()
    s = LiveState(now_fn=clk)
    clk.t = 1.0
    s.update("stages", 100, 80)
    clk.t = 5.0
    assert s.snapshot()["meters"]["stages"]["age_s"] == 4.0


def test_ride_clock_start_elapsed_stop():
    clk = FakeClock()
    s = LiveState(now_fn=clk)
    assert s.ride_elapsed_s() is None
    clk.t = 10.0
    s.start_ride()
    clk.t = 15.0
    assert s.ride_elapsed_s() == 5.0
    snap = s.snapshot()
    assert snap["ride_started"] is True and snap["ride_elapsed_s"] == 5.0
    s.stop_ride()
    assert s.ride_elapsed_s() is None


def test_start_ride_is_idempotent():
    clk = FakeClock()
    s = LiveState(now_fn=clk)
    clk.t = 10.0
    s.start_ride()
    clk.t = 20.0
    s.start_ride()  # second press must NOT reset the clock
    clk.t = 25.0
    assert s.ride_elapsed_s() == 15.0


def test_event_sink_fires_on_start_and_stop():
    events: list[tuple[str, dict]] = []
    s = LiveState(mode="live", event_sink=lambda ev, meta: events.append((ev, meta)))
    s.start_ride()
    s.stop_ride()
    assert [e[0] for e in events] == ["ride_start", "ride_stop"]
    assert events[0][1]["mode"] == "live"
