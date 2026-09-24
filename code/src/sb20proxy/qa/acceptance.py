"""Board acceptance verdict — "is this board good to ship?", as pure logic.

The pre-beta plan ships ~10 **pre-flashed** boards; before one goes in an envelope it must pass a
quick, repeatable acceptance gate. This module is the *verdict* half: given the facts a bench
script gathered off a real board — did it advertise as the spoof crank? did its ``/status`` answer
and look healthy? did it actually push decodable CPS frames? — it returns a structured pass/fail
**acceptance card**. No hardware, no I/O: the script (`code/scripts/qa_board.py`) owns the BLE /
HTTP seam and feeds the observations here, so the decision logic is unit-tested like the rest of
the codec layer (real-data-first: the CPS frames the script samples decode with `ble.cps`).

A board is **acceptable** when every *critical* check that ran passed, and at least one critical
*evidence* check ran (a board that never showed up on the air can't be judged "good").

Fleet identity (#330): the card also vetoes a board whose advertised identity is one of bike 1's
REAL cranks (unless the board is deliberately standing in for that crank) and a name that more than
one advertiser is using — the two collisions that pair an SB20 to an unfed spoof or block pairing.
The MAC-derived default identity (``default_spoof_name``) is the Python twin of
``firmware/lib/proxy/FleetIdentity.h``; ``test_qa_acceptance.py`` parity-locks the two.
"""

from __future__ import annotations

import re
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

# Bike 1's real Stages cranks (left 62144, right 4963; captured over BLE). A board advertising one
# of these next to bike 1 pairs the bike to an unfed spoof (0 W) or refuses pairing
# (system-reference §7). Mirrors firmware/lib/proxy/FleetIdentity.h::kRealCrankNames
# (parity-tested).
REAL_CRANK_NAMES = ("Stages 62144", "Stages 4963")

# The MAC-derived default identity namespace, "Stages 9NNNN": what a board with NOTHING stored
# advertises (FleetIdentity.h::defaultSpoofName). NNNN = the last 16 bits of the base MAC mod 10000,
# zero-padded. The '9' prefix keeps every derived id clear of the real cranks; whether the Stages
# app pairs a 9xxxx id is an open assumption (system-reference §11): only 62145 is proven on the
# bike.
DERIVED_IDENTITY_PREFIX = "Stages 9"
DERIVED_IDENTITY_MODULUS = 10000

_MAC_SEP_RE = re.compile(r"[:\-]")


def default_spoof_name(mac: str) -> str:
    """The identity a board derives when nothing is stored — the Python twin of defaultSpoofName().

    ``mac`` is the 6-byte BASE MAC as ``esptool read-mac`` / BOARDS.md print it
    (``A4:CB:8F:DA:E9:CC``, or dash-separated, any case). Its last two bytes are also the board's
    ``Setup-XXXX`` SSID suffix."""
    parts = [p for p in _MAC_SEP_RE.split(mac.strip()) if p]
    if len(parts) != 6 or not all(len(p) == 2 for p in parts):
        raise ValueError(f"not a 6-byte MAC: {mac!r}")
    low16 = (int(parts[4], 16) << 8) | int(parts[5], 16)
    return f"{DERIVED_IDENTITY_PREFIX}{low16 % DERIVED_IDENTITY_MODULUS:04d}"


def is_real_crank_identity(name: str) -> bool:
    """Is ``name`` one of bike 1's real crank identities? (Exact match, like the firmware.)"""
    return name in REAL_CRANK_NAMES


@dataclass
class Check:
    """One acceptance line. ``ok`` is None when the check didn't run (e.g. no IP given)."""

    name: str
    ok: bool | None
    detail: str
    critical: bool = True
    # A VETO check can fail the card, but passing it is not evidence the board works: "identity is
    # not a real crank" passes for a board that never came on the air. Vetoes are excluded from the
    # "at least one critical check ran" rule so an identity rule can never turn an unobserved board
    # into a PASS.
    veto: bool = False

    @property
    def mark(self) -> str:
        return "PASS" if self.ok else ("SKIP" if self.ok is None else "FAIL")


