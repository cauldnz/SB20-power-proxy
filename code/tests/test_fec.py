"""FE-C codec round-trip against the real SB20 trainer stream + the control page.

The round-trip gate re-encodes every real FE-C 0x10/0x19 page from QUICK-multi and
asserts it reproduces the exact bytes (same discipline as the Bike Power codec). The
Target Power control page (0x31) is spec-derived and tested for self-consistency; its
on-air behaviour is a bench item (a real trainer / the TrainerTwin).
"""

from __future__ import annotations

import json

from sb20proxy.ant.fec import (
    PAGE_GENERAL_FE,
    PAGE_TRAINER_DATA,
    decode_fec_page,
    decode_target_power,
    encode_fec_page,
    encode_general_fe_data,
    encode_target_power,
    encode_trainer_data,
)

QUICK = "QUICK-multi-20260615-064037.jsonl"


def _fec_pages(captures_dir):
    with open(captures_dir / QUICK) as fh:
        for line in fh:
            rec = json.loads(line)
            if rec.get("source") != "bike_fec" or rec.get("kind") != "broadcast":
                continue
            raw_hex = (rec.get("data") or {}).get("raw_hex")
            if not raw_hex:
                continue
            raw = bytes.fromhex(raw_hex)[:8]
            if len(raw) == 8 and (raw[0] & 0x7F) in (PAGE_GENERAL_FE, PAGE_TRAINER_DATA):
                yield raw


def test_fec_roundtrip_real_records(captures_dir):
    count = 0
    pages = set()
    for raw in _fec_pages(captures_dir):
        assert encode_fec_page(decode_fec_page(raw)) == raw, (
            f"FE-C round-trip mismatch: {raw.hex()}"
        )
        pages.add(raw[0] & 0x7F)
        count += 1
    assert count > 100, f"only {count} FE-C data records"
    assert pages == {PAGE_GENERAL_FE, PAGE_TRAINER_DATA}


def test_trainer_data_known_vector():
    # real sample: 19 a1 00 91 d4 00 00 33 -> accum 0xD491, power 0, cadence 0
    raw = bytes.fromhex("19a10091d4000033")
    d = decode_fec_page(raw)
    assert d["accumulated_power"] == 0xD491
    assert d["instantaneous_power_w"] == 0
    assert encode_fec_page(d) == raw


def test_general_fe_known_vector():
    # real sample: 10 19 3b 80 5c 09 ff 34 -> equipment 25, speed 0x095c
    raw = bytes.fromhex("10193b805c09ff34")
    d = decode_fec_page(raw)
    assert d["equipment_type"] == 25
    assert d["speed_mm_s"] == 0x095C
    assert encode_fec_page(d) == raw


def test_trainer_data_power_roundtrips():
    b = encode_trainer_data(event_count=5, instantaneous_power_w=287,
                            accumulated_power=1000, cadence_rpm=90)
    d = decode_fec_page(b)
    assert d["instantaneous_power_w"] == 287
    assert d["instantaneous_cadence_rpm"] == 90
    assert d["accumulated_power"] == 1000


def test_invalid_power_roundtrips_to_none():
    b = encode_trainer_data(event_count=1, instantaneous_power_w=None, accumulated_power=0)
    assert decode_fec_page(b)["instantaneous_power_w"] is None


def test_general_fe_builder():
    b = encode_general_fe_data(elapsed_time_quarter_s=10, distance_m=5, speed_mm_s=2396)
    assert decode_fec_page(b)["speed_mm_s"] == 2396


def test_target_power_control_page():
    # 250 W -> 0.25 W units = 1000 = 0x03E8 at bytes 6-7
    b = encode_target_power(250)
    assert b == bytes.fromhex("31ffffffffffe803")
    assert decode_target_power(b) == 250.0
