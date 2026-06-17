"""Turn an ESP32 unit's ``/log`` dump into a structured observation summary.

A field/beta unit logs what it sees to its RAM ring, retrievable with ``curl http://<ip>/log``.
This parses that text — built to the firmware's exact line formats (``src/ble/BleMeterClient.cpp``
and ``BleCrankPeripheral.cpp``) — into:

- **meter frame spec** from ``[meter] cps flags=0x.... cadence=yes/no <hex>`` — which CPS fields the
  meter (Garmin / Wahoo / Assioma / ...) sends, and whether it carries cadence;
- **the consumer's handshake** from ``[cp] write <hex>`` (decoded via :mod:`sb20proxy.ble.cps`) and
  ``[prop fe02] write <hex>`` (the opaque Stages proprietary writes);
- **connect / disconnect** events from ``[srv] ...`` (bonding / reconnection behaviour).

Pure — no hardware, no network — so it is host-tested; fixtures are the real firmware line formats.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

from sb20proxy.ble import cps

# CPS Measurement flag bit -> human field name (data fields and the two modifier bits).
_FLAG_NAMES: list[tuple[int, str]] = [
    (cps.F_PEDAL_BALANCE, "pedal_balance"),
    (cps.F_BALANCE_REF_LEFT, "balance_ref_left"),
    (cps.F_ACCUM_TORQUE, "accumulated_torque"),
    (cps.F_TORQUE_SOURCE_CRANK, "torque_source_crank"),
    (cps.F_WHEEL_REV, "wheel_rev"),
    (cps.F_CRANK_REV, "crank_rev(cadence)"),
]

# Control-point op code -> name (what the consumer asked of the crank).
_CP_OPS: dict[int, str] = {
    cps.CP_SET_CUMULATIVE_VALUE: "set_cumulative_value",
    cps.CP_REQUEST_SUPPORTED_SENSOR_LOCATIONS: "request_sensor_locations",
    cps.CP_SET_CRANK_LENGTH: "set_crank_length",
    cps.CP_REQUEST_CRANK_LENGTH: "request_crank_length",
    cps.CP_START_OFFSET_COMPENSATION: "start_offset_compensation(zero-reset)",
    cps.CP_RESPONSE_CODE: "response",
}


def flag_field_names(flags: int) -> list[str]:
    """The CPS-Measurement fields/modifiers present for ``flags`` (ascending bit order)."""
    return [name for bit, name in _FLAG_NAMES if flags & bit]


def cp_op_name(opcode: int) -> str:
    return _CP_OPS.get(opcode, f"op_0x{opcode:02x}")


@dataclass
class MeterObservation:
    flags: int
    has_cadence: bool
    fields: list[str]
    raw_hex: str


@dataclass
class CpWrite:
    raw_hex: str
    op: int
    op_name: str


@dataclass
class LogSummary:
    meters: list[MeterObservation] = field(default_factory=list)
    cp_writes: list[CpWrite] = field(default_factory=list)
    prop_writes: list[str] = field(default_factory=list)
    connects: list[str] = field(default_factory=list)
    disconnects: list[int] = field(default_factory=list)

    def render(self) -> str:
        out: list[str] = []
        for m in self.meters:
            out.append(
                f"meter frame: flags=0x{m.flags:04x} cadence={'yes' if m.has_cadence else 'NO'} "
                f"fields=[{', '.join(m.fields)}] raw={m.raw_hex}"
            )
        for c in self.connects:
            out.append(f"consumer connected: {c}")
        for w in self.cp_writes:
            out.append(f"control-point write: {w.raw_hex}  -> {w.op_name}")
        for p in self.prop_writes:
            out.append(f"proprietary fe02 write: {p}  (opaque)")
        for r in self.disconnects:
            out.append(f"consumer disconnected: reason={r}")
        if not out:
            out.append("(no recognised observation lines)")
        return "\n".join(out)


_RE_METER = re.compile(r"\[meter\] cps flags=0x([0-9a-fA-F]+) cadence=(\w+)\s*([0-9a-fA-F]*)")
_RE_CP = re.compile(r"\[cp\] write ([0-9a-fA-F]*)")
_RE_PROP = re.compile(r"\[prop fe02\] write ([0-9a-fA-F]*)")
_RE_CONNECT = re.compile(r"\[srv\] connect from (.+?)\s*$")
_RE_DISCONNECT = re.compile(r"\[srv\] disconnect reason=(-?\d+)")


def parse_log(text: str) -> LogSummary:
    """Parse a ``/log`` dump (oldest-first lines) into a :class:`LogSummary`."""
    s = LogSummary()
    for line in text.splitlines():
        if (m := _RE_METER.search(line)):
            flags = int(m.group(1), 16)
            s.meters.append(
                MeterObservation(
                    flags=flags,
                    has_cadence=bool(flags & cps.F_CRANK_REV),
                    fields=flag_field_names(flags),
                    raw_hex=m.group(3),
                )
            )
        elif (m := _RE_CP.search(line)):
            raw = m.group(1)
            op = int(raw[:2], 16) if len(raw) >= 2 else -1
            s.cp_writes.append(CpWrite(raw_hex=raw, op=op, op_name=cp_op_name(op)))
        elif (m := _RE_PROP.search(line)):
            s.prop_writes.append(m.group(1))
        elif (m := _RE_CONNECT.search(line)):
            s.connects.append(m.group(1))
        elif (m := _RE_DISCONNECT.search(line)):
            s.disconnects.append(int(m.group(1)))
    return s
