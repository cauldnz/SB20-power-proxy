"""LiveState — the thread-safe live owner of the session.

It holds everything that changes during a ride: the latest reading per meter, the
ride clock, and the live `RidePlan` + `Cursor`. The capture/replay feed runs in one
thread (blocking openant `node.start()`, or a replay loop) and pushes readings here;
the HTTP handler thread reads a snapshot to answer GET /api/live and calls the
control methods (start/skip/edit) to answer the POSTs. A single lock guards it all.
The clock is injectable so the director timing is testable without wall-clock waits.

The director projection (advance_cursor / derive_state) is pure and lives in
`director`; this class drives it under the lock and keeps the cursor consistent
across live plan edits (insert/delete shift the active index so the rider's current
block keeps its identity). When the rider presses Start, the ride clock + cursor
begin and (optionally) a `ride_start` marker is written into the capture JSONL via
`event_sink`, so analysis can line the workout up against the recorded data.
"""

from __future__ import annotations

import threading
import time
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from .director import (
    Cursor,
    RidePlan,
    RiderProfile,
    Segment,
    advance_cursor,
    derive_state,
    replace,
)
from .webapp import director_view

EventSink = Callable[[str, dict[str, Any]], None]


@dataclass
class MeterState:
    power_w: int | None = None
    cadence_rpm: int | None = None
    count: int = 0
    last_update_s: float | None = None


