"""Unit tests for StagesAntTarget page generation and calibration handling.

These exercise the target's logic directly (no radio opened): the next-page
provider and the ack handler. Every emitted page is decoded with the real
decoder and checked. Full-pipeline behaviour is in test_loopback.py.
"""

from __future__ import annotations

import pytest

from sb20proxy.ant import (
    PAGE_CALIBRATION,
    PAGE_MANUFACTURER_INFO,
    PAGE_POWER_ONLY,
    decode_page,
    encode_calibration_request,
)
from sb20proxy.ant.master import LoopbackMaster
from sb20proxy.reading import PowerReading
from sb20proxy.targets.stages_ant import StagesAntTarget


def _target(**kw) -> StagesAntTarget:
    # A LoopbackMaster is fine to construct without opening it.
    return StagesAntTarget(LoopbackMaster(), **kw)


def _reading(power, **kw) -> PowerReading:
    return PowerReading(timestamp=0.0, power_w=power, source_id="test", **kw)


def test_decoded_emits_power_only_from_reading():
    t = _target()
    t.push_reading(_reading(250, cadence_rpm=90, left_balance=49))
    d = decode_page(t._next_page())
    assert (d["page"] & 0x7F) == PAGE_POWER_ONLY
    assert d["instantaneous_power_w"] == 250
    assert d["instantaneous_cadence_rpm"] == 90
    assert d["pedal_power_balance"] == 49


def test_accumulated_power_and_event_count_advance():
    t = _target()
    t.push_reading(_reading(100))
    d1 = decode_page(t._next_page())
    d2 = decode_page(t._next_page())
    assert (d2["accumulated_power"] - d1["accumulated_power"]) == 100
    assert d2["event_count"] != d1["event_count"]


def test_no_reading_yet_emits_zero_power():
    t = _target()
    d = decode_page(t._next_page())
    assert d["instantaneous_power_w"] == 0


def test_zero_reset_request_triggers_0xAC_response():
    t = _target()
    t.push_reading(_reading(100))
    t._on_ack(encode_calibration_request())
    d = decode_page(t._next_page())
    assert (d["page"] & 0x7F) == PAGE_CALIBRATION
    assert d["calibration_id"] == 0xAC
    assert d["calibration_data"] == 903


def test_non_calibration_ack_is_ignored():
    t = _target()
    t.push_reading(_reading(100))
    # A power page arriving as ack is not a calibration request -> next page is data.
    from sb20proxy.ant import encode_power_only
    t._on_ack(encode_power_only(event_count=1, instantaneous_power_w=50))
    d = decode_page(t._next_page())
    assert (d["page"] & 0x7F) == PAGE_POWER_ONLY


def test_commons_burst_is_scheduled():
    t = _target(commons_every=4)
    t.push_reading(_reading(100))
    seen = {decode_page(t._next_page())["page"] & 0x7F for _ in range(6)}
    assert PAGE_MANUFACTURER_INFO in seen  # 0x50 with manufacturer id appears


def test_commons_carry_stages_identity():
    t = _target(commons_every=1)
    t.push_reading(_reading(100))
    mfr = None
    for _ in range(6):
        d = decode_page(t._next_page())
        if (d["page"] & 0x7F) == PAGE_MANUFACTURER_INFO:
            mfr = d["manufacturer_id"]
            break
    assert mfr == 69


def test_verbatim_cycles_pages_exactly():
    p1 = bytes.fromhex("10c8b93349b24100")
    p2 = bytes.fromhex("12c7c735fc6110b8")
    t = _target(mode="verbatim", verbatim_pages=[p1, p2])
    got = [t._next_page() for _ in range(5)]
    assert got == [p1, p2, p1, p2, p1]


def test_verbatim_requires_pages():
    with pytest.raises(ValueError):
        _target(mode="verbatim")


def test_bad_mode_rejected():
    with pytest.raises(ValueError):
        _target(mode="nonsense")