@dataclass
class AcceptanceReport:
    checks: list[Check] = field(default_factory=list)

    def add(self, name: str, ok: bool | None, detail: str, *, critical: bool = True,
            veto: bool = False) -> None:
        self.checks.append(Check(name, ok, detail, critical, veto))

    @property
    def critical_ran(self) -> list[Check]:
        """The critical EVIDENCE checks that actually ran (vetoes don't count as evidence)."""
        return [c for c in self.checks if c.critical and c.ok is not None and not c.veto]

    @property
    def failures(self) -> list[Check]:
        return [c for c in self.checks if c.ok is False]

    @property
    def passed(self) -> bool:
        """Acceptable = no critical check failed, and at least one critical evidence check ran."""
        crit = self.critical_ran
        no_critical_failure = not any(c.ok is False for c in self.checks if c.critical)
        return bool(crit) and no_critical_failure

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
    advert_matches: list[str] | None = None,
    status: dict | None = None,
    cps_powers: list[int] | None = None,
    flash_ok: bool | None = None,
    crank_rescue: bool = False,
) -> AcceptanceReport:
    """Build the acceptance card from what the bench script observed.

    - ``flash_ok``: did the (re)flash step succeed? None when ``--no-flash``.
    - ``advert_seen``: was a BLE advert whose name contains ``expected_spoof_name`` seen?
    - ``advert_names``: names seen on the air (for the detail line / debugging).
    - ``advert_matches``: the BLE addresses whose advert matched ``expected_spoof_name`` (None when
      the scan did not run). More than one = two devices share the identity: that fails.
    - ``status``: the parsed ``/status`` JSON dict, or None if not fetched.
    - ``cps_powers``: instantaneous-power values decoded from sampled CPS frames, or None.
    - ``crank_rescue``: this board is DELIBERATELY standing in for one of bike 1's real cranks (the
      single-right-crank rescue), so a real-crank identity is intended rather than a hazard.
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

    # Identity hygiene (#330): the name this board advertises must not be one of bike 1's REAL
    # cranks (a board on 62144 next to that bike pairs it to an unfed spoof or blocks pairing)
    # unless the board is deliberately standing in for that crank; and it must be the ONLY
    # advertiser under it.
    names = {expected_spoof_name}
    if status is not None and status.get("identity"):
        names.add(str(status["identity"]))
    real = sorted(n for n in names if is_real_crank_identity(n))
    if not real:
        r.add("identity is not a real crank", True, f"'{expected_spoof_name}'", veto=True)
    elif crank_rescue:
        r.add("identity is not a real crank", True,
              f"{real} is bike 1's real crank id, intended (--crank-rescue)", veto=True)
    else:
        r.add("identity is not a real crank", False,
              f"{real} is bike 1's real crank id; give the board its own identity in /setup "
              "(blank = its MAC-derived default), or pass --crank-rescue if it is meant to stand "
              "in for that crank", veto=True)
    if advert_matches is not None and len(advert_matches) > 1:
        r.add("identity unique on air", False,
              f"{len(advert_matches)} advertisers match '{expected_spoof_name}': "
              f"{sorted(advert_matches)} (two boards, or a board and a real crank, share one name)",
              veto=True)
    elif advert_matches:
        r.add("identity unique on air", True, f"one advertiser ({advert_matches[0]})", veto=True)

    if status is None:
        r.add("firmware responds (/status)", None, "no IP given - skipped")
        r.add("heap healthy", None, "no /status")
        r.add("source state sane", None, "no /status")
        r.add("identity provenance", None, "no /status", critical=False)
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
        # Informational: a board on its MAC-derived default has not been named by anyone yet. Not a
        # failure (the fleet table may adopt that default), but it should be visible on the card.
        dflt = status.get("identity_default")
        if dflt is None:
            r.add("identity provenance", None, "firmware predates identity_default", critical=False)
        else:
            r.add("identity provenance", True,
                  "MAC-derived default (nothing stored)" if dflt else "configured (stored in NVS)",
                  critical=False)

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
