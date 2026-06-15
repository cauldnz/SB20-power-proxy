"""ANT+ Bike Power page codec — decode and encode the 8-byte data pages.

`encode_page` is the precise inverse of `decode_page`; together they let the
proxy receive a Stages crank stream and re-transmit it as a spoofed master.

REAL-DATA-FIRST PROVENANCE
--------------------------
The byte layouts here are NOT transcribed from the ANT+ spec from memory — they
mirror `code/scripts/01_capture_stages.py:decode_page`, which was validated in
Phase 0 against the real Stages crank and the Stages app. Every layout in this
module round-trips against the committed Phase 0 captures (see
`tests/test_ant_pages.py`): for all 3,209 real data records across
`A-stagesL-steady-20260614-165737.jsonl` and `C0-ack-dryrun-20260614-164426.jsonl`,
`encode_page(decode_page(raw)) == raw`.

The decoder is lossy on reserved bytes (it doesn't store them), so the encoder
must refill them with the values that were actually on the wire. Those were
confirmed constant at 0xFF across every captured record:

    page 0x13  bytes 6,7   = FF FF      (after the 4 TE/PS bytes)
    page 0x50  bytes 1,2   = FF FF      (reserved before HW rev)
    page 0x51  byte 1      = FF         (reserved before SW rev)
    page 0x52  byte 1      = FF         (reserved before battery id)
    page 0x01  bytes 3,4,5 = FF FF FF   (reserved in the calibration page)

Field names match `decode_page` output exactly, so `encode_page` consumes a
decoded dict directly and Phase 2 can build the same dict from a `PowerReading`.

NOTE: `decode_page` below is mirrored verbatim from the Phase 0 capture script so
this package is a self-contained codec. The two copies must stay in sync; a
follow-up should make the capture script import this one. Do not "simplify" the
byte offsets — e.g. manufacturer_id genuinely lives at bytes 4-5 of page 0x50.

Reference: ANT+ Bicycle Power Device Profile D00001086 Rev 5.x.
"""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

# ---------------------------------------------------------------------------
# Page IDs. Bit 7 of the page byte is a toggle bit in some ANT+ profiles; mask
# with 0x7F before matching. Bike Power does not toggle its main pages, but the
# codec stays robust if a toggled page ever appears.
# ---------------------------------------------------------------------------
PAGE_CALIBRATION = 0x01
PAGE_POWER_ONLY = 0x10
PAGE_CRANK_TORQUE = 0x12
PAGE_TORQUE_EFFECTIVENESS = 0x13
PAGE_MANUFACTURER_INFO = 0x50
PAGE_PRODUCT_INFO = 0x51
PAGE_BATTERY_STATUS = 0x52

RESERVED = 0xFF  # the fill byte for every undecoded/reserved position (verified on real data)

# Calibration response IDs (page 0x01, byte 1)
CAL_ID_MANUAL_ZERO_SUCCESS = 0xAC
CAL_ID_MANUAL_ZERO_FAIL = 0xAF
CAL_ID_MANUAL_ZERO_REQUEST = 0xAA


class UnknownPage(ValueError):
    """Raised when encode_page is asked to build a page with no encoder here."""


# ---------------------------------------------------------------------------
# Little-endian byte helpers. These are LOUD on out-of-range input: a bad value
# fed in from Phase 2 should crash here naming the field, not silently wrap and
# put a wrong number on the wire.
# ---------------------------------------------------------------------------

def _u8(value: Any, name: str) -> int:
    v = int(value)
    if not 0 <= v <= 0xFF:
        raise ValueError(f"{name}={v} out of range for uint8 (0..255)")
    return v


def _u16le(value: Any, name: str) -> list[int]:
    v = int(value)
    if not 0 <= v <= 0xFFFF:
        raise ValueError(f"{name}={v} out of range for uint16 (0..65535)")
    return [v & 0xFF, (v >> 8) & 0xFF]


def _u24le(value: Any, name: str) -> list[int]:
    v = int(value)
    if not 0 <= v <= 0xFFFFFF:
        raise ValueError(f"{name}={v} out of range for uint24 (0..16777215)")
    return [v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF]


def _u32le(value: Any, name: str) -> list[int]:
    v = int(value)
    if not 0 <= v <= 0xFFFFFFFF:
        raise ValueError(f"{name}={v} out of range for uint32")
    return [v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF]


def _s16le(value: Any, name: str) -> list[int]:
    v = int(value)
    if not -32768 <= v <= 32767:
        raise ValueError(f"{name}={v} out of range for sint16 (-32768..32767)")
    u = v & 0xFFFF
    return [u & 0xFF, (u >> 8) & 0xFF]


