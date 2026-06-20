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

# An FTP-relative power-zone workout: targets are expressed as %FTP / Coggan zone, so
# the absolute watts come from the rider profile (--ftp / POST /api/control/profile).
# This is the proof of the zone model; a fuller library is the project backlog.
SWEET_SPOT = Workout(
    name="Sweet-spot 2x12 (%FTP)",
    segments=(
        Segment(300, "Warm-up", cadence_rpm=90, zone="Z2", note="Easy endurance spin."),
        Segment(720, "Sweet spot 1", cadence_rpm=90, pct_ftp=0.90,
                note="Hold ~90% FTP — comfortably hard."),
        Segment(300, "Recover", cadence_rpm=90, zone="Z1", note="Spin easy."),
        Segment(720, "Sweet spot 2", cadence_rpm=90, pct_ftp=0.90,
                note="Same again — smooth and steady."),
        Segment(300, "Cool-down", cadence_rpm=88, zone="Z1", note="Ease off to finish."),
    ),
)

# ---- a small Coggan power-zone library (all %FTP / zone, resolved by the profile) ----


def _over_unders(reps: int, under_s: float, under_pct: float,
                 over_s: float, over_pct: float, cad: int) -> list[Segment]:
    """`reps` alternations of just-under-threshold then just-over-threshold."""
    out: list[Segment] = []
    for i in range(reps):
        out.append(Segment(under_s, f"Under {i + 1}", cadence_rpm=cad, pct_ftp=under_pct,
                           note="Hold just under threshold."))
        out.append(Segment(over_s, f"Over {i + 1}", cadence_rpm=cad, pct_ftp=over_pct,
                           note="Lift just over — ride the burn."))
    return out


def _on_offs(reps: int, on_s: float, on_pct: float, off_s: float,
             off_pct: float, cad_on: int) -> list[Segment]:
    """`reps` of a hard on-effort then an easy spin (e.g. VO2 30/30s)."""
    out: list[Segment] = []
    for i in range(reps):
        out.append(Segment(on_s, f"VO2 {i + 1}", cadence_rpm=cad_on, pct_ftp=on_pct,
                           note="Hard — full gas for the on."))
        out.append(Segment(off_s, f"Easy {i + 1}", pct_ftp=off_pct, note="Spin easy."))
    return out


ENDURANCE_Z2 = Workout(
    name="Endurance — Zone 2 (60 min)",
    segments=(
        Segment(600, "Warm-up", cadence_rpm=90, zone="Z2", note="Easy spin to open the legs."),
        Segment(2700, "Endurance", cadence_rpm=88, pct_ftp=0.68,
                note="Steady aerobic — stay conversational."),
        Segment(300, "Cool-down", cadence_rpm=88, zone="Z1", note="Spin down."),
    ),
)

SWEET_SPOT_2X20 = Workout(
    name="Sweet-spot 2x20 (%FTP)",
    segments=(
        Segment(600, "Warm-up", cadence_rpm=90, zone="Z2", note="Build to working effort."),
        Segment(1200, "Sweet spot 1", cadence_rpm=90, pct_ftp=0.90, note="~90% FTP, smooth."),
        Segment(360, "Recover", cadence_rpm=90, zone="Z1", note="Easy spin."),
        Segment(1200, "Sweet spot 2", cadence_rpm=90, pct_ftp=0.90, note="Same again — steady."),
        Segment(300, "Cool-down", cadence_rpm=88, zone="Z1", note="Ease off to finish."),
    ),
)

THRESHOLD_OVER_UNDER = Workout(
    name="Threshold over-unders (2 sets)",
    segments=(
        Segment(600, "Warm-up", cadence_rpm=90, zone="Z2", note="Build steadily."),
        *_over_unders(3, 120, 0.95, 60, 1.05, 92),
        Segment(300, "Recover", cadence_rpm=90, zone="Z1", note="Spin easy between sets."),
        *_over_unders(3, 120, 0.95, 60, 1.05, 92),
        Segment(300, "Cool-down", cadence_rpm=88, zone="Z1", note="Ease off."),
    ),
)

VO2_30_30 = Workout(
    name="VO2 max 30/30s (2 sets)",
    segments=(
        Segment(600, "Warm-up", cadence_rpm=95, zone="Z2", note="Include a couple of openers."),
        *_on_offs(8, 30, 1.15, 30, 0.50, 100),
        Segment(300, "Recover", cadence_rpm=90, zone="Z1", note="Easy spin between sets."),
        *_on_offs(8, 30, 1.15, 30, 0.50, 100),
        Segment(300, "Cool-down", cadence_rpm=88, zone="Z1", note="Spin down."),
    ),
)

WORKOUTS: dict[str, Workout] = {
    "calibration": CALIBRATION,
    "demo": DEMO,
    "sweetspot": SWEET_SPOT,
    "endurance": ENDURANCE_Z2,
    "sweetspot2x20": SWEET_SPOT_2X20,
    "overunder": THRESHOLD_OVER_UNDER,
    "vo2": VO2_30_30,
}