class LiveState:
    def __init__(
        self,
        *,
        plan: RidePlan | None = None,
        profile: RiderProfile | None = None,
        mode: str = "",
        output: str = "",
        event_sink: EventSink | None = None,
        now_fn: Callable[[], float] = time.monotonic,
    ) -> None:
        self._lock = threading.Lock()
        self._now = now_fn
        self._meters: dict[str, MeterState] = {}
        self._messages = 0
        self._mode = mode
        self._output = output
        self._capture_running = True
        self._event_sink = event_sink
        self._ride_started_at: float | None = None
        self._plan = plan if plan is not None else RidePlan("")
        self._profile = profile if profile is not None else RiderProfile()
        self._cursor = Cursor()
        # agent control surface: coaching messages + an ad-hoc hold-target override
        self._coach: list[dict[str, Any]] = []
        self._next_msg_id = 1
        self._hold: dict[str, Any] | None = None

    # ---- written by the capture/replay thread ----

    def update(self, source: str, power_w: int | None, cadence_rpm: int | None) -> None:
        with self._lock:
            m = self._meters.setdefault(source, MeterState())
            if power_w is not None:
                m.power_w = power_w
            if cadence_rpm is not None:
                m.cadence_rpm = cadence_rpm
            m.count += 1
            m.last_update_s = self._now()
            self._messages += 1

    def set_capture_stopped(self) -> None:
        with self._lock:
            self._capture_running = False

    # ---- ride clock + cursor (driven by the rider / agent via the web UI) ----

    def start_ride(self) -> None:
        sink = None
        with self._lock:
            if self._ride_started_at is None:
                now = self._now()
                self._ride_started_at = now
                self._cursor = Cursor(index=0, started_at=now)
                sink = self._event_sink
        if sink is not None:
            sink("ride_start", {"mode": self._mode})

    def stop_ride(self) -> None:
        sink = None
        with self._lock:
            was_running = self._ride_started_at is not None
            self._ride_started_at = None
            self._cursor = Cursor()
            if was_running:
                sink = self._event_sink
        if sink is not None:
            sink("ride_stop", {})

    def ride_elapsed_s(self) -> float | None:
        with self._lock:
            if self._ride_started_at is None:
                return None
            return self._now() - self._ride_started_at

    def _sync_cursor(self, now: float) -> None:
        """Bring the cursor up to date for `now` so 'which block is active' is current
        before a cursor-relative mutation reasons about it. Caller holds the lock."""
        self._cursor = advance_cursor(self._plan, self._cursor, now)

    def skip(self) -> None:
        """Advance to the next segment now (the rider/agent skips the current block)."""
        with self._lock:
            if self._cursor.started_at is None:
                return
            now = self._now()
            self._sync_cursor(now)
            self._cursor = Cursor(index=self._cursor.index + 1, started_at=now)

    def goto(self, index: int) -> None:
        """Jump the cursor to `index` and (re)start that block now."""
        with self._lock:
            if self._cursor.started_at is None:
                return
            index = max(0, min(index, len(self._plan.segments)))
            self._cursor = Cursor(index=index, started_at=self._now())

    def extend(self, seconds: float) -> None:
        """Lengthen (or shorten) the active block by `seconds` (clamped at 0)."""
        with self._lock:
            if self._cursor.started_at is not None:
                self._sync_cursor(self._now())
            i = self._cursor.index
            if not 0 <= i < len(self._plan.segments):
                return
            seg = self._plan.segments[i]
            self._plan.segments[i] = replace(
                seg, duration_s=max(0.0, seg.duration_s + seconds)
            )
            self._plan.version += 1

    # ---- plan editing (the agent author-ahead / live-edit path) ----

    @property
    def plan(self) -> RidePlan:
        return self._plan

    def append_segment(self, seg: Segment) -> None:
        with self._lock:
            self._plan.segments.append(seg)
            self._plan.version += 1

    def insert_segment(self, index: int, seg: Segment) -> None:
        with self._lock:
            if self._cursor.started_at is not None:
                self._sync_cursor(self._now())
            index = max(0, min(index, len(self._plan.segments)))
            self._plan.segments.insert(index, seg)
            self._plan.version += 1
            # keep the active block's identity if we inserted at/ before it
            if self._cursor.started_at is not None and index <= self._cursor.index:
                self._cursor = replace(self._cursor, index=self._cursor.index + 1)

    def replace_segment(self, index: int, seg: Segment) -> None:
        with self._lock:
            if not 0 <= index < len(self._plan.segments):
                return
            self._plan.segments[index] = seg
            self._plan.version += 1

    def delete_segment(self, index: int) -> None:
        with self._lock:
            if not 0 <= index < len(self._plan.segments):
                return
            if self._cursor.started_at is not None:
                self._sync_cursor(self._now())
            del self._plan.segments[index]
            self._plan.version += 1
            if self._cursor.started_at is None:
                return
            ci = self._cursor.index
            if index < ci:
                self._cursor = replace(self._cursor, index=ci - 1)
            elif index == ci:
                # the active block vanished — start whatever now sits here, fresh
                self._cursor = Cursor(
                    index=min(ci, len(self._plan.segments)), started_at=self._now()
                )

    def replace_plan(self, plan: RidePlan) -> None:
        """Swap in a whole new plan (live swap or author-ahead). If a ride is running,
        restart it from the top of the new plan. `version` stays monotonic so the
        phone always sees a change."""
        with self._lock:
            plan.version = self._plan.version + 1
            self._plan = plan
            if self._cursor.started_at is not None:
                self._cursor = Cursor(index=0, started_at=self._now())

    # ---- agent coaching messages + ad-hoc hold-target (the control surface) ----

    def post_message(self, text: str, *, level: str = "info",
                     ttl_s: float | None = None) -> None:
        """Push a coaching message for the phone banner (latest active one wins)."""
        with self._lock:
            self._coach.append({
                "id": self._next_msg_id, "text": text, "level": level,
                "created_s": self._now(), "ttl_s": ttl_s,
            })
            self._next_msg_id += 1
            self._coach = self._coach[-20:]  # keep a short tail

    def set_hold(self, *, power_w: int | None = None, pct_ftp: float | None = None,
                 cadence_rpm: int | None = None, duration_s: float | None = None) -> None:
        """Set an ad-hoc hold-this target that supersedes the segment target on the
        phone (and the erg setpoint). Target is absolute `power_w` or `pct_ftp` (resolved
        against the profile). Optional duration auto-clears it."""
        with self._lock:
            until = None if not duration_s else self._now() + duration_s
            self._hold = {"power_w": power_w, "pct_ftp": pct_ftp,
                          "cadence_rpm": cadence_rpm, "until": until}

    def clear_hold(self) -> None:
        with self._lock:
            self._hold = None

    @property
    def profile(self) -> RiderProfile:
        return self._profile

    def set_profile(self, *, ftp_w: int | None = None, scale: str | None = None) -> None:
        """Update FTP / scale live; re-resolves every %FTP and zone target."""
        with self._lock:
            self._profile = RiderProfile(
                ftp_w=self._profile.ftp_w if ftp_w is None else ftp_w,
                scale=self._profile.scale if scale is None else scale,
            )

    def _active_message(self, now: float) -> dict[str, Any] | None:
        for msg in reversed(self._coach):
            if msg["ttl_s"] is None or now - msg["created_s"] < msg["ttl_s"]:
                return {"id": msg["id"], "text": msg["text"], "level": msg["level"],
                        "age_s": round(now - msg["created_s"], 1)}
        return None

    def _hold_view(self, now: float) -> dict[str, Any] | None:
        """The live hold override, lazily clearing it once its duration is up. Resolves
        a `pct_ftp` hold to watts against the current profile."""
        if self._hold is None:
            return None
        until = self._hold["until"]
        if until is not None and now >= until:
            self._hold = None
            return None
        power = self._hold["power_w"]
        if power is None and self._hold["pct_ftp"] is not None:
            power = self._profile.watts_for_pct(self._hold["pct_ftp"])
        return {
            "power_w": power,
            "cadence_rpm": self._hold["cadence_rpm"],
            "remaining_s": None if until is None else round(until - now, 1),
        }

    # ---- read by the HTTP handler thread ----

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            now = self._now()
            self._cursor = advance_cursor(self._plan, self._cursor, now)
            ds = derive_state(self._plan, self._cursor, now)
            meters = {
                name: {
                    "power_w": m.power_w,
                    "cadence_rpm": m.cadence_rpm,
                    "count": m.count,
                    "age_s": (round(now - m.last_update_s, 1)
                              if m.last_update_s is not None else None),
                }
                for name, m in self._meters.items()
            }
            elapsed = (None if self._ride_started_at is None
                       else round(now - self._ride_started_at, 2))
            director = director_view(ds, self._plan, self._profile)
            hold = self._hold_view(now)
            # the erg setpoint the (future) FTMS path will write: the hold override if
            # one is set, else the active segment's resolved target.
            erg = (hold["power_w"] if hold and hold["power_w"] is not None
                   else director["target_power_w"])
            return {
                "mode": self._mode,
                "output": self._output,
                "capture_running": self._capture_running,
                "messages": self._messages,
                "meters": meters,
                "ride_started": self._ride_started_at is not None,
                "ride_elapsed_s": elapsed,
                "plan_version": self._plan.version,
                "cursor": {"index": self._cursor.index,
                           "started_at": self._cursor.started_at},
                "profile": {"ftp_w": self._profile.ftp_w, "scale": self._profile.scale},
                "director": director,
                "message": self._active_message(now),
                "hold": hold,
                "erg_setpoint_w": erg,
            }