def _cadence_byte(value: Any) -> int:
    """Cadence byte: 0xFF means 'invalid/none', else the rpm value."""
    if value is None:
        return RESERVED
    return _u8(value, "instantaneous_cadence_rpm")


def _require(fields: Mapping[str, Any], key: str) -> Any:
    if key not in fields:
        raise KeyError(
            f"encode_page: missing required field {key!r} for this page; "
            f"got keys {sorted(fields)}"
        )
    return fields[key]


# ---------------------------------------------------------------------------
# encode_page — the inverse of decode_page. Consumes a decoded-page dict (same
# field names decode_page emits) and returns the exact 8 wire bytes.
# ---------------------------------------------------------------------------

def encode_page(fields: Mapping[str, Any]) -> bytes:
    """Encode one 8-byte Bike Power data page from a decoded-page dict.

    `fields` uses the same keys as `decode_page`'s output. `fields["page"]` is
    the raw page byte (toggle bit preserved). Reserved positions are filled with
    the values observed on real captures (0xFF). Raises UnknownPage for a page
    with no encoder, ValueError/KeyError (loudly) for bad/missing fields.
    """
    page = _u8(_require(fields, "page"), "page")
    pm = page & 0x7F

    if pm == PAGE_POWER_ONLY:
        out = [
            page,
            _u8(_require(fields, "event_count"), "event_count"),
            _u8(fields.get("pedal_power_raw", RESERVED), "pedal_power_raw"),
            _cadence_byte(fields.get("instantaneous_cadence_rpm")),
            *_u16le(_require(fields, "accumulated_power"), "accumulated_power"),
            *_u16le(_require(fields, "instantaneous_power_w"), "instantaneous_power_w"),
        ]
    elif pm == PAGE_CRANK_TORQUE:
        out = [
            page,
            _u8(_require(fields, "event_count"), "event_count"),
            _u8(_require(fields, "crank_ticks"), "crank_ticks"),
            _cadence_byte(fields.get("instantaneous_cadence_rpm")),
            *_u16le(_require(fields, "accumulated_crank_period"), "accumulated_crank_period"),
            *_u16le(_require(fields, "accumulated_torque"), "accumulated_torque"),
        ]
    elif pm == PAGE_TORQUE_EFFECTIVENESS:
        out = [
            page,
            _u8(_require(fields, "event_count"), "event_count"),
            _u8(_require(fields, "left_te_raw"), "left_te_raw"),
            _u8(_require(fields, "right_te_raw"), "right_te_raw"),
            _u8(_require(fields, "left_ps_raw"), "left_ps_raw"),
            _u8(_require(fields, "right_ps_raw"), "right_ps_raw"),
            # bytes 6,7 reserved — decoder drops them; verified 0xFF on the wire.
            _u8(fields.get("te_reserved_6", RESERVED), "te_reserved_6"),
            _u8(fields.get("te_reserved_7", RESERVED), "te_reserved_7"),
        ]
    elif pm == PAGE_MANUFACTURER_INFO:
        out = [
            page,
            RESERVED, RESERVED,  # bytes 1,2 reserved (verified 0xFF)
            _u8(_require(fields, "hw_revision"), "hw_revision"),
            *_u16le(_require(fields, "manufacturer_id"), "manufacturer_id"),
            *_u16le(_require(fields, "model_number"), "model_number"),
        ]
    elif pm == PAGE_PRODUCT_INFO:
        out = [
            page,
            RESERVED,  # byte 1 reserved (verified 0xFF)
            _u8(_require(fields, "sw_revision_supp"), "sw_revision_supp"),
            _u8(_require(fields, "sw_revision_main"), "sw_revision_main"),
            *_u32le(_require(fields, "serial_number"), "serial_number"),
        ]
    elif pm == PAGE_BATTERY_STATUS:
        out = [
            page,
            RESERVED,  # byte 1 reserved (verified 0xFF)
            _u8(_require(fields, "battery_id"), "battery_id"),
            *_u24le(_require(fields, "operating_time_lsb"), "operating_time_lsb"),
            _u8(_require(fields, "battery_voltage_frac"), "battery_voltage_frac"),
            _u8(_require(fields, "battery_status_byte"), "battery_status_byte"),
        ]
    elif pm == PAGE_CALIBRATION:
        out = [
            page,
            _u8(_require(fields, "calibration_id"), "calibration_id"),
            _u8(fields.get("auto_zero_status", RESERVED), "auto_zero_status"),
            RESERVED, RESERVED, RESERVED,  # bytes 3,4,5 reserved (verified 0xFF)
            *_s16le(_require(fields, "calibration_data"), "calibration_data"),
        ]
    else:
        raise UnknownPage(f"no encoder for page 0x{pm:02X}")

    assert len(out) == 8, f"page 0x{pm:02X} produced {len(out)} bytes, expected 8"
    return bytes(out)


