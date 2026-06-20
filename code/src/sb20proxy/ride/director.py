"""The ride director — a structured workout and where the rider is in it.

Two layers, both pure (no I/O, no clock of their own — the clock is injected):

* The **plan**: an immutable `Workout` template is loaded into a mutable, versioned
  `RidePlan` — the live list of segments the director runs. Edits bump `version` so
  the phone re-fetches the timeline.
* The **cursor** + projection: `advance_cursor()` and `derive_state()` are total
  functions of (plan, cursor, now). The cursor pins the active segment by wall-clock
  (`started_at`), so a live edit to a *future* segment never retro-shifts the active
  one — the flaw a pure `sum(durations)` projection would have under mid-ride edits.

`LiveState` owns the live cursor + clock and drives these under its lock; the simple
`RideDirector.state_at(elapsed)` is a thin pure projector over the same core (handy
for the static timeline and host tests). Powers are STAGES watts (what the bike/app
shows — the number the rider chases) until Phase 3 layers %FTP/zone resolution on top.
"""

from __future__ import annotations

from dataclasses import dataclass, field, replace


@dataclass(frozen=True)
class Zone:
    """A Coggan power-training zone: a %FTP band plus a representative target %FTP
    (used when a segment chases the zone rather than a specific number)."""

    id: str        # "Z1".."Z7"
    name: str
    lo_pct: float  # inclusive lower bound, as a fraction of FTP
    hi_pct: float  # exclusive upper bound (inf for Z7)
    target_pct: float  # the %FTP a zone-target segment chases


# Classic Coggan 7-zone model (fractions of FTP). Bands are [lo, hi); Z7 is open-ended.
COGGAN_ZONES: tuple[Zone, ...] = (
    Zone("Z1", "Active recovery", 0.0, 0.55, 0.50),
    Zone("Z2", "Endurance", 0.55, 0.75, 0.65),
    Zone("Z3", "Tempo", 0.75, 0.90, 0.83),
    Zone("Z4", "Threshold", 0.90, 1.05, 0.98),
    Zone("Z5", "VO2 max", 1.05, 1.20, 1.13),
    Zone("Z6", "Anaerobic", 1.20, 1.50, 1.35),
    Zone("Z7", "Neuromuscular", 1.50, float("inf"), 1.70),
)
ZONE_IDS: frozenset[str] = frozenset(z.id for z in COGGAN_ZONES)


@dataclass(frozen=True)
class RiderProfile:
    """The rider's FTP and which meter scale it's measured on. Resolves %FTP / zone
    targets to watts and classifies watts into a zone for display. Configurable at
    launch (--ftp/--scale) and live via POST /api/control/profile."""

    ftp_w: int = 250
    scale: str = "stages"

    def watts_for_pct(self, pct: float) -> int:
        return round(pct * self.ftp_w)

    def watts_for_zone(self, zone_id: str) -> int | None:
        for z in COGGAN_ZONES:
            if z.id == zone_id:
                return round(z.target_pct * self.ftp_w)
        return None

    def zone_for_watts(self, watts: int | None) -> Zone | None:
        if watts is None or self.ftp_w <= 0:
            return None
        pct = watts / self.ftp_w
        for z in COGGAN_ZONES:
            if z.lo_pct <= pct < z.hi_pct:
                return z
        return None


@dataclass(frozen=True)
class Segment:
    """One block of a workout. The target is one of: absolute `power_w` (STAGES watts,
    the number the rider chases), `pct_ftp` (e.g. 0.88), or a `zone` id ("Z4"); they
    resolve in that precedence. None across all three means 'no target' (free / coast)."""

    duration_s: float
    label: str
    power_w: int | None = None
    cadence_rpm: int | None = None
    note: str = ""
    pct_ftp: float | None = None
    zone: str | None = None

    def resolved_power_w(self, profile: RiderProfile | None = None) -> int | None:
        """The concrete target watts. Absolute `power_w` wins; otherwise %FTP / zone
        need a profile to resolve (None without one)."""
        if self.power_w is not None:
            return self.power_w
        if profile is None:
            return None
        if self.pct_ftp is not None:
            return profile.watts_for_pct(self.pct_ftp)
        if self.zone is not None:
            return profile.watts_for_zone(self.zone)
        return None


@dataclass(frozen=True)
class Workout:
    """An immutable workout template. Loaded into a `RidePlan` to run it."""

    name: str
    segments: tuple[Segment, ...]

    @property
    def total_s(self) -> float:
        return sum(s.duration_s for s in self.segments)


