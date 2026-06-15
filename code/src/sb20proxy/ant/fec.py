"""ANT+ FE-C (Fitness Equipment Control) page codec — the trainer side.

The SB20 (and a TacX Neo, and any smart trainer) speaks FE-C (device type 0x11):
- page 0x10 General FE Data (speed/distance/state),
- page 0x19 Specific Trainer Data (instantaneous + accumulated power, cadence),
- page 0x31 Target Power (the *control* page a head unit sends to set erg power).

`decode_fec_page` mirrors the Phase 0-validated decoder in `07_capture_multi.py`;
`encode_fec_page` is its inverse, refilling the bytes the decoder drops with the
values observed across all 777 real SB20 FE-C records (HR 0xFF, capabilities nibble
0x4, status bit7 0). This is the foundation for a TrainerTwin (a software smart
trainer that reports power and accepts erg targets).

Reference: ANT+ FE-C profile D000001231; power layout per openant fitness_equipment.py.
"""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from sb20proxy.ant.pages import UnknownPage

DEVICE_TYPE_FEC = 0x11  # 17

PAGE_GENERAL_FE = 0x10
PAGE_TRAINER_DATA = 0x19
PAGE_TARGET_POWER = 0x31  # control: head unit -> trainer

# Constant fills the decoder doesn't store (verified across all 777 SB20 records).
_GEN_HR = 0xFF
_GEN_CAPABILITIES_NIBBLE = 0x4
POWER_INVALID = 0x0FFF


def _u8(v: Any, name: str) -> int:
    v = int(v)
    if not 0 <= v <= 0xFF:
        raise ValueError(f"{name}={v} out of range 0..255")
    return v


def _u16le(v: int) -> list[int]:
    return [v & 0xFF, (v >> 8) & 0xFF]


def encode_fec_page(fields: Mapping[str, Any]) -> bytes:
    """Inverse of decode_fec_page for the FE-C data pages (0x10, 0x19)."""
    page = _u8(fields["page"], "page")
    pm = page & 0x7F
    if pm == PAGE_TRAINER_DATA:
        power = fields.get("instantaneous_power_w")
        pwr_field = POWER_INVALID if power is None else int(power)
        cadence = fields.get("instantaneous_cadence_rpm")
        out = [
            page,
            _u8(fields["event_count"], "event_count"),
            0xFF if cadence is None else _u8(cadence, "cadence"),
            *_u16le(int(fields["accumulated_power"]) & 0xFFFF),
            pwr_field & 0xFF,
            ((int(fields.get("trainer_status_bits", 0)) & 0x0F) << 4) | ((pwr_field >> 8) & 0x0F),
            ((int(fields.get("fe_state", 0)) & 0x07) << 4) | (int(fields.get("flags", 0)) & 0x0F),
        ]
    elif pm == PAGE_GENERAL_FE:
        speed = fields.get("speed_mm_s")
        out = [
            page,
            _u8(fields["equipment_type"], "equipment_type") & 0x1F,
            _u8(fields["elapsed_time_quarter_s"], "elapsed_time_quarter_s"),
            _u8(fields["distance_m"], "distance_m"),
            *_u16le(0xFFFF if speed is None else int(speed)),
            _GEN_HR,
            ((int(fields.get("fe_state", 0)) & 0x07) << 4) | _GEN_CAPABILITIES_NIBBLE,
        ]
    else:
        raise UnknownPage(f"no FE-C encoder for page 0x{pm:02X}")
    assert len(out) == 8
    return bytes(out)


def decode_fec_page(data: bytes) -> dict[str, Any]:
    """Decode an FE-C page (mirrors 07_capture_multi.py:decode_fec)."""
    if len(data) < 8:
        return {"page": None, "raw_hex": data.hex(), "error": "short payload"}
    page = data[0]
    out: dict[str, Any] = {"page": page, "page_hex": f"0x{page:02X}",
                           "raw_hex": data.hex(), "page_no_toggle": page & 0x7F}
    pm = page & 0x7F
    if pm == PAGE_TRAINER_DATA:
        power = data[5] | ((data[6] & 0x0F) << 8)
        out.update({
            "event_count": data[1],
            "instantaneous_cadence_rpm": data[2] if data[2] != 0xFF else None,
            "accumulated_power": int.from_bytes(data[3:5], "little"),
            "instantaneous_power_w": None if power == POWER_INVALID else power,
            "trainer_status_bits": (data[6] >> 4) & 0x0F,
            "fe_state": (data[7] >> 4) & 0x07,
            "flags": data[7] & 0x0F,
        })
    elif pm == PAGE_GENERAL_FE:
        speed = int.from_bytes(data[4:6], "little")
        out.update({
            "equipment_type": data[1] & 0x1F,
            "elapsed_time_quarter_s": data[2],
            "distance_m": data[3],
            "speed_mm_s": None if speed == 0xFFFF else speed,
            "fe_state": (data[7] >> 4) & 0x07,
        })
    if len(data) >= 13:
        out["ext_flag"] = data[8]
        out["ext_device_number"] = int.from_bytes(data[9:11], "little")
        out["ext_device_type"] = data[11]
        out["ext_transmission_type"] = data[12]
    return out


# ---- typed builders ----

def encode_trainer_data(
    *, event_count: int, instantaneous_power_w: int | None, accumulated_power: int,
    cadence_rpm: int | None = None, trainer_status_bits: int = 0,
    fe_state: int = 3, flags: int = 0,
) -> bytes:
    """Page 0x19 Specific Trainer Data (fe_state 3 = IN_USE)."""
    return encode_fec_page({
        "page": PAGE_TRAINER_DATA, "event_count": event_count,
        "instantaneous_power_w": instantaneous_power_w,
        "accumulated_power": accumulated_power, "instantaneous_cadence_rpm": cadence_rpm,
        "trainer_status_bits": trainer_status_bits, "fe_state": fe_state, "flags": flags,
    })


def encode_general_fe_data(
    *, elapsed_time_quarter_s: int, distance_m: int, speed_mm_s: int | None,
    equipment_type: int = 25, fe_state: int = 3,
) -> bytes:
    """Page 0x10 General FE Data (equipment_type 25 = trainer/stationary bike)."""
    return encode_fec_page({
        "page": PAGE_GENERAL_FE, "equipment_type": equipment_type,
        "elapsed_time_quarter_s": elapsed_time_quarter_s, "distance_m": distance_m,
        "speed_mm_s": speed_mm_s, "fe_state": fe_state,
    })


# ---- control: Target Power (page 0x31), head unit -> trainer ----
# Spec-derived (D000001231): bytes 1-5 reserved 0xFF, bytes 6-7 = target power in
# 0.25 W units (LE uint16). Self-consistent encode/decode here; on-air behaviour is
# a bench item (needs a real trainer / the TrainerTwin).

def encode_target_power(watts: float) -> bytes:
    field = max(0, round(watts * 4)) & 0xFFFF  # 0.25 W units
    return bytes([PAGE_TARGET_POWER, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                  field & 0xFF, (field >> 8) & 0xFF])


def decode_target_power(data: bytes) -> float:
    if (data[0] & 0x7F) != PAGE_TARGET_POWER:
        raise ValueError(f"not a Target Power page: 0x{data[0]:02X}")
    return int.from_bytes(data[6:8], "little") / 4.0
