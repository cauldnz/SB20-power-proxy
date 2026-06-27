"""Import Zwift ``.zwo`` / Garmin ``.fit`` workouts into the canonical on-device workout JSON the
firmware WorkoutEngine consumes (``findings/on-device-workout-engine.md``).

The pipeline is **import format → `ride.director.Workout` (the shared IR) → device JSON**:

* `from_zwo(xml)` — parse a Zwift workout (stdlib XML; the de-facto %FTP authoring standard).
* `from_fit(path)` — parse a Garmin workout (lazy `fitparse`, the ``[analysis]`` extra). The pure
  step→segment mapping (`fit_steps_to_segments`) is host-tested with synthetic steps; the FIT
  *power* decode is best-effort until validated against a real exported file (real-data-first).
* `to_device_json(workout, ftp_w)` — emit the canonical device JSON (POST it to ``/workout/load``).

Reuses the existing `Segment`/`Workout` types so it shares the engine's semantics, not a fork.
"""

from __future__ import annotations

import json
import xml.etree.ElementTree as ET
from typing import Any

from ..ride.director import Segment, Workout


class WorkoutImportError(ValueError):
    """An external workout file that can't be parsed into a Workout."""


def _f(el: ET.Element, attr: str) -> float | None:
    v = el.get(attr)
    return float(v) if v is not None else None


def _cad(el: ET.Element, attr: str = "Cadence") -> int | None:
    v = el.get(attr)
    return int(round(float(v))) if v else None


def from_zwo(xml_text: str, *, name: str | None = None) -> Workout:
    """Parse a Zwift ``.zwo`` workout into a Workout. Ramps (Warmup/Cooldown/Ramp) are flattened
    to a single steady segment at the average %FTP — the device engine holds steady targets, not
    ramps. ``IntervalsT`` expands to Repeat×(on, off); ``FreeRide`` → an untargeted block."""
    try:
        root = ET.fromstring(xml_text)
    except ET.ParseError as e:
        raise WorkoutImportError(f"not valid XML: {e}") from e
    wname = (name or (root.findtext("name") or "").strip() or "Imported workout")
    body = root.find("workout")
    if body is None:
        raise WorkoutImportError("no <workout> element")

    segs: list[Segment] = []
    for el in body:
        tag = el.tag
        if tag in ("Warmup", "Cooldown", "Ramp"):
            lo = _f(el, "PowerLow")
            hi = _f(el, "PowerHigh")
            pct = _f(el, "Power")
            if pct is None:
                lo = lo if lo is not None else 0.0
                hi = hi if hi is not None else lo
                pct = round((lo + hi) / 2.0, 4)
            label = {"Warmup": "Warm-up", "Cooldown": "Cool-down", "Ramp": "Ramp"}[tag]
            segs.append(Segment(duration_s=int(_f(el, "Duration") or 0), label=label,
                                pct_ftp=round(pct, 4), cadence_rpm=_cad(el)))
        elif tag == "SteadyState":
            segs.append(Segment(duration_s=int(_f(el, "Duration") or 0), label="Steady",
                                pct_ftp=round(_f(el, "Power") or 0.0, 4), cadence_rpm=_cad(el)))
        elif tag == "IntervalsT":
            rep = int(_f(el, "Repeat") or 1)
            on = int(_f(el, "OnDuration") or 0)
            off = int(_f(el, "OffDuration") or 0)
            on_p = round(_f(el, "OnPower") or 0.0, 4)
            off_p = round(_f(el, "OffPower") or 0.0, 4)
            on_cad = _cad(el)
            off_cad = _cad(el, "CadenceResting")
            for i in range(rep):
                segs.append(Segment(duration_s=on, label=f"Interval {i + 1}",
                                    pct_ftp=on_p, cadence_rpm=on_cad))
                segs.append(Segment(duration_s=off, label="Recovery",
                                    pct_ftp=off_p, cadence_rpm=off_cad))
        elif tag == "FreeRide":
            segs.append(Segment(duration_s=int(_f(el, "Duration") or 0), label="Free ride"))
        # other tags (textevent, etc.) carry no power block — ignore.

    segs = [s for s in segs if s.duration_s > 0]
    if not segs:
        raise WorkoutImportError("no usable segments in the .zwo")
    return Workout(name=wname, segments=tuple(segs))


