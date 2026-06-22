"""Parse a tester's on-device ``/diag`` report (the desk half of the collaboration loop).

The firmware serves a plain-text diagnostic at ``GET /diag`` (``firmware/lib/proxy/DiagReport.h``)
that a beta tester saves and sends us when their meter isn't recognised. This module turns that
text back into structured data — the config, the live status, and the **raw CPS frames** the meter
sent — so ``scripts/parse_diag.py`` can decode them with the same :mod:`sb20proxy.ble.cps` codec the
firmware mirrors and tell us, offline, whether we handle the meter (real-data-first: a new meter's
golden vectors come from these committed bytes, never invented).

Pure text-parsing; no hardware. The report is section-based::

    [config]
      source_addr=...
      spoof_name=Stages 62144
    [status]
      src_power_w=158  src_cadence_rpm=88  src_balance_pct=44
    [meter frames] (CPS 0x2A63 raw hex, oldest first; 2 captured)
      23009e005816134e4d
      23009f005a1a13915a
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

_SECTION_RE = re.compile(r"^\[([a-z0-9 _]+)\]", re.IGNORECASE)
_HEX_RE = re.compile(r"^[0-9a-fA-F]+$")
_KV_RE = re.compile(r"(\w+)=(\S+(?:[^=\n]*?)?)(?=\s+\w+=|\s*$)")


@dataclass
class DiagReport:
    """A parsed ``/diag`` report. ``frames`` are lowercase hex strings (oldest→newest)."""

    fw: str = ""
    config: dict[str, str] = field(default_factory=dict)
    status: dict[str, str] = field(default_factory=dict)
    frames: list[str] = field(default_factory=list)


def _parse_kv_line(line: str) -> dict[str, str]:
    """Parse ``key=value  key2=value2``. A value may contain spaces (``spoof_name=Stages 62144``):
    we split on the next ``key=`` boundary so a spaced value stays whole."""
    return {m.group(1): m.group(2).strip() for m in _KV_RE.finditer(line)}


def parse_diag_report(text: str) -> DiagReport:
    """Parse the raw text of a ``/diag`` report into a :class:`DiagReport`."""
    rep = DiagReport()
    section = ""
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("fw=") and not rep.fw:
            rep.fw = _parse_kv_line(line).get("fw", "")
            # the header line also carries uptime/heap/rssi — keep them under status
            rep.status.update(_parse_kv_line(line))
            continue
        sec = _SECTION_RE.match(line)
        if sec:
            section = sec.group(1).strip().lower()
            continue
        if section == "meter frames":
            if _HEX_RE.match(line) and len(line) % 2 == 0:
                rep.frames.append(line.lower())
        elif section == "config":
            rep.config.update(_parse_kv_line(line))
        elif section == "status":
            rep.status.update(_parse_kv_line(line))
    return rep
