"""Ride director + live dashboard for capture / calibration sessions.

A dependency-free (stdlib-only) web UI that drives our data capture and talks the
rider through a structured workout, so a calibration ride is repeatable and the
captured JSONL is cleanly segmented. The pure parts (director, workouts, live
state, page render) are host-tested; the capture/replay feed is the hardware seam.

  python code/scripts/ride_web.py --replay <capture.jsonl>          # desk demo, no stick
  python code/scripts/ride_web.py --live --stages-id N --assioma-id M --output cap.jsonl
"""

from __future__ import annotations

from .director import (
    COGGAN_ZONES,
    Cursor,
    DirectorState,
    RideDirector,
    RidePlan,
    RiderProfile,
    Segment,
    Workout,
    Zone,
    advance_cursor,
    derive_state,
)
from .state import LiveState
from .workouts import WORKOUTS

__all__ = [
    "Segment",
    "Workout",
    "RidePlan",
    "Cursor",
    "RideDirector",
    "DirectorState",
    "RiderProfile",
    "Zone",
    "COGGAN_ZONES",
    "advance_cursor",
    "derive_state",
    "LiveState",
    "WORKOUTS",
]
