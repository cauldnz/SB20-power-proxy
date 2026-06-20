"""FTMS codec (ble/ftms.py) — encode/decode round-trips + the spec-derived vectors.

SPEC-BUILT: validated against the Bluetooth FTMS spec layout (not real captures yet).
The SPEC_VECTORS are hand-built to the spec; these tests pin both the vectors and the
codec. Session 4 §C (capture_ftms.py --erg) will supply the real golden frames.
"""

from __future__ import annotations

import pytest

from sb20proxy.ble import ftms


def hx(s: str) -> bytes:
    return bytes.fromhex(s)


# ---- spec-derived vectors ----

def test_spec_vectors_present_and_hex():
    assert {label for label, _h, _n in ftms.SPEC_VECTORS} >= {
        "ibd_speed_cadence_power", "cp_set_target_power_250", "cp_response_success",
        "feature_power_cadence_ergcap", "power_range_0_1000_1",
    }
    for _label, h, _note in ftms.SPEC_VECTORS:
        bytes.fromhex(h)  # all valid hex


def test_vector_indoor_bike_data():
    d = ftms.decode_indoor_bike_data(hx("4400b80bb400c800"))
    assert d.speed_kmh == 30.0 and d.cadence_rpm == 90.0 and d.power_w == 200
    assert not (d.flags & ftms.IBD_MORE_DATA)  # speed present -> bit0 clear


def test_vector_control_point_set_target_power():
    req = ftms.decode_control_point(hx("05fa00"))
    assert isinstance(req, ftms.ControlPointRequest)
    assert req.opcode == ftms.CP_SET_TARGET_POWER and req.target_power_w == 250


def test_vector_control_point_responses():
    ok = ftms.decode_control_point(hx("800501"))
    assert isinstance(ok, ftms.ControlPointResponse) and ok.success
    assert ok.request_opcode == ftms.CP_SET_TARGET_POWER
    no = ftms.decode_control_point(hx("800505"))
    assert not no.success and no.result_name == "control_not_permitted"


def test_vector_feature_and_power_range_and_status():
    feat = ftms.decode_fitness_machine_feature(hx("0240000008000000"))
    assert feat.cadence and feat.power_measurement and feat.power_target_setting
    pr = ftms.decode_supported_power_range(hx("0000e8030100"))
    assert (pr.minimum, pr.maximum, pr.increment) == (0, 1000, 1)
    st = ftms.decode_fitness_machine_status(hx("08c800"))
    assert st.opcode == ftms.ST_TARGET_POWER_CHANGED and st.target_power_w == 200


# ---- Indoor Bike Data round-trips ----

def test_ibd_encode_reproduces_the_spec_vector():
    # SI encode of (30 km/h, 90 rpm, 200 W) must equal the documented frame
    assert ftms.encode_indoor_bike_data(power_w=200, cadence_rpm=90, speed_kmh=30.0).hex() \
        == "4400b80bb400c800"


def test_ibd_more_data_inversion():
    # no speed -> More-Data bit set; with speed -> clear
    no_speed = ftms.encode_indoor_bike_data(power_w=200)
    assert ftms.decode_indoor_bike_data(no_speed).flags & ftms.IBD_MORE_DATA
    assert ftms.decode_indoor_bike_data(no_speed).speed_kmh is None
    with_speed = ftms.encode_indoor_bike_data(power_w=200, speed_kmh=25.0)
    assert not (ftms.decode_indoor_bike_data(with_speed).flags & ftms.IBD_MORE_DATA)


def test_ibd_full_field_roundtrip():
    raw = ftms.encode_indoor_bike_data(
        power_w=250, cadence_rpm=85, speed_kmh=32.5, avg_power_w=240,
        resistance_level=-3, total_distance_m=12345, heart_rate=150,
        elapsed_time_s=600, remaining_time_s=1200)
    d = ftms.decode_indoor_bike_data(raw)
    assert d.power_w == 250 and d.average_power == 240
    assert d.cadence_rpm == 85.0 and d.speed_kmh == 32.5
    assert d.resistance_level == -3 and d.total_distance == 12345
    assert d.heart_rate == 150 and d.elapsed_time == 600 and d.remaining_time == 1200


def test_ibd_negative_power_is_signed():
    d = ftms.decode_indoor_bike_data(ftms.encode_indoor_bike_data(power_w=-10))
    assert d.power_w == -10


# ---- Control Point encoders ----

def test_control_point_encoders():
    assert ftms.encode_request_control().hex() == "00"
    assert ftms.encode_start().hex() == "07"
    assert ftms.encode_stop().hex() == "0801"
    assert ftms.encode_stop(pause=True).hex() == "0802"
    assert ftms.encode_set_target_power(250).hex() == "05fa00"
    assert ftms.encode_set_target_power(-5).hex() == "05fbff"
    assert ftms.encode_reset().hex() == "01"


def test_control_point_sim_params_roundtrip():
    raw = ftms.encode_set_indoor_bike_sim(grade_pct=2.5)
    req = ftms.decode_control_point(raw)
    assert req.opcode == ftms.CP_SET_INDOOR_BIKE_SIM
    # grade is s16 1/100 % -> 2.5% == 250
    assert int.from_bytes(req.params[2:4], "little", signed=True) == 250


def test_control_point_response_roundtrip():
    raw = ftms.encode_control_point_response(ftms.CP_SET_TARGET_POWER, ftms.CP_SUCCESS)
    resp = ftms.decode_control_point(raw)
    assert isinstance(resp, ftms.ControlPointResponse) and resp.success


# ---- Feature / range / status ----

def test_feature_encode_decode_roundtrip():
    raw = ftms.encode_fitness_machine_feature(
        ftms.FEAT_CADENCE | ftms.FEAT_POWER_MEASUREMENT, ftms.TGT_POWER | ftms.TGT_INDOOR_BIKE_SIM)
    f = ftms.decode_fitness_machine_feature(raw)
    assert f.cadence and f.power_measurement and f.power_target_setting and f.indoor_bike_sim


def test_power_range_clamp():
    pr = ftms.decode_supported_power_range(ftms.encode_supported_power_range(50, 400, 5))
    assert pr.clamp(1000) == 400 and pr.clamp(10) == 50 and pr.clamp(200) == 200


def test_status_started_and_permission_lost():
    assert ftms.decode_fitness_machine_status(hx("04")).opcode == ftms.ST_STARTED_RESUMED
    assert ftms.decode_fitness_machine_status(hx("ff")).opcode == ftms.ST_CONTROL_PERMISSION_LOST


def test_training_status_decode():
    ts = ftms.decode_training_status(hx("0103") + b"Manual\x00")
    assert ts.flags == 1 and ts.status == 3 and ts.string == "Manual"


# ---- robustness ----

@pytest.mark.parametrize("bad", ["", "44", "440010", "05"])
def test_truncated_inputs(bad):
    # IBD frames that claim fields but are too short must raise; CP set-power with no
    # param decodes but yields no target.
    data = hx(bad)
    if bad in ("", "44"):
        with pytest.raises(ValueError):
            ftms.decode_indoor_bike_data(data)
    elif bad == "440010":  # claims speed (bit0=0) + cadence (bit2) but truncated
        with pytest.raises(ValueError):
            ftms.decode_indoor_bike_data(data)
    else:  # "05" set-target-power with no watts
        assert ftms.decode_control_point(data).target_power_w is None
