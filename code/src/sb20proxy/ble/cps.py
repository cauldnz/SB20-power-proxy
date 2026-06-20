"""Bluetooth Cycling Power Service (0x1818) — the pure measurement + control-point codec.

The Python mirror of the ESP32 firmware's `firmware/lib/proxy/Cps.h`, built to the full
Cycling Power Measurement layout (Bluetooth SIG characteristic 0x2A63) so we can encode
*and* decode every flag combination. The wire layout is the public spec; correctness is
pinned by REAL frames captured off the Stages crank and the Assioma
(findings/captures/G-*-ble-*.jsonl) — real-data-first, never invented bytes.

Field order on the wire (CPS Measurement): flags (uint16 LE), instantaneous power
(sint16 LE, always), then each present optional field in ascending-flag-bit order:
  bit0 pedal balance (uint8, 1/2 %) · bit2 accumulated torque (uint16, 1/32 Nm) ·
  bit4 wheel revs (uint32) + wheel event time (uint16) · bit5 crank revs (uint16) +
  crank event time (uint16, 1/1024 s) · ... (extreme/dead-spot/energy fields follow).
Bits 1 (balance reference) and 3 (torque source) are modifiers with no data field.
"""

from __future__ import annotations

from dataclasses import dataclass

# --- service / characteristic UUIDs (16-bit assigned numbers) ---
UUID_CPS = 0x1818
UUID_CP_MEASUREMENT = 0x2A63      # notify
UUID_CP_FEATURE = 0x2A65          # read
UUID_CP_SENSOR_LOCATION = 0x2A5D  # read
UUID_CP_CONTROL_POINT = 0x2A66    # write + indicate

# --- Cycling Power Measurement flags (uint16) ---
F_PEDAL_BALANCE = 1 << 0
F_BALANCE_REF_LEFT = 1 << 1       # modifier (no field): balance is left-referenced
F_ACCUM_TORQUE = 1 << 2
F_TORQUE_SOURCE_CRANK = 1 << 3    # modifier (no field): torque is crank-based
F_WHEEL_REV = 1 << 4
F_CRANK_REV = 1 << 5
F_EXTREME_FORCE = 1 << 6
F_EXTREME_TORQUE = 1 << 7
F_EXTREME_ANGLES = 1 << 8
F_TOP_DEAD_SPOT = 1 << 9
F_BOTTOM_DEAD_SPOT = 1 << 10
F_ACCUM_ENERGY = 1 << 11
F_OFFSET_COMP_IND = 1 << 12       # modifier (no field)

# The Stages SPM2 crank's full flag set, exactly as captured (0x002F):
# balance + balance-ref-left + accumulated-torque + torque-source-crank + crank-rev.
STAGES_FLAGS = (F_PEDAL_BALANCE | F_BALANCE_REF_LEFT | F_ACCUM_TORQUE
                | F_TORQUE_SOURCE_CRANK | F_CRANK_REV)

# --- Cycling Power Feature (0x2A65, uint32) bits we care about ---
FEAT_PEDAL_BALANCE = 1 << 0
FEAT_ACCUM_TORQUE = 1 << 1
FEAT_WHEEL_REV = 1 << 2
FEAT_CRANK_REV = 1 << 3

# --- Control Point (0x2A66) op codes ---
CP_SET_CUMULATIVE_VALUE = 0x01
CP_REQUEST_SUPPORTED_SENSOR_LOCATIONS = 0x03
CP_SET_CRANK_LENGTH = 0x04
CP_REQUEST_CRANK_LENGTH = 0x05
CP_START_OFFSET_COMPENSATION = 0x0C  # the BLE zero-reset
CP_RESPONSE_CODE = 0x20
CP_RESULT_SUCCESS = 0x01
CP_RESULT_OP_NOT_SUPPORTED = 0x02
CP_RESULT_INVALID_PARAMETER = 0x03
CP_RESULT_OPERATION_FAILED = 0x04


