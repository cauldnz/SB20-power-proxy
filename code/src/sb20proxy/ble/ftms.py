"""Bluetooth Fitness Machine Service (0x1826) — the pure "indoor bike" codec.

The Python mirror of the ESP32 firmware's `firmware/lib/proxy/Ftms.h`, built to the
Bluetooth SIG FTMS spec so we can encode *and* decode the bike characteristics:
Indoor Bike Data (0x2AD2), the Fitness Machine Control Point (0x2AD9 — erg lives here:
Set Target Power), Fitness Machine Feature (0x2ACC), the Supported *Range chars, the
Fitness Machine Status (0x2ADA) and Training Status (0x2AD3).

SPEC-BUILT, pending real-capture validation (Session 4 §C, `capture_ftms.py --erg`):
unlike the CPS codec (pinned by captured Stages/Assioma frames), there are no FTMS
payload captures yet. The wire layout here is the public spec; `SPEC_VECTORS` are
labelled *spec-derived* (hand-built to the spec, NOT real captures). When the SB20
frames are captured they become the golden source and real data wins on any conflict.

Indoor Bike Data wire order (each optional field gated by a flag bit, ascending):
  flags (u16) · instantaneous speed (u16, 1/100 km/h — present when the More-Data bit0
  is *0*) · avg speed (u16) · instantaneous cadence (u16, 1/2 rpm) · avg cadence (u16) ·
  total distance (u24, m) · resistance (s16) · instantaneous power (s16, W) · avg power
  (s16, W) · expended energy (u16 total kcal + u16 kcal/h + u8 kcal/min) · heart rate
  (u8) · metabolic equivalent (u8, 1/10) · elapsed time (u16, s) · remaining time (u16, s).
"""

from __future__ import annotations

from dataclasses import dataclass

# --- service / characteristic UUIDs (16-bit assigned numbers) ---
UUID_FTMS = 0x1826
UUID_INDOOR_BIKE_DATA = 0x2AD2          # notify
UUID_FTMS_FEATURE = 0x2ACC              # read
UUID_SUPPORTED_SPEED_RANGE = 0x2AD4     # read
UUID_SUPPORTED_INCLINATION_RANGE = 0x2AD5   # read
UUID_SUPPORTED_RESISTANCE_RANGE = 0x2AD6    # read  (note: 0x2AD7 in some older drafts)
UUID_SUPPORTED_POWER_RANGE = 0x2AD8     # read
UUID_FTMS_CONTROL_POINT = 0x2AD9        # write + indicate
UUID_FTMS_STATUS = 0x2ADA               # notify
UUID_TRAINING_STATUS = 0x2AD3           # read + notify

# --- Indoor Bike Data flags (u16) ---
IBD_MORE_DATA = 1 << 0          # bit0: when 0, Instantaneous Speed IS present (spec inversion)
IBD_AVG_SPEED = 1 << 1
IBD_INST_CADENCE = 1 << 2
IBD_AVG_CADENCE = 1 << 3
IBD_TOTAL_DISTANCE = 1 << 4
IBD_RESISTANCE = 1 << 5
IBD_INST_POWER = 1 << 6
IBD_AVG_POWER = 1 << 7
IBD_EXPENDED_ENERGY = 1 << 8
IBD_HEART_RATE = 1 << 9
IBD_METABOLIC = 1 << 10
IBD_ELAPSED_TIME = 1 << 11
IBD_REMAINING_TIME = 1 << 12

# --- Fitness Machine Control Point (0x2AD9) op codes ---
CP_REQUEST_CONTROL = 0x00
CP_RESET = 0x01
CP_SET_TARGET_SPEED = 0x02              # + u16 (1/100 km/h)
CP_SET_TARGET_INCLINATION = 0x03        # + s16 (1/10 %)
CP_SET_TARGET_RESISTANCE = 0x04         # + s16 (1/10)
CP_SET_TARGET_POWER = 0x05              # + s16 LE watts  <-- erg
CP_SET_TARGET_HEART_RATE = 0x06         # + u8 bpm
CP_START_RESUME = 0x07
CP_STOP_PAUSE = 0x08                    # + u8 (0x01 stop / 0x02 pause)
CP_SET_INDOOR_BIKE_SIM = 0x11          # + s16 wind, s16 grade, u8 crr, u8 cw
CP_SPIN_DOWN_CONTROL = 0x13
CP_SET_TARGETED_CADENCE = 0x14          # + u16 (1/2 rpm)
CP_RESPONSE = 0x80                      # response-code op

