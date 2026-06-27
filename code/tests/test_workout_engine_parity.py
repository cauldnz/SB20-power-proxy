"""Parity anchor: the firmware WorkoutEngine.h must compute the same workout timeline as the
Python ride director it ports (code/src/sb20proxy/ride/director.py).

The same "4x8 Threshold" golden workout + the same sample elapsed points are encoded in the C++
host test (firmware/test/test_proxy/test_main.cpp `kGoldenWorkout` / `test_workout_*`). This test
asserts the Python reference produces the identical numbers, so a drift in either port fails CI.
"""

from sb20proxy.ride.director import RideDirector, RiderProfile, Segment, Workout

PROFILE = RiderProfile(ftp_w=285)


def _golden_workout() -> Workout:
    # mirrors kGoldenWorkout in test_main.cpp
    return Workout(
        name="4x8 Threshold",
        segments=(
            Segment(duration_s=600, label="Warm-up", pct_ftp=0.55),
            Segment(duration_s=480, label="Interval 1", power_w=250, cadence_rpm=90),
            Segment(duration_s=120, label="Recovery", power_w=90),
            Segment(duration_s=480, label="Interval 2", power_w=250),
            Segment(duration_s=120, label="Recovery", power_w=90),
        ),
    )


def test_resolved_targets_match_cpp_golden():
    w = _golden_workout()
    targets = [s.resolved_power_w(PROFILE) for s in w.segments]
    assert targets == [157, 250, 90, 250, 90]  # 0.55*285 -> 157; the rest are absolute
    assert w.total_s == 1800


def test_stepper_matches_cpp_golden():
    d = RideDirector(_golden_workout())
    # (elapsed_s, seg_index, target_w, seg_remaining_s, total_remaining_s, finished)
    cases = [
        (0, 0, 157, 600, 1800, False),
        (900, 1, 250, 180, 900, False),   # 300 s into Interval 1 (600..1080)
        (1080, 2, 90, 120, 720, False),   # start of Recovery 1
    ]
    for elapsed, idx, target, seg_rem, tot_rem, finished in cases:
        st = d.state_at(elapsed)
        assert st.seg_index == idx
        assert st.segment is not None
        assert st.segment.resolved_power_w(PROFILE) == target
        assert st.seg_remaining_s == seg_rem
        assert st.total_remaining_s == tot_rem
        assert st.finished is finished

    done = d.state_at(1800)
    assert done.finished is True
    assert done.total_remaining_s == 0