@dataclass(frozen=True)
class CpsMeasurement:
    flags: int
    power_w: int
    pedal_balance: int | None = None        # uint8, units of 1/2 %
    accumulated_torque: int | None = None   # uint16, units of 1/32 Nm
    cumulative_wheel_revs: int | None = None
    last_wheel_event_time: int | None = None
    cumulative_crank_revs: int | None = None
    last_crank_event_time: int | None = None  # 1/1024 s, wraps ~64 s

    @property
    def balance_pct(self) -> float | None:
        return None if self.pedal_balance is None else self.pedal_balance / 2.0


def decode_cps_measurement(data: bytes) -> CpsMeasurement:
    """Parse a Cycling Power Measurement (0x2A63) notification per its flags."""
    if len(data) < 4:
        raise ValueError(f"CPS measurement too short ({len(data)} bytes)")
    flags = data[0] | (data[1] << 8)
    power = int.from_bytes(data[2:4], "little", signed=True)
    i = 4

    def take(n: int) -> bytes:
        nonlocal i
        if i + n > len(data):
            raise ValueError(f"CPS measurement truncated: need {n} bytes at offset {i}")
        b = data[i:i + n]
        i += n
        return b

    bal = torque = wheel_c = wheel_t = crank_c = crank_t = None
    if flags & F_PEDAL_BALANCE:
        bal = take(1)[0]
    if flags & F_ACCUM_TORQUE:
        torque = int.from_bytes(take(2), "little")
    if flags & F_WHEEL_REV:
        wheel_c = int.from_bytes(take(4), "little")
        wheel_t = int.from_bytes(take(2), "little")
    if flags & F_CRANK_REV:
        crank_c = int.from_bytes(take(2), "little")
        crank_t = int.from_bytes(take(2), "little")
    # Extreme-force/torque/angle, dead-spot and energy fields would follow here; our
    # meters don't set those flags, so we stop (and would extend if a meter does).
    return CpsMeasurement(
        flags=flags, power_w=power, pedal_balance=bal, accumulated_torque=torque,
        cumulative_wheel_revs=wheel_c, last_wheel_event_time=wheel_t,
        cumulative_crank_revs=crank_c, last_crank_event_time=crank_t,
    )


def encode_cps_measurement(
    power_w: int,
    *,
    pedal_balance: int | None = None,
    accumulated_torque: int | None = None,
    cumulative_crank_revs: int | None = None,
    last_crank_event_time: int | None = None,
    cumulative_wheel_revs: int | None = None,
    last_wheel_event_time: int | None = None,
    extra_flags: int = 0,
) -> bytes:
    """Build a Cycling Power Measurement. Flag bits for present fields are set
    automatically; `extra_flags` carries the modifier bits that have no data field
    (balance-reference, torque-source, offset-compensation-indicator) — pass
    `STAGES_FLAGS`-style modifiers there to reproduce a specific crank's framing."""
    flags = extra_flags
    if pedal_balance is not None:
        flags |= F_PEDAL_BALANCE
    if accumulated_torque is not None:
        flags |= F_ACCUM_TORQUE
    if cumulative_wheel_revs is not None or last_wheel_event_time is not None:
        flags |= F_WHEEL_REV
    if cumulative_crank_revs is not None or last_crank_event_time is not None:
        flags |= F_CRANK_REV

    out = bytearray()
    out += (flags & 0xFFFF).to_bytes(2, "little")
    out += int(power_w).to_bytes(2, "little", signed=True)
    if flags & F_PEDAL_BALANCE:
        out.append((pedal_balance or 0) & 0xFF)
    if flags & F_ACCUM_TORQUE:
        out += ((accumulated_torque or 0) & 0xFFFF).to_bytes(2, "little")
    if flags & F_WHEEL_REV:
        out += ((cumulative_wheel_revs or 0) & 0xFFFFFFFF).to_bytes(4, "little")
        out += ((last_wheel_event_time or 0) & 0xFFFF).to_bytes(2, "little")
    if flags & F_CRANK_REV:
        out += ((cumulative_crank_revs or 0) & 0xFFFF).to_bytes(2, "little")
        out += ((last_crank_event_time or 0) & 0xFFFF).to_bytes(2, "little")
    return bytes(out)


