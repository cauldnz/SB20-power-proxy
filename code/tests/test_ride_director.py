"""The ride director is a pure total function of elapsed time — test it directly."""

from __future__ import annotations

from sb20proxy.ride import RideDirector, Segment, Workout
from sb20proxy.ride.workouts import CALIBRATION, WORKOUTS

# A small deterministic workout: A=[0,10) B=[10,30) C=[30,35).
W = Workout("t", (Segment(10, "A", 100, 80), Segment(20, "B", 200, 90),
                  Segment(5, "C", 0, 0)))


def test_total_s():
    assert W.total_s == 35


def test_not_started_points_at_first_segment():
    d = RideDirector(W).state_at(0.0, started=False)
    assert d.started is False and d.finished is False
    assert d.segment is None
    assert d.next_segment is not None and d.next_segment.label == "A"
    assert d.total_remaining_s == 35 and d.total_elapsed_s == 0.0


def test_first_segment():
    d = RideDirector(W).state_at(3.0)
    assert d.seg_index == 0 and d.segment is not None and d.segment.label == "A"
    assert d.seg_elapsed_s == 3.0 and d.seg_remaining_s == 7.0
    assert d.next_segment.label == "B"
    assert d.total_elapsed_s == 3.0 and d.total_remaining_s == 32.0


def test_boundary_belongs_to_the_next_segment():
    d = RideDirector(W).state_at(10.0)  # A ends at 10 -> we're in B
    assert d.seg_index == 1 and d.segment.label == "B" and d.seg_elapsed_s == 0.0


def test_mid_middle_segment():
    d = RideDirector(W).state_at(25.0)  # B = [10,30)
    assert d.segment.label == "B" and d.seg_elapsed_s == 15.0 and d.seg_remaining_s == 5.0


def test_last_segment_has_no_next():
    d = RideDirector(W).state_at(32.0)  # C = [30,35)
    assert d.segment.label == "C" and d.next_segment is None


def test_finished_at_and_past_the_end():
    d = RideDirector(W).state_at(35.0)
    assert d.finished is True and d.segment is None and d.seg_index == 3
    assert d.total_remaining_s == 0.0 and d.total_elapsed_s == 35
    assert RideDirector(W).state_at(999.0).finished is True


def test_calibration_workout_is_well_formed():
    # Has every label unique, a coast (0 W zero-power block), a hard block, and a
    # spread of targets so the meter-vs-meter fit gets data across the range.
    segs = CALIBRATION.segments
    assert len({s.label for s in segs}) == len(segs)
    powers = [s.power_w for s in segs if s.power_w is not None]
    assert 0 in powers, "needs a coast/zero-power block"
    assert max(powers) >= 330, "needs a threshold/hard block"
    assert max(powers) - min(p for p in powers if p > 0) >= 200, "needs a wide power spread"
    assert CALIBRATION.total_s == 960  # 16 min


def test_workouts_registry():
    assert "calibration" in WORKOUTS and "demo" in WORKOUTS
    assert WORKOUTS["calibration"] is CALIBRATION
