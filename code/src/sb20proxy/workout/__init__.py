"""Workout building + a session controller — the desk core behind the MCP workout tools.

`builder.build_plan()` turns a workout *spec* (a built-in name, a structured dict, or a
compact shorthand string) into a `ride.director.RidePlan`. `session.WorkoutSession` owns a
live `ride.state.LiveState` and exposes the agent-facing verbs (build / start / stop / skip /
goto / extend / set_target / status / list) as plain host-tested methods, so the eventual MCP
server (`forward-plan.md` §13) is a thin transport over this — and the same core drives the
`ftms_workout.py` interval driver.

Pure where it can be: the builder has no I/O; the session's clock is injectable so the whole
thing is tested with a fake clock and the in-process FTMS twin (no bike).
"""

from __future__ import annotations

from .builder import WorkoutSpecError, build_plan
from .session import WorkoutSession

__all__ = ["build_plan", "WorkoutSpecError", "WorkoutSession"]
