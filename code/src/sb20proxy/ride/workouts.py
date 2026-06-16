"""Built-in workouts for the ride director.

The CALIBRATION ride is the structured form of ride_wizard.py's Session A: a spread
of steady power blocks (plus a coast for zero-power samples and two cadence
variations) chosen to sample the full meter-vs-meter curve, so the calibration
fitter (sb20proxy.calibration) has clean data across the range. Powers are STAGES
watts — the number the bike/app shows, which is what the rider chases.
"""

from __future__ import annotations

from .director import Segment, Workout

CALIBRATION = Workout(
    name="Calibration ride (meter-vs-meter)",
    segments=(
        Segment(180, "Warm-up", 140, 85, "Easy spin — let both meters wake up."),
        Segment(180, "Endurance", 200, 90, "Settle into a steady 200 W."),
        Segment(120, "Tempo", 260, 90, "Lift to a firm tempo."),
        Segment(120, "Threshold", 330, 90, "Hard — about 90% of your Stages FTP."),
        Segment(30, "Surge", 420, None, "30 s all-out, 400 W+. Push!"),
        Segment(30, "Coast", 0, 0, "STOP pedalling completely — zero-power samples."),
        Segment(120, "Grind", 200, 60, "Same 200 W, but low cadence ~60 rpm."),
        Segment(120, "Spin", 150, 98, "Easy 150 W, but high cadence ~98 rpm."),
        Segment(60, "Cool-down", 130, 85, "Easy spin to finish."),
    ),
)

# A short version for a desk demo / quick replay so it finishes in a couple of
# minutes instead of 15.
DEMO = Workout(
    name="Demo (short)",
    segments=(
        Segment(20, "Warm-up", 130, 85, "Easy spin."),
        Segment(20, "Tempo", 250, 90, "Lift to tempo."),
        Segment(10, "Surge", 400, None, "All-out!"),
        Segment(10, "Coast", 0, 0, "Stop pedalling."),
        Segment(20, "Cool-down", 140, 85, "Ease off."),
    ),
)

WORKOUTS: dict[str, Workout] = {
    "calibration": CALIBRATION,
    "demo": DEMO,
}