CP_SUCCESS = 0x01
CP_OP_NOT_SUPPORTED = 0x02
CP_INVALID_PARAMETER = 0x03
CP_OPERATION_FAILED = 0x04
CP_CONTROL_NOT_PERMITTED = 0x05
CP_RESULT_NAMES = {
    CP_SUCCESS: "success", CP_OP_NOT_SUPPORTED: "op_not_supported",
    CP_INVALID_PARAMETER: "invalid_parameter", CP_OPERATION_FAILED: "operation_failed",
    CP_CONTROL_NOT_PERMITTED: "control_not_permitted",
}

# --- Fitness Machine Feature (0x2ACC) — Machine Features (u32) bits we use ---
FEAT_AVG_SPEED = 1 << 0
FEAT_CADENCE = 1 << 1
FEAT_TOTAL_DISTANCE = 1 << 2
FEAT_RESISTANCE_LEVEL = 1 << 7
FEAT_EXPENDED_ENERGY = 1 << 9
FEAT_HEART_RATE = 1 << 10
FEAT_POWER_MEASUREMENT = 1 << 14
# --- Target Setting Features (u32) bits we use ---
TGT_SPEED = 1 << 0
TGT_INCLINATION = 1 << 1
TGT_RESISTANCE = 1 << 2
TGT_POWER = 1 << 3                      # Power Target Setting Supported (erg capability)
TGT_HEART_RATE = 1 << 4
TGT_INDOOR_BIKE_SIM = 1 << 13
TGT_SPIN_DOWN = 1 << 15
TGT_CADENCE = 1 << 16

# --- Fitness Machine Status (0x2ADA) op codes we parse ---
ST_RESET = 0x01
ST_STOPPED_PAUSED = 0x02                # + u8 (0x01 stop / 0x02 pause)
ST_STARTED_RESUMED = 0x04
ST_TARGET_POWER_CHANGED = 0x08          # + s16 watts
ST_INDOOR_BIKE_SIM_CHANGED = 0x0D
ST_CONTROL_PERMISSION_LOST = 0xFF


# ============================ Indoor Bike Data ============================

@dataclass(frozen=True)
class IndoorBikeData:
    """A decoded Indoor Bike Data (0x2AD2) notification — raw wire values + SI helpers."""

    flags: int
    instantaneous_speed: int | None = None   # 1/100 km/h
    average_speed: int | None = None
    instantaneous_cadence: int | None = None  # 1/2 rpm
    average_cadence: int | None = None
    total_distance: int | None = None         # m
    resistance_level: int | None = None       # s16
    instantaneous_power: int | None = None    # W (s16)
    average_power: int | None = None          # W
    total_energy: int | None = None           # kcal
    energy_per_hour: int | None = None        # kcal/h
    energy_per_minute: int | None = None      # kcal/min
    heart_rate: int | None = None             # bpm
    metabolic_equivalent: int | None = None   # 1/10
    elapsed_time: int | None = None           # s
    remaining_time: int | None = None         # s

    @property
    def power_w(self) -> int | None:
        return self.instantaneous_power

    @property
    def speed_kmh(self) -> float | None:
        return None if self.instantaneous_speed is None else self.instantaneous_speed / 100.0

    @property
    def cadence_rpm(self) -> float | None:
        return None if self.instantaneous_cadence is None else self.instantaneous_cadence / 2.0


