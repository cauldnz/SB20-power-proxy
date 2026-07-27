"""Round-trip gate for the ANT+ Bike Power page codec.

The load-bearing test re-encodes every real captured page and asserts it
reproduces the exact bytes the Stages crank sent. The fixtures are the committed
Phase 0 captures themselves (real data, real device) — not synthetic — so a
green run means "observed", not "assumed" (the real-data-first discipline).

If a future capture introduces a page or a reserved-byte value the encoder
doesn't reproduce, these tests fail loudly and name the mismatch.
"""

from __future__ import annotations

import pytest

from sb20proxy.ant import (
    UnknownPage,
    decode_page,
    encode_calibration_response,
    encode_manufacturer_info,
    encode_page,
    encode_power_only,
    encode_product_info,
)

# A steady ride (all data pages) + a calibration dry-run (adds page 0x01).
CAPTURE_FILES = [
    "A-stagesL-steady-20260614-165737.jsonl",
    "C0-ack-dryrun-20260614-164426.jsonl",
]

ALL_BIKE_POWER_PAGES = {0x01, 0x10, 0x12, 0x13, 0x50, 0x51, 0x52}


def test_capture_fixtures_present(captures_dir):
    for name in CAPTURE_FILES:
        assert (captures_dir / name).exists(), f"missing real capture fixture: {name}"


@pytest.mark.parametrize("filename", CAPTURE_FILES)
def test_encode_from_captured_fields_reproduces_real_bytes(capture_pages, filename):
    """encode_page(<the decoder's own output>) == the real wire bytes, for every record."""
    count = 0
    for decoded, raw in capture_pages(filename):
        got = encode_page(decoded)
        assert got == raw, (
            f"{filename}: page {decoded.get('page_hex')} round-trip mismatch\n"
            f"  encoded: {got.hex()}\n  real:    {raw.hex()}"
        )
        count += 1
    assert count > 100, f"{filename}: only {count} records — capture looks empty"


@pytest.mark.parametrize("filename", CAPTURE_FILES)
def test_pure_byte_roundtrip(capture_pages, filename):
    """encode_page(decode_page(raw)) == raw — codec is self-consistent on real bytes."""
    for _decoded, raw in capture_pages(filename):
        assert encode_page(decode_page(raw)) == raw, f"{filename}: {raw.hex()} did not round-trip"


def test_all_seven_pages_are_exercised(capture_pages):
    """The fixtures must collectively cover every Bike Power page the encoder handles."""
    seen: set[int] = set()
    for name in CAPTURE_FILES:
        for _decoded, raw in capture_pages(name):
            seen.add(raw[0] & 0x7F)
    assert seen == ALL_BIKE_POWER_PAGES, f"pages covered: {sorted(hex(p) for p in seen)}"


# ---- Explicit known-vectors (document each layout against an observed sample) ----

def test_calibration_response_known_vectors():
    # C0-ack-dryrun: offsets +903 and -950, captured as the bytes the bike accepts.
    assert encode_calibration_response(calibration_data=903) == bytes.fromhex("01acffffffff8703")
    assert encode_calibration_response(calibration_data=-950) == bytes.fromhex("01acffffffff4afc")


def test_power_only_known_vector():
    # Real A-steady sample: 10 c8 b9 33 49 b2 41 00
    #   event=0xC8 pedal=0xB9 cadence=0x33(51) accum=0xB249 power=0x0041(65 W)
    b = encode_power_only(
        event_count=0xC8, pedal_power=0xB9, cadence_rpm=0x33,
        accumulated_power=0xB249, instantaneous_power_w=0x0041,
    )
    assert b == bytes.fromhex("10c8b93349b24100")


def test_manufacturer_info_known_vector():
    # Real sample: 50 ff ff 03 45 00 03 00  -> hw 3, mfr 69 (Stages), model 3
    assert encode_manufacturer_info(hw_revision=3, manufacturer_id=69, model_number=3) == \
        bytes.fromhex("50ffff0345000300")


def test_product_info_known_vector():
    # Real sample: 51 ff 02 12 ce 61 b4 00  -> sw_supp 2, sw_main 18, serial 11821518
    assert encode_product_info(sw_revision_main=18, sw_revision_supp=2, serial_number=11821518) == \
        bytes.fromhex("51ff0212ce61b400")


def test_decode_page_reads_extended_message_tail():
    # A real 13-byte extended-message packet (page 0x50 from Stages crank 62144):
    # 8 payload bytes + tail [flag, devnum LE, devtype, transtype] = 80 c0 f2 0b 05
    decoded = decode_page(bytes.fromhex("50ffff034500030080c0f20b05"))
    assert decoded["manufacturer_id"] == 69
    assert decoded["ext_flag"] == 0x80
    assert decoded["ext_device_number"] == 62144
    assert decoded["ext_device_type"] == 11      # 0x0B Bike Power
    assert decoded["ext_transmission_type"] == 5


def test_cadence_none_becomes_0xff():
    b = encode_power_only(event_count=0, instantaneous_power_w=0, cadence_rpm=None)
    assert b[3] == 0xFF


def test_unknown_page_raises():
    with pytest.raises(UnknownPage):
        encode_page({"page": 0x99})


def test_out_of_range_value_fails_loudly():
    with pytest.raises(ValueError):
        encode_power_only(event_count=0, instantaneous_power_w=70000)  # > uint16


def test_missing_field_fails_loudly():
    with pytest.raises(KeyError):
        encode_page({"page": 0x10, "event_count": 1})  # no power fields


def test_partial_extended_tail_is_marked_not_silently_dropped():
    """A started-but-unfinished extended tail is truncation, not a plain broadcast (issue #306).

    An ANT+ page is a fixed 8 bytes; with extended RX messages enabled a 5-byte channel-ID tail
    follows. Anything between those two lengths is a frame that stopped mid-tail, and used to
    decode identically to an ordinary non-extended message.
    """
    page = bytes([0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])

    plain = decode_page(page)
    assert "truncated_at_field" not in plain, "8 bytes is a normal non-extended message"
    assert "ext_flag" not in plain

    full = decode_page(page + bytes([0x80, 0x34, 0x12, 0x0B, 0x05]))
    assert "truncated_at_field" not in full, "13 bytes is a complete extended message"
    assert full["ext_device_number"] == 0x1234

    for n in (1, 2, 3, 4):
        partial = decode_page(page + bytes([0x80, 0x34, 0x12, 0x0B, 0x05])[:n])
        assert partial["truncated_at_field"] == "ext_channel_id", f"{8 + n} bytes"
        assert partial["truncated_missing_bytes"] == 5 - n
        assert "ext_device_number" not in partial, "no half-read device number is reported"


def test_a_short_page_still_reports_short_payload_not_truncation():
    """Below the fixed 8-byte page there is nothing to decode, and that already said so."""
    out = decode_page(bytes([0x10, 0x00]))
    assert out["error"] == "short payload"
    assert "truncated_at_field" not in out
    assert out["raw_hex"] == "1000"