# ---------------------------------------------------------------------------
# Typed per-page builders. These give Phase 1/2 a discoverable, range-checked
# API; each just assembles the decoded-dict shape and calls encode_page so there
# is a single source of truth for every byte layout.
# ---------------------------------------------------------------------------

def pedal_power_byte(balance_pct: int | None, *, differentiated: bool = True) -> int:
    """Build the page 0x10 pedal-power byte. None -> 0xFF (balance not provided)."""
    if balance_pct is None:
        return RESERVED
    if not 0 <= balance_pct <= 100:
        raise ValueError(f"balance_pct={balance_pct} out of range (0..100)")
    return (balance_pct & 0x7F) | (0x80 if differentiated else 0x00)


def encode_power_only(
    *,
    event_count: int,
    instantaneous_power_w: int,
    accumulated_power: int = 0,
    cadence_rpm: int | None = None,
    pedal_power: int = RESERVED,
) -> bytes:
    """Page 0x10 (Power-Only). `pedal_power` is the raw byte (see pedal_power_byte)."""
    return encode_page({
        "page": PAGE_POWER_ONLY,
        "event_count": event_count,
        "pedal_power_raw": pedal_power,
        "instantaneous_cadence_rpm": cadence_rpm,
        "accumulated_power": accumulated_power,
        "instantaneous_power_w": instantaneous_power_w,
    })


def encode_crank_torque(
    *,
    event_count: int,
    crank_ticks: int,
    accumulated_crank_period: int,
    accumulated_torque: int,
    cadence_rpm: int | None = None,
) -> bytes:
    """Page 0x12 (Crank Torque)."""
    return encode_page({
        "page": PAGE_CRANK_TORQUE,
        "event_count": event_count,
        "crank_ticks": crank_ticks,
        "instantaneous_cadence_rpm": cadence_rpm,
        "accumulated_crank_period": accumulated_crank_period,
        "accumulated_torque": accumulated_torque,
    })


def encode_torque_effectiveness(
    *,
    event_count: int,
    left_te_raw: int = RESERVED,
    right_te_raw: int = RESERVED,
    left_ps_raw: int = RESERVED,
    right_ps_raw: int = RESERVED,
) -> bytes:
    """Page 0x13 (Torque Effectiveness / Pedal Smoothness)."""
    return encode_page({
        "page": PAGE_TORQUE_EFFECTIVENESS,
        "event_count": event_count,
        "left_te_raw": left_te_raw,
        "right_te_raw": right_te_raw,
        "left_ps_raw": left_ps_raw,
        "right_ps_raw": right_ps_raw,
    })


def encode_manufacturer_info(*, hw_revision: int, manufacturer_id: int, model_number: int) -> bytes:
    """Common page 0x50 (Manufacturer's Identification). Stages crank: 69 / model 3 / hw 3."""
    return encode_page({
        "page": PAGE_MANUFACTURER_INFO,
        "hw_revision": hw_revision,
        "manufacturer_id": manufacturer_id,
        "model_number": model_number,
    })


def encode_product_info(
    *, sw_revision_main: int, serial_number: int, sw_revision_supp: int = RESERVED
) -> bytes:
    """Common page 0x51 (Product Information). Stages crank: fw 1.8.2 / serial 11821518."""
    return encode_page({
        "page": PAGE_PRODUCT_INFO,
        "sw_revision_supp": sw_revision_supp,
        "sw_revision_main": sw_revision_main,
        "serial_number": serial_number,
    })


def encode_battery_status(
    *, battery_id: int, operating_time_lsb: int, battery_voltage_frac: int, battery_status_byte: int
) -> bytes:
    """Common page 0x52 (Battery Status)."""
    return encode_page({
        "page": PAGE_BATTERY_STATUS,
        "battery_id": battery_id,
        "operating_time_lsb": operating_time_lsb,
        "battery_voltage_frac": battery_voltage_frac,
        "battery_status_byte": battery_status_byte,
    })


def encode_calibration_response(
    *,
    calibration_data: int,
    calibration_id: int = CAL_ID_MANUAL_ZERO_SUCCESS,
    auto_zero_status: int = RESERVED,
) -> bytes:
    """Page 0x01 calibration response. Default is manual-zero SUCCESS (0xAC).

    Captured: offset 903 -> 01 AC FF FF FF FF 87 03 (the value the bike accepts).
    """
    return encode_page({
        "page": PAGE_CALIBRATION,
        "calibration_id": calibration_id,
        "auto_zero_status": auto_zero_status,
        "calibration_data": calibration_data,
    })