def decode_indoor_bike_data(data: bytes) -> IndoorBikeData:
    """Parse an Indoor Bike Data (0x2AD2) notification per its flags."""
    if len(data) < 2:
        raise ValueError(f"indoor bike data too short ({len(data)} bytes)")
    flags = data[0] | (data[1] << 8)
    i = 2

    def take(n: int) -> int:
        nonlocal i
        if i + n > len(data):
            raise ValueError(f"indoor bike data truncated: need {n} bytes at offset {i}")
        v = int.from_bytes(data[i:i + n], "little")
        i += n
        return v

    def takes(n: int) -> int:
        nonlocal i
        if i + n > len(data):
            raise ValueError(f"indoor bike data truncated: need {n} bytes at offset {i}")
        v = int.from_bytes(data[i:i + n], "little", signed=True)
        i += n
        return v

    f: dict[str, int] = {}
    if not (flags & IBD_MORE_DATA):           # speed present when More-Data bit is 0
        f["instantaneous_speed"] = take(2)
    if flags & IBD_AVG_SPEED:
        f["average_speed"] = take(2)
    if flags & IBD_INST_CADENCE:
        f["instantaneous_cadence"] = take(2)
    if flags & IBD_AVG_CADENCE:
        f["average_cadence"] = take(2)
    if flags & IBD_TOTAL_DISTANCE:
        f["total_distance"] = take(3)
    if flags & IBD_RESISTANCE:
        f["resistance_level"] = takes(2)
    if flags & IBD_INST_POWER:
        f["instantaneous_power"] = takes(2)
    if flags & IBD_AVG_POWER:
        f["average_power"] = takes(2)
    if flags & IBD_EXPENDED_ENERGY:
        f["total_energy"] = take(2)
        f["energy_per_hour"] = take(2)
        f["energy_per_minute"] = take(1)
    if flags & IBD_HEART_RATE:
        f["heart_rate"] = take(1)
    if flags & IBD_METABOLIC:
        f["metabolic_equivalent"] = take(1)
    if flags & IBD_ELAPSED_TIME:
        f["elapsed_time"] = take(2)
    if flags & IBD_REMAINING_TIME:
        f["remaining_time"] = take(2)
    return IndoorBikeData(flags=flags, **f)


def encode_indoor_bike_data(
    *,
    power_w: int | None = None,
    cadence_rpm: float | None = None,
    speed_kmh: float | None = None,
    avg_power_w: int | None = None,
    resistance_level: int | None = None,
    total_distance_m: int | None = None,
    heart_rate: int | None = None,
    elapsed_time_s: int | None = None,
    remaining_time_s: int | None = None,
) -> bytes:
    """Build an Indoor Bike Data notification from SI values. The flag bits for present
    fields are set automatically; the More-Data bit0 is set (speed absent) only when no
    `speed_kmh` is given — matching the spec's inverted speed-present rule."""
    flags = 0
    out = bytearray()

    if speed_kmh is None:
        flags |= IBD_MORE_DATA                # no speed -> More-Data bit set
    else:
        out += round(speed_kmh * 100).to_bytes(2, "little")
    if cadence_rpm is not None:
        flags |= IBD_INST_CADENCE
        out += round(cadence_rpm * 2).to_bytes(2, "little")
    if total_distance_m is not None:
        flags |= IBD_TOTAL_DISTANCE
        out += int(total_distance_m).to_bytes(3, "little")
    if resistance_level is not None:
        flags |= IBD_RESISTANCE
        out += int(resistance_level).to_bytes(2, "little", signed=True)
    if power_w is not None:
        flags |= IBD_INST_POWER
        out += int(power_w).to_bytes(2, "little", signed=True)
    if avg_power_w is not None:
        flags |= IBD_AVG_POWER
        out += int(avg_power_w).to_bytes(2, "little", signed=True)
    if heart_rate is not None:
        flags |= IBD_HEART_RATE
        out += int(heart_rate).to_bytes(1, "little")
    if elapsed_time_s is not None:
        flags |= IBD_ELAPSED_TIME
        out += int(elapsed_time_s).to_bytes(2, "little")
    if remaining_time_s is not None:
        flags |= IBD_REMAINING_TIME
        out += int(remaining_time_s).to_bytes(2, "little")

    return flags.to_bytes(2, "little") + bytes(out)


