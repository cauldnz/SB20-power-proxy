"""CPS codec — validated against REAL frames captured off the Stages crank + Assioma."""

from __future__ import annotations

import json

import pytest

from sb20proxy.ble import cps

# Real golden frames off the Stages SPM2 crank (G-crank62144-ble-20260615-065556 / -zero).
CRANK_FRAME = "2f00870058fe5b400e0f2b"  # 135 W, balance 44%, torque 23550, crank 3648/11023
CRANK_ZERO = "2f0000005eab7c3a10f635"   # 0 W, crank 4154/13814


def test_decode_real_crank_frame():
    m = cps.decode_cps_measurement(bytes.fromhex(CRANK_FRAME))
    assert m.flags == cps.STAGES_FLAGS == 0x2F
    assert m.power_w == 135
    assert m.pedal_balance == 0x58 and m.balance_pct == 44.0
    assert m.accumulated_torque == 23550
    assert m.cumulative_crank_revs == 3648
    assert m.last_crank_event_time == 11023


def test_encode_reproduces_real_crank_frame():
    out = cps.encode_cps_measurement(
        135, pedal_balance=0x58, accumulated_torque=23550,
        cumulative_crank_revs=3648, last_crank_event_time=11023,
        extra_flags=cps.F_BALANCE_REF_LEFT | cps.F_TORQUE_SOURCE_CRANK,
    )
    assert out.hex() == CRANK_FRAME  # byte-for-byte the captured frame


# Real Assioma DUO BLE CPS frame (flags 0x0023 = balance + ref-left + crank-rev), from
# findings/captures/ASSIOMA-ble-cps-20260622.jsonl — the grounding for L/R balance forwarding.
ASSIOMA_FRAME = "23009e005816134e4d"  # 158 W, balance 88 (44% L / 56% R), crank-rev at offset 5


def test_decode_real_assioma_balance_frame():
    m = cps.decode_cps_measurement(bytes.fromhex(ASSIOMA_FRAME))
    assert m.flags == 0x0023
    assert m.power_w == 158
    assert m.pedal_balance == 88 and m.balance_pct == 44.0   # left-referenced split
    assert m.cumulative_crank_revs is not None               # cadence still decodes behind balance


def test_balance_forwarded_through_spoof_frame():
    # The proxy reads the Assioma's split and re-emits it on the spoofed Stages crank: the balance
    # byte must survive decode -> forward -> re-encode unchanged (firmware does the same).
    src = cps.decode_cps_measurement(bytes.fromhex(ASSIOMA_FRAME))
    spoof = cps.encode_cps_measurement(
        src.power_w, pedal_balance=src.pedal_balance, accumulated_torque=0,
        cumulative_crank_revs=0, last_crank_event_time=0,
        extra_flags=cps.F_BALANCE_REF_LEFT | cps.F_TORQUE_SOURCE_CRANK,
    )
    assert spoof[4] == 88                                    # forwarded split, not a 50/50 default
    assert cps.decode_cps_measurement(spoof).balance_pct == 44.0


def test_decode_zero_power_frame():
    m = cps.decode_cps_measurement(bytes.fromhex(CRANK_ZERO))
    assert m.power_w == 0
    assert m.cumulative_crank_revs == 4154 and m.last_crank_event_time == 13814


def test_power_only_round_trip():
    out = cps.encode_cps_measurement(250)
    assert out.hex() == "0000fa00"  # flags 0, power 250 = 0x00FA LE
    m = cps.decode_cps_measurement(out)
    assert m.flags == 0 and m.power_w == 250 and m.cumulative_crank_revs is None


def test_negative_power_is_signed():
    m = cps.decode_cps_measurement(cps.encode_cps_measurement(-5))
    assert m.power_w == -5


def test_crank_rev_round_trip_sets_flag():
    out = cps.encode_cps_measurement(200, cumulative_crank_revs=10, last_crank_event_time=5120)
    m = cps.decode_cps_measurement(out)
    assert m.flags & cps.F_CRANK_REV
    assert m.cumulative_crank_revs == 10 and m.last_crank_event_time == 5120


def test_wheel_then_crank_field_order():
    out = cps.encode_cps_measurement(
        180, cumulative_wheel_revs=1000, last_wheel_event_time=2048,
        cumulative_crank_revs=20, last_crank_event_time=4096,
    )
    m = cps.decode_cps_measurement(out)
    assert (m.cumulative_wheel_revs, m.last_wheel_event_time) == (1000, 2048)
    assert (m.cumulative_crank_revs, m.last_crank_event_time) == (20, 4096)


def test_calibration_response_round_trip():
    assert cps.encode_calibration_response(0).hex() == "200c010000"  # the real zero-reset reply
    r = cps.decode_control_point(bytes.fromhex("200c010000"))
    assert isinstance(r, cps.ControlPointResponse)
    assert r.request_opcode == cps.CP_START_OFFSET_COMPENSATION
    assert r.success and r.offset == 0


def test_calibration_negative_offset():
    r = cps.decode_control_point(bytes.fromhex("200c01ffff"))  # Assioma returned offset -1
    assert isinstance(r, cps.ControlPointResponse) and r.offset == -1


def test_control_point_request_decode():
    r = cps.decode_control_point(bytes([cps.CP_START_OFFSET_COMPENSATION]))
    assert r == ("request", cps.CP_START_OFFSET_COMPENSATION, b"")


def test_cadence_from_crank_two_samples():
    # 96 rpm -> 640 ticks/rev; 8 revs over 5120 ticks.
    assert abs(cps.cadence_rpm_from_crank(0, 0, 8, 5120) - 96.0) < 0.1


def test_crank_cadence_advance_recovers_rpm():
    c = cps.CrankCadence()
    for _ in range(5):
        c.advance(96.0, 1000)
    assert c.cumulative_revs == 8  # 1.6 rev/s * 5 s
    assert abs(cps.cadence_rpm_from_crank(0, 0, c.cumulative_revs, c.last_event_time) - 96.0) < 0.5


def test_short_frame_raises():
    with pytest.raises(ValueError):
        cps.decode_cps_measurement(b"\x00\x00")  # flags only, no power


def test_decoder_matches_every_captured_crank_frame(captures_dir):
    """Real-data sweep: decode every CPS notification in the committed crank capture and
    confirm my decoder reproduces the values the capture script recorded."""
    path = captures_dir / "G-crank62144-ble-20260615-065556.jsonl"
    n = 0
    with open(path) as fh:
        for line in fh:
            rec = json.loads(line)
            if (rec.get("kind") != "ble_notification"
                    or rec.get("char") != "cycling_power_measurement"):
                continue
            d = rec.get("data") or {}
            if "raw_hex" not in d or "instantaneous_power_w" not in d:
                continue
            m = cps.decode_cps_measurement(bytes.fromhex(d["raw_hex"]))
            assert m.power_w == d["instantaneous_power_w"]
            if d.get("cumulative_crank_revs") is not None:
                assert m.cumulative_crank_revs == d["cumulative_crank_revs"]
            if d.get("accumulated_torque_raw") is not None:
                assert m.accumulated_torque == d["accumulated_torque_raw"]
            n += 1
    assert n > 20, "should have decoded many real crank frames"
