"""Build a `RidePlan` from a workout spec — the pure core behind the MCP `build_workout` tool.

Three spec forms (precedence in this order):

* **name** — a string naming a built-in workout (`ride.workouts.WORKOUTS`, case-insensitive),
  e.g. ``"sweetspot"`` → that template.
* **structured** — a dict ``{"name"?: str, "segments": [node, ...]}`` where each *node* is
  either a segment object (the `ride.control.segment_from_json` shape:
  ``{duration_s, label?, power_w?|pct_ftp?|zone?, cadence_rpm?, note?}``) or a *repeat* node
  ``{"repeat": N, "segments": [node, ...]}`` that expands its body N times. This is the robust
  path an agent emits when it has already worked out the structure.
* **shorthand** — a compact string of ``;``-separated steps, each
  ``"<duration> @ <target> [<cadence>rpm]"``, with ``Nx(...)`` groups for intervals, e.g.
  ``"5min @ 130W; 6x(90s @ 430W; 3min @ 100W); 2min @ 100W"``. Targets: ``430W`` (watts),
  ``90%`` (%FTP), ``Z4`` (zone), ``coast`` (no target). Duration: ``90s`` / ``2min`` / ``1h`` /
  ``1:30`` (mm:ss) / bare seconds.

All pure: spec → `RidePlan`. No I/O, no clock.
"""

from __future__ import annotations

import re
from dataclasses import replace
from typing import Any

from ..ride.control import ControlError, segment_from_json
from ..ride.director import RidePlan, Segment, Workout
from ..ride.workouts import WORKOUTS


class WorkoutSpecError(ValueError):
    """A workout spec that can't be parsed into a plan."""


def build_plan(spec: Any, *, name: str | None = None) -> RidePlan:
    """Resolve any supported spec form into a fresh `RidePlan`. Raises `WorkoutSpecError`
    on anything malformed (so a bad spec never silently yields an empty/garbled plan)."""
    if isinstance(spec, RidePlan):
        return RidePlan(name=name or spec.name, segments=list(spec.segments))
    if isinstance(spec, Workout):
        return RidePlan(name=name or spec.name, segments=list(spec.segments))
    if isinstance(spec, dict):
        return _from_structured(spec, name)
    if isinstance(spec, str):
        key = spec.strip().lower()
        if key in WORKOUTS:
            w = WORKOUTS[key]
            return RidePlan(name=name or w.name, segments=list(w.segments))
        return _from_shorthand(spec, name)
    raise WorkoutSpecError(f"unsupported spec type: {type(spec).__name__}")


# ----------------------------- structured ------------------------------------------------


def _from_structured(spec: dict[str, Any], name: str | None) -> RidePlan:
    nodes = spec.get("segments")
    if not isinstance(nodes, list) or not nodes:
        raise WorkoutSpecError("structured spec needs a non-empty 'segments' list")
    segs: list[Segment] = []
    for node in nodes:
        segs.extend(_expand_node(node))
    if not segs:
        raise WorkoutSpecError("structured spec produced no segments")
    return RidePlan(name=name or str(spec.get("name", "Custom workout")), segments=segs)


def _expand_node(node: Any) -> list[Segment]:
    if not isinstance(node, dict):
        raise WorkoutSpecError("each segment node must be an object")
    if "repeat" in node:
        n = node["repeat"]
        if isinstance(n, bool) or not isinstance(n, int) or n < 1:
            raise WorkoutSpecError("'repeat' must be an integer >= 1")
        body_nodes = node.get("segments")
        if not isinstance(body_nodes, list) or not body_nodes:
            raise WorkoutSpecError("'repeat' needs a non-empty 'segments' list")
        body: list[Segment] = []
        for child in body_nodes:
            body.extend(_expand_node(child))
        return _repeat(body, n)
    # a leaf segment — reuse control.py's validated parser (targets, types, ranges),
    # surfacing its ControlError as our WorkoutSpecError so callers catch one type.
    try:
        return [segment_from_json(node)]
    except ControlError as e:
        raise WorkoutSpecError(str(e)) from e


def _repeat(body: list[Segment], n: int) -> list[Segment]:
    out: list[Segment] = []
    for i in range(1, n + 1):
        for seg in body:
            label = f"{seg.label} {i}/{n}" if seg.label else seg.label
            out.append(replace(seg, label=label))
    return out


# ----------------------------- shorthand -------------------------------------------------