# --- Control Point (zero-reset / calibration) ---

@dataclass(frozen=True)
class ControlPointResponse:
    request_opcode: int
    result: int
    params: bytes = b""

    @property
    def success(self) -> bool:
        return self.result == CP_RESULT_SUCCESS

    @property
    def offset(self) -> int | None:
        """The sint16 offset returned by Start Offset Compensation, if present."""
        if len(self.params) >= 2:
            return int.from_bytes(self.params[:2], "little", signed=True)
        return None


def encode_calibration_response(
    offset: int,
    *,
    request_opcode: int = CP_START_OFFSET_COMPENSATION,
    result: int = CP_RESULT_SUCCESS,
) -> bytes:
    """The control-point indication answering a zero-reset: 0x20 <req> <result> <offset LE>."""
    return bytes([CP_RESPONSE_CODE, request_opcode, result]) + \
        int(offset).to_bytes(2, "little", signed=True)


def decode_control_point(data: bytes) -> ControlPointResponse | tuple[str, int, bytes] | None:
    """Decode a control-point value. A response (starts with 0x20) -> ControlPointResponse;
    a request (any other op code) -> ("request", opcode, params)."""
    if not data:
        return None
    if data[0] == CP_RESPONSE_CODE and len(data) >= 3:
        return ControlPointResponse(request_opcode=data[1], result=data[2],
                                    params=bytes(data[3:]))
    return ("request", data[0], bytes(data[1:]))


# --- cadence from Crank Revolution Data (what a head unit does) ---

def cadence_rpm_from_crank(revs0: int, t0: int, revs1: int, t1: int) -> float:
    """Recover cadence (rpm) from two Crank Revolution Data samples. uint16 deltas wrap,
    which is correct; event time is in 1/1024 s. Returns 0 if no event-time elapsed."""
    d_revs = (revs1 - revs0) & 0xFFFF
    d_t = (t1 - t0) & 0xFFFF
    if d_t == 0:
        return 0.0
    return d_revs * 60.0 * 1024.0 / d_t


class CrankCadenceTracker:
    """Recover cadence (rpm) across successive Crank Revolution Data samples, holding the
    previous (revs, event_time) so callers don't each hand-roll the state. `update` returns the
    rounded rpm, or None when there's no prior sample yet or no cadence is recoverable (rpm <= 0).
    The caller decides what to do with None (a per-reading source uses it as-is; a sticky twin
    keeps its last good value)."""

    def __init__(self) -> None:
        self._prev: tuple[int, int] | None = None

    def update(self, revs: int, event_time: int) -> int | None:
        cadence: int | None = None
        if self._prev is not None:
            rpm = cadence_rpm_from_crank(self._prev[0], self._prev[1], revs, event_time)
            if rpm > 0:
                cadence = round(rpm)
        self._prev = (revs, event_time)
        return cadence


@dataclass
class CrankCadence:
    """Advancing crank-revolution state for the CPS Crank Revolution Data fields. A head
    unit derives cadence from the DELTA of (cumulative revs, last crank event time)
    between notifications, so we advance the event time by exactly one revolution-period
    per completed revolution — the recovered cadence then equals the input rpm. The
    Python mirror of Cps.h's CrankCadence."""

    cumulative_revs: int = 0
    last_event_time: int = 0  # 1/1024 s, wraps ~64 s
    pending_revs: float = 0.0

    def advance(self, rpm: float, dt_ms: int) -> None:
        if rpm <= 0.0 or dt_ms == 0:
            return  # coasting / no time elapsed: no new crank events
        self.pending_revs += rpm * (dt_ms / 60000.0)
        period_ticks = 60.0 * 1024.0 / rpm  # 1/1024 s per revolution
        while self.pending_revs >= 1.0:
            self.pending_revs -= 1.0
            self.cumulative_revs = (self.cumulative_revs + 1) & 0xFFFF
            self.last_event_time = (self.last_event_time + round(period_ticks)) & 0xFFFF
