"""Power-zone / FTP model: RiderProfile resolution + zone classification, %FTP/zone
segments, and the control ops (profile, %FTP target, zone segments) that drive them.
"""

from __future__ import annotations

import pytest

from sb20proxy.ride import LiveState, RidePlan, RiderProfile
from sb20proxy.ride.control import ControlError, apply_control, segment_from_json
from sb20proxy.ride.director import Segment
from sb20proxy.ride.webapp import workout_json
from sb20proxy.ride.workouts import SWEET_SPOT


class FakeClock:
    def __init__(self) -> None:
        self.t = 0.0

    def __call__(self) -> float:
        return self.t


# ---- RiderProfile ----

def test_watts_for_pct_and_zone():
    p = RiderProfile(ftp_w=250)
    assert p.watts_for_pct(0.90) == 225
    assert p.watts_for_zone("Z4") == round(0.98 * 250)  # 245
    assert p.watts_for_zone("Z9") is None


# ftp=100 makes the Coggan band edges land on round watts
@pytest.mark.parametrize("watts,zone_id", [
    (40, "Z1"), (54, "Z1"), (55, "Z2"), (74, "Z2"), (75, "Z3"), (90, "Z4"),
    (105, "Z5"), (120, "Z6"), (150, "Z7"), (300, "Z7"),
])
def test_zone_for_watts_bands(watts, zone_id):
    assert RiderProfile(ftp_w=100).zone_for_watts(watts).id == zone_id


def test_zone_for_watts_none_cases():
    assert RiderProfile(ftp_w=250).zone_for_watts(None) is None
    assert RiderProfile(ftp_w=0).zone_for_watts(200) is None


# ---- Segment.resolved_power_w precedence ----

def test_resolved_power_precedence():
    p = RiderProfile(ftp_w=250)
    assert Segment(10, "x", power_w=200, pct_ftp=0.5, zone="Z2").resolved_power_w(p) == 200
    assert Segment(10, "x", pct_ftp=0.80).resolved_power_w(p) == 200
    assert Segment(10, "x", zone="Z2").resolved_power_w(RiderProfile(ftp_w=100)) == 65
    assert Segment(10, "x", pct_ftp=0.80).resolved_power_w(None) is None  # needs a profile
    assert Segment(10, "x").resolved_power_w(p) is None  # coast


# ---- director_view zone label via a live snapshot ----

def test_snapshot_resolves_zone_label_and_pct():
    clk = FakeClock()
    plan = RidePlan("z", [Segment(60, "SS", pct_ftp=0.90)])
    s = LiveState(plan=plan, profile=RiderProfile(ftp_w=200), now_fn=clk)
    s.start_ride()
    d = s.snapshot()["director"]
    assert d["target_power_w"] == 180  # 0.90 * 200
    assert d["zone"] == "Z4" and d["zone_name"] == "Threshold"
    assert d["target_pct_ftp"] == 0.9
    assert s.snapshot()["profile"]["ftp_w"] == 200


def test_workout_json_resolves_with_profile():
    wj = workout_json(SWEET_SPOT, RiderProfile(ftp_w=300))
    ss = next(s for s in wj["segments"] if s["label"] == "Sweet spot 1")
    assert ss["power_w"] == round(0.90 * 300)  # 270
    assert ss["zone"] == "Z4"
    # without a profile, %FTP/zone targets can't resolve
    assert workout_json(SWEET_SPOT)["segments"][1]["power_w"] is None


# ---- control ops: profile, %FTP target, zone segments ----

def test_profile_op_reresolves_targets():
    clk = FakeClock()
    plan = RidePlan("z", [Segment(60, "SS", pct_ftp=1.0)])
    s = LiveState(plan=plan, profile=RiderProfile(ftp_w=200), now_fn=clk)
    s.start_ride()
    assert s.snapshot()["director"]["target_power_w"] == 200
    apply_control(s, "profile", {"ftp_w": 320})
    assert s.snapshot()["director"]["target_power_w"] == 320
    assert s.snapshot()["profile"]["ftp_w"] == 320


def test_profile_op_rejects_nonpositive_ftp():
    with pytest.raises(ControlError):
        apply_control(LiveState(plan=RidePlan("z", [Segment(1, "a")])),
                      "profile", {"ftp_w": 0})


def test_target_pct_ftp_hold_resolves():
    clk = FakeClock()
    s = LiveState(plan=RidePlan("z", [Segment(60, "A", 100)]),
                  profile=RiderProfile(ftp_w=250), now_fn=clk)
    s.start_ride()
    apply_control(s, "target", {"pct_ftp": 1.0})
    snap = s.snapshot()
    assert snap["hold"]["power_w"] == 250  # 1.0 * FTP
    assert snap["erg_setpoint_w"] == 250


def test_segment_from_json_zone_and_pct():
    assert segment_from_json({"duration_s": 60, "zone": "Z5"}).zone == "Z5"
    assert segment_from_json({"duration_s": 60, "pct_ftp": 0.88}).pct_ftp == 0.88


def test_segment_from_json_rejects_bad_zone():
    with pytest.raises(ControlError):
        segment_from_json({"duration_s": 60, "zone": "Z9"})


def test_sweetspot_workout_registered():
    from sb20proxy.ride import WORKOUTS
    assert WORKOUTS["sweetspot"] is SWEET_SPOT
    # built from %FTP/zone, so it carries no absolute watts of its own
    assert all(s.power_w is None for s in SWEET_SPOT.segments)
