"""Tests for the pure workout-spec builder (sb20proxy.workout.builder)."""

from __future__ import annotations

import pytest

from sb20proxy.ride.director import RidePlan
from sb20proxy.workout.builder import WorkoutSpecError, build_plan

# ----------------------------- library names ---------------------------------------------


def test_build_from_library_name():
    plan = build_plan("demo")
    assert isinstance(plan, RidePlan)
    assert plan.segments
    assert plan.name == "Demo (short)"


def test_library_name_is_case_insensitive():
    assert build_plan("SweetSpot").segments == build_plan("sweetspot").segments


def test_name_override_wins():
    assert build_plan("demo", name="My demo").name == "My demo"


# ----------------------------- structured ------------------------------------------------


def test_structured_basic():
    plan = build_plan({
        "name": "Two blocks",
        "segments": [
            {"duration_s": 300, "label": "Warm-up", "power_w": 130},
            {"duration_s": 600, "label": "Tempo", "pct_ftp": 0.85, "cadence_rpm": 90},
        ],
    })
    assert plan.name == "Two blocks"
    assert [s.duration_s for s in plan.segments] == [300, 600]
    assert plan.segments[0].power_w == 130
    assert plan.segments[1].pct_ftp == 0.85
    assert plan.segments[1].cadence_rpm == 90


def test_structured_repeat_expands_with_labels():
    plan = build_plan({
        "segments": [
            {"duration_s": 60, "label": "Warm-up", "power_w": 120},
            {"repeat": 3, "segments": [
                {"duration_s": 90, "label": "Work", "power_w": 430},
                {"duration_s": 180, "label": "Recover", "power_w": 100},
            ]},
        ],
    })
    labels = [s.label for s in plan.segments]
    assert labels == [
        "Warm-up",
        "Work 1/3", "Recover 1/3",
        "Work 2/3", "Recover 2/3",
        "Work 3/3", "Recover 3/3",
    ]
    assert len(plan.segments) == 7
    assert sum(s.duration_s for s in plan.segments) == 60 + 3 * (90 + 180)


def test_structured_nested_repeat():
    plan = build_plan({"segments": [
        {"repeat": 2, "segments": [{"duration_s": 30, "label": "On", "power_w": 400}]},
    ]})
    assert len(plan.segments) == 2
    assert all(s.power_w == 400 for s in plan.segments)


@pytest.mark.parametrize("bad", [
    {"segments": []},
    {"segments": [{"label": "no duration"}]},
    {"segments": [{"repeat": 0, "segments": [{"duration_s": 1}]}]},
    {"segments": [{"repeat": 2, "segments": []}]},
    {"segments": [{"repeat": True, "segments": [{"duration_s": 1}]}]},
    {"no_segments": 1},
])
def test_structured_rejects_malformed(bad):
    with pytest.raises(WorkoutSpecError):
        build_plan(bad)


# ----------------------------- shorthand -------------------------------------------------


def test_shorthand_simple_chain():
    plan = build_plan("5min @ 130W; 10min @ 200W; 2min @ 100W")
    assert [s.duration_s for s in plan.segments] == [300, 600, 120]
    assert [s.power_w for s in plan.segments] == [130, 200, 100]
    assert plan.segments[0].label == "130 W"


def test_shorthand_intervals_group():
    plan = build_plan("5min @ 130W; 6x(90s @ 430W; 3min @ 100W); 2min @ 100W")
    # warmup + 6*(work+recover) + cooldown = 14 segments
    assert len(plan.segments) == 1 + 6 * 2 + 1
    works = [s for s in plan.segments if s.power_w == 430]
    assert len(works) == 6
    assert sum(s.duration_s for s in plan.segments) == 300 + 6 * (90 + 180) + 120


def test_shorthand_single_step_repeat():
    plan = build_plan("4x 30s @ 400W")
    assert len(plan.segments) == 4
    assert all(s.duration_s == 30 and s.power_w == 400 for s in plan.segments)


def test_shorthand_pct_zone_coast_cadence():
    plan = build_plan("10min @ 90% 95rpm; 5min @ Z2; 30s coast")
    assert plan.segments[0].pct_ftp == 0.90
    assert plan.segments[0].cadence_rpm == 95
    assert plan.segments[0].label == "90% FTP"
    assert plan.segments[1].zone == "Z2"
    assert plan.segments[2].label == "Coast"
    assert plan.segments[2].power_w is None


@pytest.mark.parametrize("tok,secs", [
    ("90s", 90), ("90sec", 90), ("2min", 120), ("2m", 120),
    ("1h", 3600), ("1:30", 90), ("45", 45), ("0:05", 5),
])
def test_shorthand_duration_units(tok, secs):
    plan = build_plan(f"{tok} @ 200W")
    assert plan.segments[0].duration_s == secs


@pytest.mark.parametrize("bad", [
    "5min @ 130W; 6x(90s @ 430W",   # unbalanced (
    "5min @ 430bogus",              # unrecognised target token
    "@ 200W",                       # missing duration
    "xyz @ 200W",                   # bad duration
    "",                             # empty
    ";;;",                          # only separators
])
def test_shorthand_rejects_malformed(bad):
    with pytest.raises(WorkoutSpecError):
        build_plan(bad)


def test_unsupported_spec_type():
    with pytest.raises(WorkoutSpecError):
        build_plan(12345)