# ============================ Control Point ============================

@dataclass(frozen=True)
class ControlPointResponse:
    """A Fitness Machine Control Point indication: 0x80 <req-op> <result> [params]."""

    request_opcode: int
    result: int
    params: bytes = b""

    @property
    def success(self) -> bool:
        return self.result == CP_SUCCESS

    @property
    def result_name(self) -> str:
        return CP_RESULT_NAMES.get(self.result, f"0x{self.result:02X}")


@dataclass(frozen=True)
class ControlPointRequest:
    """A decoded write TO the control point (the server's view): op + parsed params."""

    opcode: int
    params: bytes = b""

    @property
    def target_power_w(self) -> int | None:
        if self.opcode == CP_SET_TARGET_POWER and len(self.params) >= 2:
            return int.from_bytes(self.params[:2], "little", signed=True)
        return None


def encode_control_point(opcode: int, params: bytes = b"") -> bytes:
    return bytes([opcode]) + params


def encode_request_control() -> bytes:
    return encode_control_point(CP_REQUEST_CONTROL)


def encode_reset() -> bytes:
    return encode_control_point(CP_RESET)


def encode_start() -> bytes:
    return encode_control_point(CP_START_RESUME)


def encode_stop(*, pause: bool = False) -> bytes:
    return encode_control_point(CP_STOP_PAUSE, bytes([0x02 if pause else 0x01]))


def encode_set_target_power(watts: int) -> bytes:
    """The erg op: Set Target Power 0x05 + sint16 LE watts."""
    return encode_control_point(CP_SET_TARGET_POWER, int(watts).to_bytes(2, "little", signed=True))


def encode_set_targeted_cadence(rpm: float) -> bytes:
    return encode_control_point(CP_SET_TARGETED_CADENCE, round(rpm * 2).to_bytes(2, "little"))


def encode_set_indoor_bike_sim(
    wind_mps: float = 0.0, grade_pct: float = 0.0, crr: float = 0.0, cw: float = 0.0
) -> bytes:
    """Set Indoor Bike Simulation Parameters (0x11): wind s16 1/1000 m/s, grade s16
    1/100 %, Crr u8 1/10000, Cw u8 1/100 kg/m."""
    return encode_control_point(CP_SET_INDOOR_BIKE_SIM,
                                round(wind_mps * 1000).to_bytes(2, "little", signed=True)
                                + round(grade_pct * 100).to_bytes(2, "little", signed=True)
                                + bytes([round(crr * 10000) & 0xFF, round(cw * 100) & 0xFF]))


def encode_control_point_response(request_opcode: int, result: int = CP_SUCCESS,
                                  params: bytes = b"") -> bytes:
    """The indication a machine returns: 0x80 <req-op> <result> [params]."""
    return bytes([CP_RESPONSE, request_opcode, result]) + params


def decode_control_point(data: bytes) -> ControlPointResponse | ControlPointRequest | None:
    """A value starting with 0x80 -> ControlPointResponse; otherwise a write request
    (the server side) -> ControlPointRequest."""
    if not data:
        return None
    if data[0] == CP_RESPONSE and len(data) >= 3:
        return ControlPointResponse(request_opcode=data[1], result=data[2], params=bytes(data[3:]))
    return ControlPointRequest(opcode=data[0], params=bytes(data[1:]))


# ============================ Feature / ranges / status ============================

@dataclass(frozen=True)
class FitnessMachineFeature:
    machine_features: int
    target_features: int

    @property
    def power_measurement(self) -> bool:
        return bool(self.machine_features & FEAT_POWER_MEASUREMENT)

    @property
    def cadence(self) -> bool:
        return bool(self.machine_features & FEAT_CADENCE)

    @property
    def power_target_setting(self) -> bool:
        """Whether the machine accepts Set Target Power (erg)."""
        return bool(self.target_features & TGT_POWER)

    @property
    def indoor_bike_sim(self) -> bool:
        return bool(self.target_features & TGT_INDOOR_BIKE_SIM)