@dataclass
class RidePlan:
    """The live, mutable workout the director runs — an ordered list of segments plus
    a monotonic `version`. Structural edits go through `LiveState` (which also keeps
    the cursor consistent) and bump `version` so the phone re-fetches the timeline."""

    name: str
    segments: list[Segment] = field(default_factory=list)
    version: int = 0

    @classmethod
    def from_workout(cls, w: Workout) -> RidePlan:
        return cls(name=w.name, segments=list(w.segments))

    @property
    def total_s(self) -> float:
        return sum(s.duration_s for s in self.segments)


@dataclass(frozen=True)
class Cursor:
    """Where the rider is in the plan. `index` is the active segment; `started_at` is
    the clock time that segment began (None before the ride starts). Pinning the start
    time is what makes future-segment edits non-retroactive."""

    index: int = 0
    started_at: float | None = None


@dataclass(frozen=True)
class DirectorState:
    """A snapshot of where the rider is in the workout at some moment."""

    started: bool
    finished: bool
    seg_index: int                 # -1 before start; len(segments) once finished
    segment: Segment | None        # current block (None before start / when done)
    next_segment: Segment | None
    seg_elapsed_s: float
    seg_remaining_s: float
    total_elapsed_s: float
    total_remaining_s: float


def advance_cursor(plan: RidePlan, cursor: Cursor, now: float) -> Cursor:
    """Step the cursor past every active segment whose duration has elapsed by `now`,
    carrying `started_at` forward by each segment's duration (so a long gap can cross
    several short blocks). Returns the same cursor unchanged if nothing advanced."""
    if cursor.started_at is None:
        return cursor
    idx, started = cursor.index, cursor.started_at
    n = len(plan.segments)
    while idx < n and now - started >= plan.segments[idx].duration_s:
        started += plan.segments[idx].duration_s
        idx += 1
    if idx == cursor.index and started == cursor.started_at:
        return cursor
    return Cursor(index=idx, started_at=started)


def derive_state(plan: RidePlan, cursor: Cursor, now: float) -> DirectorState:
    """Project (plan, cursor, now) onto a DirectorState. Assumes the cursor has been
    advanced for `now` (so the active segment is the one in progress)."""
    segs = plan.segments
    total = plan.total_s
    first = segs[0] if segs else None
    if cursor.started_at is None:
        return DirectorState(
            started=False, finished=False, seg_index=-1, segment=None,
            next_segment=first, seg_elapsed_s=0.0, seg_remaining_s=0.0,
            total_elapsed_s=0.0, total_remaining_s=total,
        )
    idx = cursor.index
    if idx >= len(segs):
        return DirectorState(
            started=True, finished=True, seg_index=len(segs), segment=None,
            next_segment=None, seg_elapsed_s=0.0, seg_remaining_s=0.0,
            total_elapsed_s=total, total_remaining_s=0.0,
        )
    seg = segs[idx]
    done = sum(s.duration_s for s in segs[:idx])  # planned time before the active block
    seg_elapsed = min(max(0.0, now - cursor.started_at), seg.duration_s)
    total_elapsed = done + seg_elapsed
    return DirectorState(
        started=True, finished=False, seg_index=idx, segment=seg,
        next_segment=segs[idx + 1] if idx + 1 < len(segs) else None,
        seg_elapsed_s=seg_elapsed,
        seg_remaining_s=seg.duration_s - seg_elapsed,
        total_elapsed_s=total_elapsed,
        total_remaining_s=total - total_elapsed,
    )


class RideDirector:
    """A thin pure projector over a plan: `state_at(elapsed)` answers 'what should the
    rider be doing at t = elapsed?', assuming the workout started at 0 and ran without
    live edits. The live, editable path is `LiveState` (same `advance_cursor` /
    `derive_state` core); this stays the simple, total-function view for the static
    timeline and host tests."""

    def __init__(self, plan: RidePlan | Workout) -> None:
        self.plan = plan if isinstance(plan, RidePlan) else RidePlan.from_workout(plan)

    @property
    def workout(self) -> RidePlan:  # back-compat alias (.name / .segments / .total_s)
        return self.plan

    def state_at(self, elapsed_s: float, *, started: bool = True) -> DirectorState:
        if not started or elapsed_s < 0:
            return derive_state(self.plan, Cursor(index=0, started_at=None), 0.0)
        cur = advance_cursor(self.plan, Cursor(index=0, started_at=0.0), elapsed_s)
        return derive_state(self.plan, cur, elapsed_s)


# re-export so callers can `from .director import replace` if they edit segments
__all__ = [
    "Segment", "Workout", "RidePlan", "Cursor", "DirectorState", "RideDirector",
    "Zone", "RiderProfile", "COGGAN_ZONES", "ZONE_IDS",
    "advance_cursor", "derive_state", "replace",
]
