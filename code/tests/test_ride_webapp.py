"""The live-JSON view transforms are pure; the page is a static asset with essentials."""

from __future__ import annotations

from sb20proxy.ride.director import (
    Cursor,
    RidePlan,
    advance_cursor,
    derive_state,
)
from sb20proxy.ride.webapp import APP_HTML, director_view, workout_json
from sb20proxy.ride.workouts import CALIBRATION, DEMO


def _view(plan: RidePlan, started_at: float | None, now: float) -> dict:
    """Drive the pure core the way LiveState.snapshot() does, then format it."""
    cur = (advance_cursor(plan, Cursor(0, started_at), now)
           if started_at is not None else Cursor())
    return director_view(derive_state(plan, cur, now), plan)


def test_workout_json_round_trips_segments():
    wj = workout_json(CALIBRATION)
    assert wj["name"] == CALIBRATION.name
    assert wj["total_s"] == CALIBRATION.total_s
    assert wj["version"] == 0  # a Workout template has no live version
    assert len(wj["segments"]) == len(CALIBRATION.segments)
    assert any(s["power_w"] == 0 for s in wj["segments"])  # the coast block


def test_workout_json_carries_plan_version():
    plan = RidePlan.from_workout(DEMO)
    plan.version = 7
    assert workout_json(plan)["version"] == 7


def test_view_not_started():
    d = _view(RidePlan.from_workout(DEMO), None, 0.0)
    assert d["started"] is False and d["finished"] is False
    assert d["label"] == "Ready"
    assert d["target_power_w"] is None
    assert d["total_s"] == DEMO.total_s


def test_view_mid_segment():
    # DEMO: Warm-up[0,20) Tempo[20,40) Surge[40,50) Coast[50,60) Cool[60,80)
    d = _view(RidePlan.from_workout(DEMO), 0.0, 25.0)
    assert d["started"] is True and d["finished"] is False
    assert d["label"] == "Tempo" and d["target_power_w"] == 250
    assert d["seg_remaining_s"] == 15.0
    assert d["next_label"] == "Surge"


def test_view_finished():
    d = _view(RidePlan.from_workout(DEMO), 0.0, 9999.0)
    assert d["finished"] is True
    assert d["label"] == "Done" and d["total_remaining_s"] == 0.0


def test_app_html_has_the_essentials():
    for needle in ("SB20 Ride Director", "/api/live", "/api/start", "/api/workout",
                   "<canvas", "target", "Start ride"):
        assert needle in APP_HTML, f"page is missing {needle!r}"


def test_app_html_wires_the_uplift_features():
    # the Phase 4 hooks consume the live-JSON fields added in Phases 2-3
    for needle in (
        'id="banner"', "renderBanner",       # agent message banner
        "erg_setpoint_w", "d.hold", "holding",  # hold-target override
        'id="zone"', "ZCOLORS", "% FTP",      # zone chip + %FTP
        "plan_version", "lastVer",            # version-aware timeline re-fetch
    ):
        assert needle in APP_HTML, f"page is missing the {needle!r} hook"
