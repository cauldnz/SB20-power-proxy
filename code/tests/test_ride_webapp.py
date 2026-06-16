"""The live-JSON transforms are pure; the page is a static asset with essentials."""

from __future__ import annotations

from sb20proxy.ride import RideDirector
from sb20proxy.ride.webapp import APP_HTML, render_live, workout_json
from sb20proxy.ride.workouts import CALIBRATION, DEMO


def _snap(**over):
    base = {
        "mode": "replay", "output": "x.jsonl", "capture_running": True, "messages": 3,
        "meters": {"stages": {"power_w": 150, "cadence_rpm": 85, "count": 3, "age_s": 0.1}},
        "ride_started": False, "ride_elapsed_s": None,
    }
    base.update(over)
    return base


def test_workout_json_round_trips_segments():
    wj = workout_json(CALIBRATION)
    assert wj["name"] == CALIBRATION.name
    assert wj["total_s"] == CALIBRATION.total_s
    assert len(wj["segments"]) == len(CALIBRATION.segments)
    assert any(s["power_w"] == 0 for s in wj["segments"])  # the coast block


def test_render_live_not_started():
    out = render_live(_snap(), RideDirector(DEMO))
    assert out["meters"]["stages"]["power_w"] == 150  # snapshot passed through
    d = out["director"]
    assert d["started"] is False and d["finished"] is False
    assert d["label"] == "Ready"
    assert d["target_power_w"] is None
    assert d["total_s"] == DEMO.total_s


def test_render_live_mid_segment():
    # DEMO: Warm-up[0,20) Tempo[20,40) Surge[40,50) Coast[50,60) Cool[60,80)
    d = render_live(_snap(ride_started=True, ride_elapsed_s=25.0), RideDirector(DEMO))["director"]
    assert d["started"] is True and d["finished"] is False
    assert d["label"] == "Tempo" and d["target_power_w"] == 250
    assert d["seg_remaining_s"] == 15.0
    assert d["next_label"] == "Surge"


def test_render_live_finished():
    d = render_live(_snap(ride_started=True, ride_elapsed_s=9999.0), RideDirector(DEMO))["director"]
    assert d["finished"] is True
    assert d["label"] == "Done" and d["total_remaining_s"] == 0.0


def test_app_html_has_the_essentials():
    for needle in ("SB20 Ride Director", "/api/live", "/api/start", "/api/workout",
                   "<canvas", "target", "Start ride"):
        assert needle in APP_HTML, f"page is missing {needle!r}"
