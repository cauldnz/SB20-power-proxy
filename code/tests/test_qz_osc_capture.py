"""Unit tests for scripts/qz_osc_capture.py — the OSC decoder used to record a live QZ ride.

Hermetic: no socket is opened. The decoder is the whole risk in that script (the rest is a recv
loop), and it runs once per second for a whole class with nobody watching, so the cases that matter
are the ones where a datagram is not what we assumed: a truncated bundle, an unknown type tag, a
bare message. A decoder that raises mid-ride loses the ride.

Grounded in the real capture: `code/findings/captures/QZ-osc-peloton-20260927.jsonl` came out of
this decoder on a 43-minute Peloton class, 2026-09-27. `test_field_set_matches_the_real_ride` pins
the field set that capture actually contains — including the *absence* of QZ's Peloton class fields,
a finding in its own right (`code/findings/peloton-integration.md` §0.3).
"""

from __future__ import annotations

import gzip
import importlib.util
import json
import struct
from pathlib import Path

import pytest

_ROOT = Path(__file__).resolve().parents[2]
_SCRIPT = _ROOT / "code" / "scripts" / "qz_osc_capture.py"
_CAPTURE = _ROOT / "code" / "findings" / "captures" / "QZ-osc-peloton-20260927.jsonl.gz"


def _load():
    spec = importlib.util.spec_from_file_location("qz_osc_capture", _SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


qz = _load()


def test_self_test_passes() -> None:
    """The script's own --self-test path is the thing a bench operator runs; it must be honest."""
    assert qz.self_test() == 0


def test_round_trips_a_qz_shaped_bundle() -> None:
    fields = [("/QZ/Watts", "f", 231.5), ("/QZ/Cadence", "f", 88.0),
              ("/QZ/PowerZone", "i", 4), ("/QZ/ElapsedTime", "s", "0:12:34")]
    got = qz.parse_packet(qz.build_bundle(fields))
    assert got == {"/QZ/Watts": 231.5, "/QZ/Cadence": 88.0,
                   "/QZ/PowerZone": 4, "/QZ/ElapsedTime": "0:12:34"}


def test_bare_message_without_a_bundle() -> None:
    """OSC allows a naked message; QZ bundles, but the decoder must not assume it."""
    bundle = qz.build_bundle([("/QZ/Watts", "f", 100.0)])
    inner = bundle[qz._BUNDLE_PREFIX_LEN + 4:]      # strip "#bundle", timetag and the size prefix
    assert qz.parse_packet(inner) == {"/QZ/Watts": 100.0}


def test_truncated_bundle_keeps_what_parsed() -> None:
    """A clipped datagram must yield the complete leading messages, not an exception."""
    full = qz.build_bundle([("/QZ/Watts", "f", 250.0), ("/QZ/Cadence", "f", 90.0),
                            ("/QZ/Heart", "i", 155)])
    got = qz.parse_packet(full[:-6])
    assert got["/QZ/Watts"] == 250.0
    assert "/QZ/Heart" not in got          # the clipped tail is dropped, quietly and safely


def test_unknown_type_tag_stops_that_message_only() -> None:
    """An unrecognised tag must not desync the whole bundle."""
    good = qz.build_bundle([("/QZ/Watts", "f", 300.0)])
    addr = b"/QZ/Weird\0\0\0"
    tags = b",x\0\0"
    odd = addr + tags + struct.pack(">i", 7)
    merged = good + struct.pack(">i", len(odd)) + odd
    got = qz.parse_packet(merged)
    assert got["/QZ/Watts"] == 300.0
    assert got["/QZ/Weird"] == []          # parsed as far as it safely could


def test_negative_or_zero_size_does_not_loop() -> None:
    """A malformed size prefix must end the scan, not spin."""
    buf = qz.OSC_BUNDLE_HEADER + struct.pack(">Q", 1) + struct.pack(">i", 0) + b"junk"
    assert qz.parse_packet(buf) == {}


@pytest.mark.skipif(not _CAPTURE.exists(), reason="the 2026-09-27 ride capture is not present")
def test_field_set_matches_the_real_ride() -> None:
    """Pin what QZ's OSC surface does and does not carry, against the committed ride."""
    with gzip.open(_CAPTURE, "rt", encoding="utf-8") as f:
        rec = json.loads(f.readline())
    fields = {k for k in rec if k.startswith("/QZ/")}
    assert {"/QZ/Watts", "/QZ/Cadence", "/QZ/Resistance", "/QZ/PowerZone",
            "/QZ/TargetPowerZone"} <= fields
    # The finding: QZ's Peloton class fields are computed in the app and never reach OSC. If a
    # future QZ adds them, this fails and someone gets to delete a caveat from the docs.
    assert not [k for k in fields if "eloton" in k], "QZ now exposes Peloton fields over OSC"