def fit_steps_to_segments(steps: list[dict[str, Any]]) -> list[Segment]:
    """Pure: map normalized FIT workout steps to Segments. Each step dict carries:
    ``duration_s`` (int), ``label`` (str), and one target of ``power_w`` (int) / ``pct_ftp``
    (float) / none (free). Host-tested independently of `fitparse`."""
    out: list[Segment] = []
    for st in steps:
        dur = int(st.get("duration_s") or 0)
        if dur <= 0:
            continue
        out.append(Segment(
            duration_s=dur,
            label=str(st.get("label") or "Step"),
            power_w=(int(st["power_w"]) if st.get("power_w") is not None else None),
            pct_ftp=(round(float(st["pct_ftp"]), 4) if st.get("pct_ftp") is not None else None),
            cadence_rpm=(int(st["cadence_rpm"]) if st.get("cadence_rpm") is not None else None),
        ))
    return out


def _fit_power(low: int | None, high: int | None) -> dict[str, Any]:
    """Decode a FIT custom-power target to {power_w} or {pct_ftp}. Convention (GoldenCheetah et
    al.): >1000 is watts (value-1000); 1..1000 is %FTP. Best-effort until validated vs a real
    .fit. Uses the midpoint of low/high. 0/None => no target (free)."""
    vals = [v for v in (low, high) if v]
    if not vals:
        return {}
    mid = sum(vals) / len(vals)
    if mid > 1000:
        return {"power_w": int(round(mid - 1000))}
    return {"pct_ftp": round(mid / 100.0, 4)}


def from_fit(path: str, *, name: str | None = None) -> Workout:
    """Parse a Garmin ``.fit`` workout file into a Workout. Requires the ``[analysis]`` extra
    (`fitparse`). FIT power decode is best-effort — validate against a real exported file."""
    try:
        import fitparse  # lazy: keeps the module importable without the [analysis] extra
    except ImportError as e:  # pragma: no cover - exercised only without the optional dep
        raise WorkoutImportError(
            "from_fit needs the 'analysis' extra (pip install -e '.[analysis]')") from e

    fit = fitparse.FitFile(path)
    wname = name
    steps: list[dict[str, Any]] = []
    for msg in fit.get_messages():
        if msg.name == "workout" and wname is None:
            v = msg.get_value("wkt_name")
            if v:
                wname = str(v)
        elif msg.name == "workout_step":
            d = {f.name: f.value for f in msg.fields}
            dv = d.get("duration_value")
            is_time = d.get("duration_type") in (None, "time")
            dur_s = int(round(dv / 1000)) if (dv and is_time) else 0
            step: dict[str, Any] = {"duration_s": dur_s, "label": d.get("wkt_step_name") or "Step"}
            step.update(_fit_power(d.get("custom_target_power_low"),
                                   d.get("custom_target_power_high")))
            steps.append(step)
    segs = fit_steps_to_segments(steps)
    if not segs:
        raise WorkoutImportError("no usable workout_step records in the .fit")
    return Workout(name=wname or "Imported workout", segments=tuple(segs))


def to_device_json(workout: Workout, ftp_w: int) -> str:
    """Emit the canonical on-device workout JSON (the WorkoutEngine.h shape) for `workout`,
    with `ftp_w` so %FTP/zone targets resolve on the device. POST this to ``/workout/load``."""
    out_segs: list[dict[str, Any]] = []
    for s in workout.segments:
        o: dict[str, Any] = {"t": int(s.duration_s), "label": s.label}
        if s.power_w is not None:
            o["power_w"] = int(s.power_w)
        elif s.pct_ftp is not None:
            o["pct_ftp"] = round(s.pct_ftp, 4)
        elif s.zone is not None:
            o["zone"] = s.zone
        if s.cadence_rpm is not None:
            o["cadence_rpm"] = int(s.cadence_rpm)
        out_segs.append(o)
    return json.dumps({"name": workout.name, "ftp_w": int(ftp_w), "segments": out_segs})
