"""Pure checks for a live board's HTTP routes — the verdict half of the route smoke test.

Motivated by a real bug: the form-POST routes (/setup/save, /calibrate/start, /calibrate/save)
silently ignored browser ``application/x-www-form-urlencoded`` bodies because the firmware read
``arg("plain")`` (empty for that content type). Host tests only ever fed the pure parser a string;
the LIVE route was never exercised, so green CI hid a ride-blocking break. This module makes the
"did the live route actually work" judgement testable + repeatable: ``scripts/route_smoke.py`` does
the HTTP, these pure functions decide pass/fail. The headline is :func:`check_form_persisted` — POST
a real urlencoded form, then confirm the device's ``/diag`` reflects it.
"""

from __future__ import annotations

import json
from dataclasses import dataclass

from sb20proxy.analysis.diag import parse_diag_report


@dataclass
class RouteCheck:
    name: str
    ok: bool
    detail: str

    @property
    def mark(self) -> str:
        return "PASS" if self.ok else "FAIL"


def check_get(name: str, status: int, body: str, must_contain: list[str]) -> RouteCheck:
    """A GET route is healthy if it returns 200 and its body carries the expected markers."""
    if status != 200:
        return RouteCheck(name, False, f"HTTP {status}")
    missing = [m for m in must_contain if m not in body]
    if missing:
        return RouteCheck(name, False, f"200 but missing {missing}")
    return RouteCheck(name, True, "200, content ok")


def check_status_json(status: int, body: str) -> RouteCheck:
    """/status must be 200 and valid JSON carrying the core observability keys."""
    if status != 200:
        return RouteCheck("/status", False, f"HTTP {status}")
    try:
        d = json.loads(body)
    except ValueError as exc:
        return RouteCheck("/status", False, f"not JSON: {exc}")
    required = {"fw", "source", "heap", "power_w"}
    missing = required - d.keys()
    if missing:
        return RouteCheck("/status", False, f"JSON missing {sorted(missing)}")
    return RouteCheck("/status", True,
                      f"fw={d.get('fw')} source={d.get('source')} heap={d.get('heap')}")


def check_form_persisted(field: str, expected: str, diag_body_after: str) -> RouteCheck:
    """The regression guard for the form-POST class of bug: after POSTing a urlencoded form that
    should set ``field`` to ``expected``, the device's ``/diag`` config must reflect it. If the
    route silently ignored the body (the original bug), the field keeps its old value -> FAIL."""
    rep = parse_diag_report(diag_body_after)
    got = rep.config.get(field, "<absent>")
    if got != expected:
        return RouteCheck(f"POST persisted ({field})", False,
                          f"expected {expected!r}, /diag shows {got!r} — form body ignored?")
    return RouteCheck(f"POST persisted ({field})", True, f"{field}={got!r} persisted")


def overall_pass(checks: list[RouteCheck]) -> bool:
    return bool(checks) and all(c.ok for c in checks)


def render(checks: list[RouteCheck], *, title: str = "Board route smoke") -> str:
    """ASCII card (cp1252-safe, like the QA acceptance card)."""
    lines = [f"{title}: {'PASS' if overall_pass(checks) else 'FAIL'}", "-" * 44]
    for c in checks:
        lines.append(f"  [{c.mark}] {c.name}: {c.detail}")
    return "\n".join(lines)
