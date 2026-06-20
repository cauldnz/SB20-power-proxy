"""The dynamic plan engine: RidePlan + cursor (pure core) and the live mutation
rules on LiveState (insert/delete keep the active block's identity; skip/goto/extend).

The pure functions take an injected `now`; the LiveState path uses a FakeClock, so
there are no wall-clock waits anywhere here.
"""

from __future__ import annotations

from sb20proxy.ride import LiveState, RidePlan
from sb20proxy.ride.director import Cursor, Segment, advance_cursor, derive_state

# A=[0,10) B=[10,30) C=[30,35)
PLAN = lambda: RidePlan("t", [Segment(10, "A", 100), Segment(20, "B", 200),  # noqa: E731
                              Segment(5, "C", 0)])


class FakeClock:
    def __init__(self) -> None:
        self.t = 0.0

    def __call__(self) -> float:
        return self.t


# ---- pure cursor / projection ----

def test_from_workout_copies_segments():
    from sb20proxy.ride.workouts import DEMO
    plan = RidePlan.from_workout(DEMO)
    assert plan.name == DEMO.name and plan.version == 0
    assert [s.label for s in plan.segments] == [s.label for s in DEMO.segments]
    plan.segments.append(Segment(1, "X"))  # mutating the plan must not touch the template
    assert len(DEMO.segments) != len(plan.segments)


def test_advance_not_started_is_noop():
    plan = PLAN()
    assert advance_cursor(plan, Cursor(), 100.0) == Cursor()  # started_at None


def test_advance_crosses_multiple_segments_in_one_step():
    plan = PLAN()
    cur = advance_cursor(plan, Cursor(0, 0.0), 32.0)  # past A and B -> in C
    assert cur.index == 2 and cur.started_at == 30.0


def test_advance_to_finished():
    plan = PLAN()
    cur = advance_cursor(plan, Cursor(0, 0.0), 35.0)
    assert cur.index == 3  # == len(segments) -> finished
    assert derive_state(plan, cur, 35.0).finished is True


def test_derive_mid_segment_totals():
    plan = PLAN()
    cur = advance_cursor(plan, Cursor(0, 0.0), 25.0)  # B = [10,30)
    ds = derive_state(plan, cur, 25.0)
    assert ds.segment.label == "B" and ds.seg_elapsed_s == 15.0
    assert ds.total_elapsed_s == 25.0 and ds.total_remaining_s == 10.0


# ---- LiveState live mutations (with a FakeClock) ----

def _running(clk: FakeClock) -> LiveState:
    s = LiveState(plan=PLAN(), now_fn=clk)
    s.start_ride()
    return s


def _dir(s: LiveState) -> dict:
    return s.snapshot()["director"]


def test_skip_advances_to_next_block_now():
    clk = FakeClock()
    s = _running(clk)
    clk.t = 3.0
    assert _dir(s)["label"] == "A"
    s.skip()
    d = _dir(s)
    assert d["label"] == "B" and d["seg_elapsed_s"] == 0.0


def test_goto_jumps_and_restarts_block():
    clk = FakeClock()
    s = _running(clk)
    clk.t = 2.0
    s.goto(2)
    d = _dir(s)
    assert d["seg_index"] == 2 and d["label"] == "C" and d["seg_elapsed_s"] == 0.0


def test_extend_lengthens_active_block_and_bumps_version():
    clk = FakeClock()
    s = _running(clk)
    v0 = s.snapshot()["plan_version"]
    clk.t = 5.0
    s.extend(10.0)  # A: 10 -> 20
    d = s.snapshot()
    assert d["plan_version"] == v0 + 1
    assert d["director"]["seg_remaining_s"] == 15.0  # 20 - 5
    assert d["director"]["total_s"] == 45.0


def test_insert_before_cursor_keeps_active_block_identity():
    clk = FakeClock()
    s = _running(clk)
    clk.t = 15.0  # in B (index 1)
    assert _dir(s)["label"] == "B"
    s.insert_segment(0, Segment(60, "NEW", 50))  # insert ahead of the cursor
    d = _dir(s)
    assert d["label"] == "B"  # still in B — the active block did not shift
    assert d["seg_elapsed_s"] == 5.0  # and its progress is preserved


def test_insert_after_cursor_does_not_move_cursor():
    clk = FakeClock()
    s = _running(clk)
    clk.t = 5.0  # in A
    s.insert_segment(2, Segment(60, "LATE", 999))
    assert _dir(s)["label"] == "A" and _dir(s)["seg_elapsed_s"] == 5.0


def test_delete_active_block_starts_the_next_one_fresh():
    clk = FakeClock()
    s = _running(clk)
    clk.t = 5.0  # in A (index 0)
    s.delete_segment(0)
    d = _dir(s)
    assert d["label"] == "B" and d["seg_elapsed_s"] == 0.0  # B is now active, fresh


def test_delete_before_cursor_preserves_progress():
    clk = FakeClock()
    s = _running(clk)
    clk.t = 15.0  # in B (index 1), 5s in
    s.delete_segment(0)  # remove A
    d = _dir(s)
    assert d["label"] == "B" and d["seg_elapsed_s"] == 5.0  # unchanged


def test_replace_plan_restarts_from_top_and_bumps_version():
    clk = FakeClock()
    s = _running(clk)
    v0 = s.snapshot()["plan_version"]
    clk.t = 25.0
    s.replace_plan(RidePlan("new", [Segment(40, "Z", 300)]))
    d = s.snapshot()
    assert d["plan_version"] == v0 + 1
    assert d["director"]["label"] == "Z" and d["director"]["seg_elapsed_s"] == 0.0


def test_mutations_no_op_before_ride_start():
    clk = FakeClock()
    s = LiveState(plan=PLAN(), now_fn=clk)  # not started
    s.skip()  # cursor stays not-started
    s.goto(1)
    assert _dir(s)["started"] is False
    s.append_segment(Segment(5, "D", 10))  # plan edits still allowed pre-ride
    assert _dir(s)["n_segments"] == 4
