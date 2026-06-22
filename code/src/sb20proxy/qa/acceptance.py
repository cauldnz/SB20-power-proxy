"""Board acceptance verdict — "is this board good to ship?", as pure logic.

The pre-beta plan ships ~10 **pre-flashed** boards; before one goes in an envelope it must pass a
quick, repeatable acceptance gate. This module is the *verdict* half: given the facts a bench
script gathered off a real board — did it advertise as the spoof crank? did its ``/status`` answer
and look healthy? did it actually push decodable CPS frames? — it returns a structured pass/fail
**acceptance card**. No hardware, no I/O: the script (`code/scripts/qa_board.py`) owns the BLE /
HTTP seam and feeds the observations here, so the decision logic is unit-tested like the rest of
the codec layer (real-data-first: the CPS frames the script samples decode with `ble.cps`).

A board is **acceptable** when every *critical* check that ran passed, and at least the
advertising check ran (a board that never showed up on the air can't be judged "good").
"""

from __future__ import annotations

from dataclasses import dataclass, field

# Free-heap floor (bytes). The C3 idles well above 100 KB; under this it's memory-stressed and a
# coex/WiFi hang is far likelier (see the PerfMonitor / watchdog work). A real captured /diag showed
# heap≈120 KB, so 40 KB is a conservative "something is wrong" line, not a tight budget.
HEAP_FLOOR_BYTES = 40_000

# Sane instantaneous power window for a sampled CPS frame (watts). Outside this a decode is almost
# certainly mis-framed, not a real reading; 0 is fine (a board with no meter near broadcasts 0).
POWER_MIN_W = 0
POWER_MAX_W = 2000

_VALID_SOURCE_STATES = {"mock", "connected", "searching"}


@dataclass
class Check:
    """One acceptance line. ``ok`` is None when the check didn't run (e.g. no IP given)."""

    name: str
    ok: bool | None
    detail: str
    critical: bool = True

    @property
    def mark(self) -> str:
        return "PASS" if self.ok else ("SKIP" if self.ok is None else "FAIL")


@dataclass
class AcceptanceReport:
    checks: list[Check] = field(default_factory=list)

    def add(self, name: str, ok: bool | None, detail: str, *, critical: bool = True) -> None:
        self.checks.append(Check(name, ok, detail, critical))

    @property
    def critical_ran(self) -> list[Check]:
        return [c for c in self.checks if c.critical and c.ok is not None]

    @property
    def failures(self) -> list[Check]:
        return [c for c in self.checks if c.ok is False]

    @property
    def passed(self) -> bool:
        """Acceptable = no critical check failed, and at least one critical check actually ran."""
        crit = self.critical_ran
        return bool(crit) and all(c.ok for c in crit)

    def render(self, *, title: str = "Board acceptance") -> str:
        """A human-readable acceptance card (the thing you read before sealing the envelope).

        Deliberately ASCII-only: this card is pasted into CI logs, shipping notes, and Windows
        consoles (cp1252), where emoji raise UnicodeEncodeError."""
        lines = [f"{title}: {'PASS' if self.passed else 'FAIL'}", "-" * 40]
        for c in self.checks:
            star = "" if c.critical else " (non-critical)"
            lines.append(f"  [{c.mark}] {c.name}{star}: {c.detail}")
        if not self.passed:
            why = ", ".join(c.name for c in self.failures) or "no critical check ran"
            lines.append("-" * 40)
            lines.append(f"  -> not shippable: {why}")
        return "\n".join(lines)


def evaluate(
    *,
    expected_spoof_name: str,
    advert_seen: bool | None,
    advert_names: list[str] | None = None,
    status: dict | None = None,
    cps_powers: list[int] | None = None,
    flash_ok: bool | None = None,
) -> AcceptanceReport:
    """Build the acceptance card from what the bench script observed.

    - ``flash_ok``: did the (re)flash step succeed? None when ``--no-flash``.
    - ``advert_seen``: was a BLE advert whose name contains ``expected_spoof_name`` seen?
    - ``advert_names``: names seen on the air (for the detail line / debugging).
    - ``status``: the parsed ``/status`` JSON dict, or None if not fetched.
    - ``cps_powers``: instantaneous-power values decoded from sampled CPS frames, or None.
    """
    r = AcceptanceReport()

    if flash_ok is not None:
        r.add("flashed", flash_ok,
              "firmware written + board rebooted" if flash_ok else "flash failed (see log)")

    seen = sorted(set(advert_names or []))
    if advert_seen is None:
        r.add("advertises as spoof crank", None, "BLE scan not run")
    else:
        detail = (f"saw '{expected_spoof_name}'" if advert_seen
                  else f"'{expected_spoof_name}' not on air"
                       + (f"; saw {seen}" if seen else "; nothing seen"))
        r.add("advertises as spoof crank", advert_seen, detail)

    if status is None:
        r.add("firmware responds (/status)", None, "no IP given - skipped")
        r.add("heap healthy", None, "no /status")
        r.add("source state sane", None, "no /status")
    else:
        fw = str(status.get("fw", ""))
        r.add("firmware responds (/status)", bool(fw), f"fw={fw or '?'}")
        heap = _as_int(status.get("heap"))
        if heap is None:
            r.add("heap healthy", None, "no heap field", critical=False)
        else:
            r.add("heap healthy", heap >= HEAP_FLOOR_BYTES,
                  f"{heap} bytes free (floor {HEAP_FLOOR_BYTES})")
        src = str(status.get("source", ""))
        r.add("source state sane", src in _VALID_SOURCE_STATES,
              f"source={src or '?'}")

    # CPS-frame sampling is a positive signal, not a hard gate: a -live board with no meter near has
    # nothing to forward and so is correctly SILENT on CPS (BleCrankPeripheral publishPower notifies
    # only on a real reading). Zero frames never blocks shipping; only a frame that decodes to an
    # out-of-range power is a real framing bug and fails.
    if cps_powers is None:
        r.add("CPS frames decode", None, "not sampled (use --connect)", critical=False)
    elif not cps_powers:
        r.add("CPS frames decode", None,
              "no frames in window (expected for a -live board with no meter near)",
              critical=False)
    else:
        bad = [p for p in cps_powers if not (POWER_MIN_W <= p <= POWER_MAX_W)]
        if bad:
            r.add("CPS frames decode", False,
                  f"{len(cps_powers)} frame(s) but out-of-range power {bad} (framing bug)")
        else:
            r.add("CPS frames decode", True,
                  f"{len(cps_powers)} frame(s), power {min(cps_powers)}-{max(cps_powers)} W",
                  critical=False)

    return r


def _as_int(v: object) -> int | None:
    try:
        return int(v)  # type: ignore[arg-type]
    except (TypeError, ValueError):
        return None
