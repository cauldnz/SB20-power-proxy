"""The agent control surface: turn a parsed control request into a LiveState edit.

`apply_control()` is socket-free (it takes a LiveState + already-parsed JSON), so the
whole control vocabulary is host-tested with a FakeClock and no HTTP. The server layer
(`server.py`) only does routing, auth, and JSON I/O around it. `control_state()` is the
rich snapshot `GET /api/control/state` returns for the agent to monitor.

Vocabulary (op -> body):
  plan      {name?, segments:[seg, ...]}            replace the whole plan
  segments  {op:append|insert|replace|delete, index?, segment?}
  skip      {}                                      advance to the next block now
  goto      {index}                                 jump to a block, restart it now
  extend    {seconds}                               +/- the active block's duration
  message   {text, level?, ttl_s?}                  push a phone-banner message
  target    {power_w?, cadence_rpm?, duration_s?} | {clear:true}   ad-hoc hold override
A `seg` is {duration_s, label?, power_w?, cadence_rpm?, note?}.
"""

from __future__ import annotations

from typing import Any

from .director import RidePlan, Segment
from .state import LiveState
from .webapp import workout_json

# the ops apply_control accepts (also used by the server to validate the path)
CONTROL_OPS = frozenset(
    {"plan", "segments", "skip", "goto", "extend", "message", "target"}
)


class ControlError(ValueError):
    """A bad control request — surfaced to the agent as HTTP 400 with the message."""


def _req_index(v: Any) -> int:
    if not isinstance(v, int) or isinstance(v, bool):
        raise ControlError("index must be an integer")
    return v


def _req_number(v: Any, name: str) -> float:
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        raise ControlError(f"{name} must be a number")
    return float(v)


def _opt_number(v: Any, name: str) -> float | None:
    return None if v is None else _req_number(v, name)


def _opt_int(v: Any, name: str) -> int | None:
    if v is None:
        return None
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        raise ControlError(f"{name} must be a number")
    return int(v)


def segment_from_json(d: Any) -> Segment:
    if not isinstance(d, dict):
        raise ControlError("segment must be an object")
    if "duration_s" not in d:
        raise ControlError("segment needs a numeric duration_s")
    duration = _req_number(d["duration_s"], "duration_s")
    if duration < 0:
        raise ControlError("segment duration_s must be >= 0")
    return Segment(
        duration_s=duration,
        label=str(d.get("label", "")),
        power_w=_opt_int(d.get("power_w"), "power_w"),
        cadence_rpm=_opt_int(d.get("cadence_rpm"), "cadence_rpm"),
        note=str(d.get("note", "")),
    )


def control_state(state: LiveState) -> dict[str, Any]:
    """The agent's monitoring view: the live snapshot plus the full plan timeline."""
    snap = state.snapshot()
    snap["plan"] = workout_json(state.plan)
    return snap


def apply_control(state: LiveState, op: str, body: dict[str, Any]) -> dict[str, Any]:
    """Apply one control op to `state`; returns {ok, applied, plan_version} or raises
    ControlError. The mutation goes through LiveState so the cursor stays consistent."""
    if not isinstance(body, dict):
        raise ControlError("request body must be a JSON object")

    if op == "plan":
        segs = body.get("segments")
        if not isinstance(segs, list) or not segs:
            raise ControlError("plan needs a non-empty segments list")
        state.replace_plan(RidePlan(
            name=str(body.get("name", state.plan.name)),
            segments=[segment_from_json(s) for s in segs],
        ))
    elif op == "segments":
        sub = str(body.get("op", ""))
        if sub == "append":
            state.append_segment(segment_from_json(body.get("segment")))
        elif sub == "insert":
            state.insert_segment(_req_index(body.get("index")),
                                 segment_from_json(body.get("segment")))
        elif sub == "replace":
            state.replace_segment(_req_index(body.get("index")),
                                  segment_from_json(body.get("segment")))
        elif sub == "delete":
            state.delete_segment(_req_index(body.get("index")))
        else:
            raise ControlError(f"unknown segments op: {sub!r}")
    elif op == "skip":
        state.skip()
    elif op == "goto":
        state.goto(_req_index(body.get("index")))
    elif op == "extend":
        state.extend(_req_number(body.get("seconds"), "seconds"))
    elif op == "message":
        text = str(body.get("text", "")).strip()
        if not text:
            raise ControlError("message needs non-empty text")
        state.post_message(text, level=str(body.get("level", "info")),
                           ttl_s=_opt_number(body.get("ttl_s"), "ttl_s"))
    elif op == "target":
        if body.get("clear"):
            state.clear_hold()
        else:
            state.set_hold(
                power_w=_opt_int(body.get("power_w"), "power_w"),
                cadence_rpm=_opt_int(body.get("cadence_rpm"), "cadence_rpm"),
                duration_s=_opt_number(body.get("duration_s"), "duration_s"),
            )
    else:
        raise ControlError(f"unknown control op: {op!r}")

    return {"ok": True, "applied": op, "plan_version": state.plan.version}
