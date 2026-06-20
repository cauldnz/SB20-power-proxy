"""The agent control vocabulary: apply_control / control_state and the LiveState
message + hold-target surface. All socket-free, driven by a FakeClock.
"""

from __future__ import annotations

import pytest

from sb20proxy.ride import LiveState, RidePlan
from sb20proxy.ride.control import (
    ControlError,
    apply_control,
    control_state,
    segment_from_json,
)
from sb20proxy.ride.director import Segment


class FakeClock:
    def __init__(self) -> None:
        self.t = 0.0

    def __call__(self) -> float:
        return self.t


def _plan() -> RidePlan:
    return RidePlan("t", [Segment(10, "A", 100), Segment(20, "B", 200)])


def _state(clk: FakeClock | None = None) -> LiveState:
    return LiveState(plan=_plan(), now_fn=clk or FakeClock())


# ---- segment parsing ----

def test_segment_from_json_minimal_and_full():
    assert segment_from_json({"duration_s": 30}) == Segment(30.0, "")
    s = segment_from_json({"duration_s": 60, "label": "X", "power_w": 250,
                           "cadence_rpm": 90, "note": "go"})
    assert s == Segment(60.0, "X", 250, 90, "go")


@pytest.mark.parametrize("bad", [
    {}, {"duration_s": "x"}, {"duration_s": -5}, {"duration_s": 10, "power_w": "z"},
])
def test_segment_from_json_rejects_bad(bad):
    with pytest.raises(ControlError):
        segment_from_json(bad)


# ---- plan / segments ops ----

def test_apply_plan_replaces_and_bumps_version():
    s = _state()
    v0 = s.plan.version
    r = apply_control(s, "plan", {"name": "new",
                                  "segments": [{"duration_s": 5, "label": "Z", "power_w": 9}]})
    assert r["ok"] and r["applied"] == "plan"
    assert s.plan.name == "new" and [x.label for x in s.plan.segments] == ["Z"]
    assert s.plan.version == v0 + 1


def test_apply_plan_rejects_empty():
    with pytest.raises(ControlError):
        apply_control(_state(), "plan", {"segments": []})


def test_segments_append_insert_replace_delete():
    s = _state()
    apply_control(s, "segments", {"op": "append", "segment": {"duration_s": 5, "label": "C"}})
    assert [x.label for x in s.plan.segments] == ["A", "B", "C"]
    apply_control(s, "segments", {"op": "insert", "index": 0,
                                  "segment": {"duration_s": 1, "label": "pre"}})
    assert [x.label for x in s.plan.segments] == ["pre", "A", "B", "C"]
    apply_control(s, "segments", {"op": "replace", "index": 1,
                                  "segment": {"duration_s": 9, "label": "A2"}})
    assert s.plan.segments[1].label == "A2"
    apply_control(s, "segments", {"op": "delete", "index": 0})
    assert [x.label for x in s.plan.segments] == ["A2", "B", "C"]


@pytest.mark.parametrize("body", [
    {"op": "nope"}, {"op": "insert"}, {"op": "insert", "index": "x", "segment": {"duration_s": 1}},
])
def test_segments_bad_requests_raise(body):
    with pytest.raises(ControlError):
        apply_control(_state(), "segments", body)


# ---- cursor ops dispatch ----

def test_skip_goto_extend_dispatch():
    clk = FakeClock()
    s = _state(clk)
    s.start_ride()
    apply_control(s, "skip", {})
    assert s.snapshot()["director"]["label"] == "B"
    apply_control(s, "goto", {"index": 0})
    assert s.snapshot()["director"]["label"] == "A"
    apply_control(s, "extend", {"seconds": 10})
    assert s.snapshot()["director"]["total_s"] == 40.0  # A 10->20, B 20


def test_goto_requires_int_index():
    with pytest.raises(ControlError):
        apply_control(_state(), "goto", {"index": 1.5})


# ---- messages ----

def test_message_shows_then_expires():
    clk = FakeClock()
    s = _state(clk)
    apply_control(s, "message", {"text": "hold steady", "level": "warn", "ttl_s": 5})
    m = s.snapshot()["message"]
    assert m["text"] == "hold steady" and m["level"] == "warn"
    clk.t = 6.0
    assert s.snapshot()["message"] is None  # ttl passed


def test_latest_message_wins():
    s = _state()
    apply_control(s, "message", {"text": "first"})
    apply_control(s, "message", {"text": "second"})
    assert s.snapshot()["message"]["text"] == "second"


def test_empty_message_rejected():
    with pytest.raises(ControlError):
        apply_control(_state(), "message", {"text": "  "})


# ---- hold target + erg setpoint ----

def test_erg_setpoint_follows_segment_then_hold():
    clk = FakeClock()
    s = _state(clk)
    s.start_ride()
    assert s.snapshot()["erg_setpoint_w"] == 100  # segment A target
    apply_control(s, "target", {"power_w": 275, "cadence_rpm": 95})
    snap = s.snapshot()
    assert snap["hold"]["power_w"] == 275 and snap["hold"]["cadence_rpm"] == 95
    assert snap["erg_setpoint_w"] == 275  # hold overrides
    apply_control(s, "target", {"clear": True})
    assert s.snapshot()["hold"] is None
    assert s.snapshot()["erg_setpoint_w"] == 100  # back to the segment target


def test_hold_duration_auto_clears():
    clk = FakeClock()
    s = _state(clk)  # A=[0,10) B=[10,30)
    s.start_ride()
    apply_control(s, "target", {"power_w": 300, "duration_s": 5})
    assert s.snapshot()["hold"]["remaining_s"] == 5.0
    clk.t = 6.0  # hold expired, still inside segment A
    assert s.snapshot()["hold"] is None
    assert s.snapshot()["erg_setpoint_w"] == 100  # back to A's target


# ---- control_state + unknown op ----

def test_control_state_carries_plan_and_live():
    s = _state()
    cs = control_state(s)
    assert cs["plan"]["segments"][0]["label"] == "A"
    assert "director" in cs and "erg_setpoint_w" in cs and "cursor" in cs


def test_unknown_op_raises():
    with pytest.raises(ControlError):
        apply_control(_state(), "frobnicate", {})