def encode_calibration_request(*, calibration_id: int = CAL_ID_MANUAL_ZERO_REQUEST) -> bytes:
    """Page 0x01 manual-zero REQUEST (display -> meter, sent as acknowledged data).

    This is what the SB20 (or the BikeTwin) sends up to the crank to trigger a
    zero-reset; the crank answers with encode_calibration_response. -> 01 AA FF...FF.
    """
    return encode_page({
        "page": PAGE_CALIBRATION,
        "calibration_id": calibration_id,
        "auto_zero_status": RESERVED,
        "calibration_data": -1,  # 0xFFFF — no data in a request
    })


# ---------------------------------------------------------------------------
# decode_page — mirrored verbatim from code/scripts/01_capture_stages.py (the
# Phase 0-validated decoder). Keep the two in sync; the byte offsets are correct
# against D00001086 and were cross-checked against the crank + Stages app.
# ---------------------------------------------------------------------------

def decode_page(data: bytes) -> dict[str, Any]:
    """Decode a Bike Power broadcast page. Returns a dict of decoded fields.

    Always includes 'page' and 'raw_hex'. Other fields depend on the page.
    Unknown pages still produce raw_hex.
    """
    if len(data) < 8:
        return {"page": None, "raw_hex": data.hex(), "error": "short payload"}

    page = data[0]
    page_match = page & 0x7F
    decoded: dict[str, Any] = {
        "page": page,
        "page_hex": f"0x{page:02X}",
        "raw_hex": data.hex(),
    }

    if page_match == PAGE_POWER_ONLY:
        decoded.update({
            "event_count": data[1],
            "pedal_power_raw": data[2],
            "pedal_power_balance": data[2] & 0x7F if data[2] != 0xFF else None,
            "pedal_power_differentiation": bool(data[2] & 0x80) if data[2] != 0xFF else None,
            "instantaneous_cadence_rpm": data[3] if data[3] != 0xFF else None,
            "accumulated_power": int.from_bytes(data[4:6], "little"),
            "instantaneous_power_w": int.from_bytes(data[6:8], "little"),
        })

    elif page_match == PAGE_CRANK_TORQUE:
        decoded.update({
            "event_count": data[1],
            "crank_ticks": data[2],
            "instantaneous_cadence_rpm": data[3] if data[3] != 0xFF else None,
            "accumulated_crank_period": int.from_bytes(data[4:6], "little"),
            "accumulated_torque": int.from_bytes(data[6:8], "little"),
        })

    elif page_match == PAGE_TORQUE_EFFECTIVENESS:
        decoded.update({
            "event_count": data[1],
            "left_te_raw": data[2],
            "right_te_raw": data[3],
            "left_ps_raw": data[4],
            "right_ps_raw": data[5],
        })

    elif page_match == PAGE_MANUFACTURER_INFO:
        decoded.update({
            "hw_revision": data[3],
            "manufacturer_id": int.from_bytes(data[4:6], "little"),
            "model_number": int.from_bytes(data[6:8], "little"),
        })

    elif page_match == PAGE_PRODUCT_INFO:
        decoded.update({
            "sw_revision_supp": data[2],
            "sw_revision_main": data[3],
            "serial_number": int.from_bytes(data[4:8], "little"),
        })

    elif page_match == PAGE_BATTERY_STATUS:
        decoded.update({
            "battery_id": data[2],
            "operating_time_lsb": int.from_bytes(data[3:6], "little"),
            "battery_voltage_frac": data[6],
            "battery_status_byte": data[7],
        })

    elif page_match == PAGE_CALIBRATION:
        decoded.update({
            "calibration_id": data[1],
            "calibration_id_hex": f"0x{data[1]:02X}",
            "auto_zero_status": data[2],
            "calibration_data": int.from_bytes(data[6:8], "little", signed=True),
        })

    decoded["page_toggle_bit"] = bool(page & 0x80)
    decoded["page_no_toggle"] = page & 0x7F

    # Extended-message tail (when ext RX messages are enabled): source channel ID
    # appended after the 8 data bytes. Purely additive; page fields live in 0-7.
    if len(data) >= 13:
        decoded["ext_flag"] = data[8]
        decoded["ext_device_number"] = int.from_bytes(data[9:11], "little")
        decoded["ext_device_type"] = data[11]
        decoded["ext_transmission_type"] = data[12]

    return decoded
