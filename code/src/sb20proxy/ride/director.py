"""The ride director — a structured workout and where you are in it.

Pure (no I/O, no clock of its own): `RideDirector.state_at(elapsed_s)` is a total
function of elapsed seconds, so the whole thing is trivially host-tested. The live
clock lives in LiveState (the rider presses Start); the web layer just asks the
director "what should they be doing at t = elapsed?".
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Segment:
    """One block of a workout. `power_w` is in STAGES watts (what the bike/app
    shows — the number the rider chases). None means 'no target' (free / coast)."""

    duration_s: float
    label: str
    power_w: int | None = None
    cadence_rpm: int | None = None
    note: str = ""


@dataclass(frozen=True)
class Workout:
    name: str
    segments: tuple[Segment, ...]

    @property
    def total_s(self) -> float:
        return sum(s.duration_s for s in self.segments)


@dataclass(frozen=True)
class DirectorState:
    """A snapshot of where the rider is in the workout at some elapsed time."""

    started: bool
    finished: bool
    seg_index: int                 # -1 before start; len(segments) once finished
    segment: Segment | None        # current block (None before start / when done)
    next_segment: Segment | None
    seg_elapsed_s: float
    seg_remaining_s: float
    total_elapsed_s: float
    total_remaining_s: float


class RideDirector:
    def __init__(self, workout: Workout) -> None:
        self.workout = workout
        # cumulative end-time of each segment, for an O(n) lookup
        self._ends: list[float] = []
        acc = 0.0
        for s in workout.segments:
            acc += s.duration_s
            self._ends.append(acc)

    def state_at(self, elapsed_s: float, *, started: bool = True) -> DirectorState:
        segs = self.workout.segments
        total = self.workout.total_s
        first = segs[0] if segs else None

        if not started or elapsed_s < 0:
            return DirectorState(
                started=False, finished=False, seg_index=-1, segment=None,
                next_segment=first, seg_elapsed_s=0.0, seg_remaining_s=0.0,
                total_elapsed_s=0.0, total_remaining_s=total,
            )
        if elapsed_s >= total:
            return DirectorState(
                started=True, finished=True, seg_index=len(segs), segment=None,
                next_segment=None, seg_elapsed_s=0.0, seg_remaining_s=0.0,
                total_elapsed_s=total, total_remaining_s=0.0,
            )

        idx = next(i for i, end in enumerate(self._ends) if elapsed_s < end)
        seg = segs[idx]
        seg_start = self._ends[idx] - seg.duration_s
        seg_elapsed = elapsed_s - seg_start
        return DirectorState(
            started=True, finished=False, seg_index=idx, segment=seg,
            next_segment=segs[idx + 1] if idx + 1 < len(segs) else None,
            seg_elapsed_s=seg_elapsed,
            seg_remaining_s=seg.duration_s - seg_elapsed,
            total_elapsed_s=elapsed_s,
            total_remaining_s=total - elapsed_s,
        )