def decode_fitness_machine_feature(data: bytes) -> FitnessMachineFeature:
    if len(data) < 8:
        raise ValueError(f"fitness machine feature too short ({len(data)} bytes)")
    return FitnessMachineFeature(
        machine_features=int.from_bytes(data[0:4], "little"),
        target_features=int.from_bytes(data[4:8], "little"),
    )


def encode_fitness_machine_feature(machine_features: int, target_features: int) -> bytes:
    return (machine_features & 0xFFFFFFFF).to_bytes(4, "little") + \
        (target_features & 0xFFFFFFFF).to_bytes(4, "little")


@dataclass(frozen=True)
class PowerRange:
    minimum: int
    maximum: int
    increment: int

    def clamp(self, watts: int) -> int:
        return max(self.minimum, min(self.maximum, watts))


def decode_supported_power_range(data: bytes) -> PowerRange:
    """Supported Power Range (0x2AD8): s16 min, s16 max, u16 increment (watts)."""
    if len(data) < 6:
        raise ValueError(f"supported power range too short ({len(data)} bytes)")
    return PowerRange(
        minimum=int.from_bytes(data[0:2], "little", signed=True),
        maximum=int.from_bytes(data[2:4], "little", signed=True),
        increment=int.from_bytes(data[4:6], "little"),
    )


def encode_supported_power_range(minimum: int, maximum: int, increment: int) -> bytes:
    return (int(minimum).to_bytes(2, "little", signed=True)
            + int(maximum).to_bytes(2, "little", signed=True)
            + int(increment).to_bytes(2, "little"))


@dataclass(frozen=True)
class FitnessMachineStatus:
    opcode: int
    params: bytes = b""

    @property
    def target_power_w(self) -> int | None:
        if self.opcode == ST_TARGET_POWER_CHANGED and len(self.params) >= 2:
            return int.from_bytes(self.params[:2], "little", signed=True)
        return None


def decode_fitness_machine_status(data: bytes) -> FitnessMachineStatus | None:
    if not data:
        return None
    return FitnessMachineStatus(opcode=data[0], params=bytes(data[1:]))


def encode_fitness_machine_status(opcode: int, params: bytes = b"") -> bytes:
    return bytes([opcode]) + params


@dataclass(frozen=True)
class TrainingStatus:
    flags: int
    status: int
    string: str = ""


def decode_training_status(data: bytes) -> TrainingStatus | None:
    if len(data) < 2:
        return None
    s = ""
    if len(data) > 2:
        s = data[2:].split(b"\x00", 1)[0].decode("utf-8", "replace")
    return TrainingStatus(flags=data[0], status=data[1], string=s)


# ============================ spec-derived golden vectors ============================
# Hand-built to the FTMS spec to pin the codec, NOT real captures. Replace / cross-check
# with the real SB20 frames once captured (Session 4 §C). (label, hex, note)
SPEC_VECTORS: list[tuple[str, str, str]] = [
    ("ibd_speed_cadence_power", "4400b80bb400c800",
     "flags 0x0044 (speed via More-Data=0, cadence, power): 30.00 km/h, 90 rpm, 200 W"),
    ("cp_set_target_power_250", "05fa00", "Set Target Power 0x05 + 250 W (s16 LE)"),
    ("cp_response_success", "800501", "0x80 + req-op 0x05 + success"),
    ("cp_response_not_permitted", "800505", "0x80 + req-op 0x05 + control-not-permitted"),
    ("feature_power_cadence_ergcap", "0240000008000000",
     "machine: cadence(b1)|power-measurement(b14)=0x4002; target: power-setting(b3)=0x08"),
    ("power_range_0_1000_1", "0000e8030100", "min 0, max 1000, increment 1 W"),
    ("status_target_power_changed_200", "08c800", "Target Power Changed -> 200 W"),
]
