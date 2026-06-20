"""The Coggan power-zone workout library is well-formed: every segment resolves to a
sane target against a profile, and the interval structures are what they claim to be.
"""

from __future__ import annotations

import pytest

from sb20proxy.ride import WORKOUTS, RiderProfile
from sb20proxy.ride.workouts import (
    ENDURANCE_Z2,
    SWEET_SPOT_2X20,
    THRESHOLD_OVER_UNDER,
    VO2_30_30,
)

ZONE_WORKOUTS = [ENDURANCE_Z2, SWEET_SPOT_2X20, THRESHOLD_OVER_UNDER, VO2_30_30]
P = RiderProfile(ftp_w=250)


@pytest.mark.parametrize("w", ZONE_WORKOUTS, ids=lambda w: w.name)
def test_every_segment_resolves_to_a_positive_target(w):
    # these are training workouts (no coast) — each block must resolve to real watts
    for s in w.segments:
        watts = s.resolved_power_w(P)
        assert watts is not None and watts > 0, f"{w.name}: {s.label} did not resolve"
    assert w.total_s >= 20 * 60, f"{w.name} is implausibly short"
    # interval workouts repeat block labels across sets — just require they're labelled
    assert all(s.label for s in w.segments), "every block must be labelled"


@pytest.mark.parametrize("w", ZONE_WORKOUTS, ids=lambda w: w.name)
def test_registered(w):
    assert w in WORKOUTS.values()


def test_over_unders_straddle_threshold():
    overs = [s for s in THRESHOLD_OVER_UNDER.segments if s.label.startswith("Over")]
    unders = [s for s in THRESHOLD_OVER_UNDER.segments if s.label.startswith("Under")]
    assert len(overs) == 6 and len(unders) == 6  # 2 sets x 3 reps
    assert all(s.resolved_power_w(P) > P.ftp_w for s in overs)   # above FTP
    assert all(s.resolved_power_w(P) < P.ftp_w for s in unders)  # below FTP


def test_vo2_intervals_are_supra_threshold():
    ons = [s for s in VO2_30_30.segments if s.label.startswith("VO2")]
    assert len(ons) == 16  # 2 sets x 8
    # 120% FTP lands in Z5 (VO2 max) at any FTP
    assert all(P.zone_for_watts(s.resolved_power_w(P)).id == "Z5" for s in ons)


def test_calibration_grid_is_a_well_formed_grid():
    from sb20proxy.ride.workouts import CALIBRATION_GRID
    segs = CALIBRATION_GRID.segments
    assert WORKOUTS["calgrid"] is CALIBRATION_GRID
    # every block resolves to a non-negative target (coast = 0 W; the rest are %FTP)
    watts = [s.resolved_power_w(P) for s in segs]
    assert all(w is not None and w >= 0 for w in watts)
    # a zero-power coast point (the meter offset / zero)
    assert any(s.power_w == 0 for s in segs)
    # a power spine spanning low -> over-threshold (>= 6 steady points)
    spine = [s.resolved_power_w(P) for s in segs if s.label.startswith("P ")]
    assert len(spine) >= 6
    assert min(spine) <= 0.45 * P.ftp_w and max(spine) >= 1.05 * P.ftp_w
    # cadence rows at a fixed power, >= 3 distinct cadences (cadence-dependence probe)
    cad_at_70 = sorted({s.cadence_rpm for s in segs if s.label.startswith("C 70%")})
    assert len(cad_at_70) >= 3
    # plausible duration for a calibration ride
    assert 18 * 60 <= CALIBRATION_GRID.total_s <= 30 * 60