def _from_shorthand(text: str, name: str | None) -> RidePlan:
    items = _split_top(text, ";")
    if not items:
        raise WorkoutSpecError("empty workout spec")
    segs: list[Segment] = []
    for item in items:
        segs.extend(_parse_item(item))
    if not segs:
        raise WorkoutSpecError("empty workout spec")
    return RidePlan(name=name or "Custom workout", segments=segs)


def _split_top(s: str, sep: str) -> list[str]:
    """Split on `sep` at paren depth 0 only, so ``;`` inside an ``Nx( ... )`` group stays
    with the group. Raises on unbalanced parens."""
    parts: list[str] = []
    depth = 0
    cur: list[str] = []
    for ch in s:
        if ch == "(":
            depth += 1
            cur.append(ch)
        elif ch == ")":
            depth -= 1
            if depth < 0:
                raise WorkoutSpecError("unbalanced ')' in spec")
            cur.append(ch)
        elif ch == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    if depth != 0:
        raise WorkoutSpecError("unbalanced '(' in spec")
    parts.append("".join(cur))
    return [p.strip() for p in parts if p.strip()]


def _parse_item(item: str) -> list[Segment]:
    reps = 1
    m = re.match(r"^(\d+)\s*[x×]\s*(.*)$", item, re.IGNORECASE)
    if m:
        reps = int(m.group(1))
        if reps < 1:
            raise WorkoutSpecError(f"repeat count must be >= 1 in {item!r}")
        item = m.group(2).strip()
    if item.startswith("("):
        if not item.endswith(")"):
            raise WorkoutSpecError(f"unbalanced group: {item!r}")
        body: list[Segment] = []
        for sub in _split_top(item[1:-1], ";"):
            body.extend(_parse_item(sub))
    else:
        body = [_parse_step(item)]
    return body if reps == 1 else _repeat(body, reps)


def _parse_step(step: str) -> Segment:
    # "@" is just a readability separator — drop it, then token 0 is the duration and the
    # rest are target/cadence tokens. Handles "90s@430W", "5min @ 130W", and "30s coast".
    toks = step.replace("@", " ").split()
    if not toks:
        raise WorkoutSpecError("missing duration")
    duration_s = _parse_duration(toks[0])
    power_w: int | None = None
    pct_ftp: float | None = None
    zone: str | None = None
    cadence_rpm: int | None = None
    label = "Step"
    for tok in toks[1:]:
        low = tok.lower()
        if low.endswith("rpm"):
            cadence_rpm = int(_num(low[:-3], tok))
        elif low in ("coast", "rest", "off", "free"):
            label = "Coast"
        elif low.endswith("w"):
            power_w = int(_num(low[:-1], tok))
            label = f"{power_w} W"
        elif low.endswith("%"):
            pct_ftp = _num(low[:-1], tok) / 100.0
            label = f"{round(pct_ftp * 100)}% FTP"
        elif re.fullmatch(r"z[1-7]", low):
            zone = low.upper()
            label = zone
        elif _is_number(low):
            power_w = int(_num(low, tok))
            label = f"{power_w} W"
        else:
            raise WorkoutSpecError(f"unrecognised target token {tok!r} in {step!r}")
    return Segment(duration_s=duration_s, label=label, power_w=power_w,
                   cadence_rpm=cadence_rpm, pct_ftp=pct_ftp, zone=zone)


_DUR_RE = re.compile(
    r"(\d+(?:\.\d+)?)\s*"
    r"(h|hr|hour|hours|m|min|mins|minute|minutes|s|sec|secs|second|seconds)?$"
)


def _parse_duration(tok: str) -> float:
    tok = tok.strip().lower()
    if not tok:
        raise WorkoutSpecError("missing duration")
    if ":" in tok:  # mm:ss
        mm, ss = tok.split(":", 1)
        if not (mm.isdigit() and ss.isdigit()):
            raise WorkoutSpecError(f"bad mm:ss duration {tok!r}")
        return float(int(mm) * 60 + int(ss))
    m = _DUR_RE.fullmatch(tok)
    if not m:
        raise WorkoutSpecError(f"bad duration {tok!r}")
    val = float(m.group(1))
    unit = m.group(2) or "s"
    if unit.startswith("h"):
        return val * 3600.0
    if unit.startswith("m"):
        return val * 60.0
    return val


def _num(s: str, orig: str) -> float:
    try:
        return float(s)
    except ValueError as e:
        raise WorkoutSpecError(f"bad number in token {orig!r}") from e


def _is_number(s: str) -> bool:
    try:
        float(s)
        return True
    except ValueError:
        return False
