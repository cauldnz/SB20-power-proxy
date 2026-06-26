"""Tests for the WorkoutSession controller (sb20proxy.workout.session).

The clock is injected so timing is deterministic; the erg drive is exercised against the
in-process FTMS twin (no bike).
"""

from __future__ import annotations

from sb20proxy.ble.ftms_erg import InProcessFtmsServer
from sb20proxy.workout.session import WorkoutSession


class Clock:
    """A manual monotonic clock for the session under test."""

    def __init__(self) -> None:
        self.t = 1000.0

    def __call__(self) -> float:
        return self.t

    def tick(self, dt: float) -> None:
        self.t += dt


def _structured(*segs):
    return {"segments": list(segs)}


# ----------------------------- build / list ----------------------------------------------


def test_build_workout_loads_and_starts():
    s = WorkoutSession(now_fn=Clock())
    st = s.build_workout(_structured(
        {"duration_s": 60, "label": "Warm-up", "power_w": 130},
        {"duration_s": 120, "label": "Work", "power_w": 250},
    ), start=True)
    assert st["ride_started"] is True
    assert st["director"]["label"] == "Warm-up"
    assert len(st["plan"]["segments"]) == 2


def test_build_workout_from_shorthand():
    s = WorkoutSession(now_fn=Clock())
    st = s.build_workout("5min @ 130W; 3x(1min @ 300W; 2min @ 100W)")
    assert len(st["plan"]["segments"]) == 1 + 3 * 2
    assert st["ride_started"] is False  # start defaults off


def test_list_workouts_covers_library():
    lib = WorkoutSession.list_workouts()
    keys = {w["key"] for w in lib}
    assert {"demo", "sweetspot", "vo2"} <= keys
    demo = next(w for w in lib if w["key"] == "demo")
    assert demo["segments"] > 0
    assert demo["total_min"] > 0


# ----------------------------- navigation ------------------------------------------------


def test_skip_advances_segment():
    clk = Clock()
    s = WorkoutSession(now_fn=clk)
    s.build_workout(_structured(
        {"duration_s": 60, "label": "A", "power_w": 100},
        {"duration_s": 60, "label": "B", "power_w": 200},
    ), start=True)
    assert s.status()["director"]["label"] == "A"
    s.skip()
    assert s.status()["director"]["label"] == "B"


def test_extend_lengthens_active_block():
    clk = Clock()
    s = WorkoutSession(now_fn=clk)
    s.build_workout(_structured({"duration_s": 60, "label": "A", "power_w": 100}), start=True)
    s.extend(30)
    seg = s.status()["plan"]["segments"][0]
    assert seg["duration_s"] == 90


def test_goto_jumps_to_block():
    clk = Clock()
    s = WorkoutSession(now_fn=clk)
    s.build_workout(_structured(
        {"duration_s": 60, "label": "A", "power_w": 100},
        {"duration_s": 60, "label": "B", "power_w": 200},
        {"duration_s": 60, "label": "C", "power_w": 300},
    ), start=True)
    s.goto(2)
    assert s.status()["director"]["label"] == "C"


# ----------------------------- erg setpoint + hold ---------------------------------------


def test_erg_setpoint_tracks_active_segment():
    s = WorkoutSession(now_fn=Clock())
    s.build_workout(_structured({"duration_s": 60, "label": "Work", "power_w": 222}), start=True)
    assert s.erg_setpoint() == 222


def test_pct_ftp_resolves_against_profile():
    s = WorkoutSession(ftp_w=250, now_fn=Clock())
    s.build_workout(_structured({"duration_s": 60, "label": "SS", "pct_ftp": 0.90}), start=True)
    assert s.erg_setpoint() == 225  # 0.90 * 250


def test_set_target_hold_overrides_segment():
    s = WorkoutSession(now_fn=Clock())
    s.build_workout(_structured({"duration_s": 60, "label": "Work", "power_w": 200}), start=True)
    s.set_target(power_w=350)
    assert s.erg_setpoint() == 350
    s.set_target(clear=True)
    assert s.erg_setpoint() == 200


def test_set_profile_rescales_pct_targets():
    s = WorkoutSession(ftp_w=200, now_fn=Clock())
    s.build_workout(_structured({"duration_s": 60, "label": "SS", "pct_ftp": 1.0}), start=True)
    assert s.erg_setpoint() == 200
    s.set_profile(ftp_w=300)
    assert s.erg_setpoint() == 300


# ----------------------------- erg drive (in-process twin) -------------------------------


def test_drive_pump_converges_to_target():
    srv = InProcessFtmsServer(power_range=(0, 1000, 1))
    s = WorkoutSession(now_fn=Clock())
    s.build_workout(_structured({"duration_s": 60, "label": "Work", "power_w": 240}), start=True)
    s.drive_pump(srv.handle)
    assert srv.controlled and srv.started
    assert srv.target_power_w == 240


def test_drive_pump_follows_target_change():
    srv = InProcessFtmsServer(power_range=(0, 1000, 1))
    s = WorkoutSession(now_fn=Clock())
    s.build_workout(_structured({"duration_s": 60, "label": "Work", "power_w": 200}), start=True)
    s.drive_pump(srv.handle)
    assert srv.target_power_w == 200
    s.set_target(power_w=420)
    s.drive_pump(srv.handle)
    assert srv.target_power_w == 420


def test_drive_clamps_to_power_range():
    srv = InProcessFtmsServer(power_range=(0, 500, 1))
    s = WorkoutSession(now_fn=Clock())
    # on the bike FtmsErgSession reads the machine's range; here we set it explicitly
    s.erg_controller.power_range = srv.power_range()
    s.build_workout(_structured({"duration_s": 60, "label": "Work", "power_w": 999}), start=True)
    s.drive_pump(srv.handle)
    assert srv.target_power_w == 500  # clamped to the machine's max


def test_release_resets_machine_and_controller():
    srv = InProcessFtmsServer(power_range=(0, 1000, 1))
    s = WorkoutSession(now_fn=Clock())
    s.build_workout(_structured({"duration_s": 60, "label": "Work", "power_w": 300}), start=True)
    s.drive_pump(srv.handle)
    assert srv.target_power_w == 300
    s.release(srv.handle)
    assert srv.controlled is False
    assert srv.target_power_w is None
    assert s.erg_controller.controlled is False
