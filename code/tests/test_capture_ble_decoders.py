"""The capture-side CPS decoders in `scripts/06_capture_ble.py`, pinned to real frames.

Two things are pinned here, and the second is the more valuable one.

**The constants.** The script used to hardcode the CPS flag bits (`flags & 0x0004`) and the
control-point op/result codes as bare hex. They are now imported from `sb20proxy.ble.cps`, so
each protocol byte has exactly one definition. Wiring the wrong constant into the wrong branch
is a silent, plausible mistake, so the golden test below replays every Cycling Power Measurement
frame in the committed BLE captures and asserts the decode still equals the dict recorded
alongside it at capture time.

**Why there are two CPS decoders at all.** A reviewer will keep re-filing "these look
duplicated; merge them". They are not duplicates, and the contrast tests at the bottom say so
executably rather than in a comment nobody reads:

  * the script decodes ALL 13 optional CPS fields; `decode_cps_measurement` stops after the four
    our meters actually set (a deliberate runtime decision, documented in that module);
  * the script is TOLERANT — a truncated frame still yields a record: the raw bytes and the
    flags always survive, and fields that did not arrive are simply absent, because a capture
    must never drop a record ("JSONL captures are the canonical lossless record"). The package
    decoder RAISES, which is correct for the runtime path and would be data loss here.

The decoders are pure — no radio, no I/O — but they live in a module whose body imports `bleak`,
which is an optional extra CI does not install. Stubbing that import is what lets these run
hermetically; it is not a workaround for a broken seam.
"""
from __future__ import annotations

import importlib.util
import json
import sys
import types
from pathlib import Path

import pytest

from sb20proxy.ble import cps as pkg_cps

_ROOT = Path(__file__).resolve().parents[1]
_SCRIPT = _ROOT / "scripts" / "06_capture_ble.py"
_CAPTURES = sorted((_ROOT / "findings" / "captures").glob("G-*ble*.jsonl"))


@pytest.fixture(scope="module")
def capture_ble():
    """Load the script with `bleak` stubbed, then put the import system back."""
    saved = sys.modules.get("bleak")
    stub = types.ModuleType("bleak")
    stub.BleakClient = object  # only referenced inside async methods / annotations
    stub.BleakScanner = object
    sys.modules["bleak"] = stub
    try:
        spec = importlib.util.spec_from_file_location("capture_ble_under_test", _SCRIPT)
        assert spec and spec.loader
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        yield module
    finally:
        if saved is not None:
            sys.modules["bleak"] = saved
        else:
            sys.modules.pop("bleak", None)


def _measurement_frames() -> list[tuple[str, dict]]:
    """Every recorded Cycling Power Measurement: (raw_hex, the dict decoded at capture time)."""
    frames: list[tuple[str, dict]] = []
    for path in _CAPTURES:
        with path.open(encoding="utf-8") as fh:
            for line in fh:
                rec = json.loads(line)
                if rec.get("kind") != "ble_notification":
                    continue
                if rec.get("char") != "cycling_power_measurement":
                    continue
                data = rec.get("data") or {}
                if data.get("raw_hex"):
                    frames.append((data["raw_hex"], data))
    return frames


_FRAMES = _measurement_frames()


def test_captures_are_present():
    """A silently empty corpus would make the golden test below pass vacuously."""
    assert _CAPTURES, "no G-*ble*.jsonl captures found - did findings/captures move?"
    assert len(_FRAMES) > 200, f"expected the full CPS corpus, got {len(_FRAMES)} frames"


def test_every_real_measurement_frame_decodes_to_its_recorded_dict(capture_ble):
    """Golden vectors: real bytes off the Stages crank and the Assioma, decoded at capture time.

    This is what catches a mis-wired flag constant. Each frame carries its own expected output,
    so a wrong bit shows up as a changed field rather than as a plausible-looking number.
    """
    mismatches = []
    for raw_hex, recorded in _FRAMES:
        got = capture_ble.decode_cp_measurement(bytes.fromhex(raw_hex))
        if got != recorded:
            mismatches.append((raw_hex, recorded, got))
    assert not mismatches, (
        f"{len(mismatches)} of {len(_FRAMES)} real frames decoded differently than when they "
        f"were captured; first: {mismatches[0]}"
    )


def test_script_uses_the_packages_control_point_codes(capture_ble):
    """The op/result codes are the package's, not a second hand-maintained copy."""
    assert capture_ble.CP_RESPONSE_OPCODE == pkg_cps.CP_RESPONSE_CODE
    assert capture_ble.CP_OPS["request-crank-length"] == pkg_cps.CP_REQUEST_CRANK_LENGTH
    assert capture_ble.CP_OPS["offset-compensation"] == pkg_cps.CP_START_OFFSET_COMPENSATION
    assert (capture_ble.CP_OPS["request-sensor-locations"]
            == pkg_cps.CP_REQUEST_SUPPORTED_SENSOR_LOCATIONS)
    assert capture_ble.CP_RESULT[pkg_cps.CP_RESULT_SUCCESS] == "success"
    assert capture_ble.CP_RESULT[pkg_cps.CP_RESULT_OPERATION_FAILED] == "operation_failed"


# --- why the two decoders are not merged, stated executably ---------------------------------

# A real Stages frame: flags 0x0023 (balance + crank revs), power 113 W.
_REAL_FRAME = bytes.fromhex("23007100526204f2e5")


def test_capture_decoder_is_tolerant_of_truncation(capture_ble):
    """A short frame must still be recorded - never dropped, never raised away.

    The bytes and the flags always survive, so `raw_hex` remains the record and everything else
    is a convenience view over it. Since issue #306 the record is also self-describing: a field
    the flags advertised but the frame could not carry is named explicitly, rather than leaving
    an absent key that looks identical to a meter that never set the flag.
    """
    truncated = _REAL_FRAME[:5]
    out = capture_ble.decode_cp_measurement(truncated)
    assert out["raw_hex"] == truncated.hex(), "the raw bytes survive regardless"
    assert out["flags"] == 0x0023, "the flags survive, so the missing field is detectable"
    assert out["instantaneous_power_w"] == 113, "the fields that did arrive are still decoded"
    assert "cumulative_crank_revs" not in out, "the field that did not arrive is still absent"
    assert out["truncated_at_field"] == "crank_rev", "and now it says WHICH field went missing"
    assert out["truncated_missing_bytes"] == 4, "5 bytes present, crank rev needed 4 more"


def test_a_complete_frame_carries_no_truncation_marker(capture_ble):
    """The marker must only ever appear on malformed frames.

    This is what makes the change additive: every committed capture and every golden vector
    decodes exactly as before.
    """
    out = capture_ble.decode_cp_measurement(_REAL_FRAME)
    assert "truncated_at_field" not in out
    assert "truncated_missing_bytes" not in out
    assert out["cumulative_crank_revs"] is not None


def test_truncation_marker_names_the_first_missing_field_only(capture_ble):
    """With several fields advertised, the first shortfall is the one that matters.

    Everything after it is missing for the same reason, so naming them all would be noise; the
    analyst needs to know where the frame stopped being trustworthy.
    """
    # flags 0x0033 = pedal balance + crank revs + wheel revs; payload stops after the balance byte
    frame = bytes.fromhex("33007100" "52")
    out = capture_ble.decode_cp_measurement(frame)
    assert out["pedal_power_balance_pct"] == 41.0, "the field that fitted is decoded"
    assert out["truncated_at_field"] == "wheel_rev", "wheel revs precede crank revs in flag order"
    assert out["truncated_missing_bytes"] == 6, "none of the 6 wheel-rev bytes arrived"


def test_csc_truncation_is_marked(capture_ble):
    """The CSC decoder gains the same marker as the CPS one."""
    complete = bytes.fromhex("03" "01000000" "0000" "0200" "0000")
    assert "truncated_at_field" not in capture_ble.decode_csc_measurement(complete)

    out = capture_ble.decode_csc_measurement(bytes.fromhex("03" "01000000"))
    assert out["flags"] == 0x03, "wheel AND crank advertised"
    assert out["truncated_at_field"] == "wheel_rev"
    assert out["truncated_missing_bytes"] == 2


def test_control_point_value_reply_truncation_is_marked(capture_ble):
    """A value-returning op whose value did not arrive is truncation, not a declined request."""
    # Real Stages crank-length reply: 20 05 59 01 -> 345 half-mm = 172.5 mm
    ok = capture_ble.decode_cp_response(bytes.fromhex("20055901"))
    assert ok["crank_length_mm"] == 172.5
    assert "truncated_at_field" not in ok

    short = capture_ble.decode_cp_response(bytes.fromhex("200501"))
    assert short["truncated_at_field"] == "crank_length"


def test_not_supported_reply_is_never_marked_as_truncated(capture_ble):
    """`0x02 = not supported` is a legitimate 3-byte reply, not a short frame.

    Marking it would cry wolf on every meter that simply does not implement an op.
    """
    out = capture_ble.decode_cp_response(bytes.fromhex("200502"))
    assert out["result_name"] is not None
    assert "truncated_at_field" not in out
    assert "crank_length_mm" not in out


def test_capture_decoder_records_a_runt_frame_rather_than_raising(capture_ble):
    """Below the fixed header there is nothing to decode, and that is said explicitly."""
    out = capture_ble.decode_cp_measurement(b"\x23\x00")
    assert out["error"] == "short payload"
    assert out["raw_hex"] == "2300", "even an undecodable frame keeps its bytes"


def test_package_decoder_raises_on_the_same_truncation():
    """The contrast that justifies two decoders: the runtime path refuses a short frame."""
    with pytest.raises(ValueError):
        pkg_cps.decode_cps_measurement(_REAL_FRAME[:5])


def test_capture_decoder_covers_flags_the_runtime_decoder_does_not(capture_ble):
    """The capture side reads the full optional set; the runtime side reads what we ship."""
    # flags: crank revs + extreme angles + top/bottom dead spot + accumulated energy
    flags = (pkg_cps.F_CRANK_REV | pkg_cps.F_EXTREME_ANGLES
             | pkg_cps.F_TOP_DEAD_SPOT | pkg_cps.F_BOTTOM_DEAD_SPOT | pkg_cps.F_ACCUM_ENERGY)
    frame = (flags.to_bytes(2, "little") + (200).to_bytes(2, "little", signed=True)
             + (10).to_bytes(2, "little") + (1024).to_bytes(2, "little")   # crank revs + time
             + (0x00A050).to_bytes(3, "little")                            # two 12-bit angles
             + (15).to_bytes(2, "little") + (195).to_bytes(2, "little")    # dead spots
             + (42).to_bytes(2, "little"))                                 # energy kJ
    out = capture_ble.decode_cp_measurement(frame)
    assert "decode_error" not in out, out.get("decode_error")
    assert out["max_angle_deg"] == 0x050
    assert out["min_angle_deg"] == 0x00A
    assert out["top_dead_spot_angle_deg"] == 15
    assert out["bottom_dead_spot_angle_deg"] == 195
    assert out["accumulated_energy_kj"] == 42
    assert out["cumulative_crank_revs"] == 10

    # The same frame through the runtime decoder: the shared fields agree, and it simply has
    # no opinion about the rest. Agreement where they overlap is the invariant that matters.
    runtime = pkg_cps.decode_cps_measurement(frame)
    assert runtime.power_w == out["instantaneous_power_w"] == 200
    assert runtime.cumulative_crank_revs == out["cumulative_crank_revs"]
    assert runtime.last_crank_event_time == out["last_crank_event_time_1024s"]


# --- the truncation marker must never fire on real data -----------------------------------------

def _all_frames_by_char() -> dict[str, list[str]]:
    """Every recorded frame in the committed BLE captures, grouped by characteristic.

    Wider than `_measurement_frames()` above, which covers only cycling_power_measurement -- the
    control-point and CSC decoders had no corpus coverage at all until issue #306.
    """
    out: dict[str, list[str]] = {}
    for path in sorted((_ROOT / "findings" / "captures").glob("*.jsonl")):
        with path.open(encoding="utf-8", errors="ignore") as fh:
            for line in fh:
                try:
                    rec = json.loads(line)
                except ValueError:
                    continue
                data = rec.get("data")
                char = rec.get("char")
                if isinstance(data, dict) and data.get("raw_hex") and char:
                    out.setdefault(char, []).append(data["raw_hex"])
    return out


_BY_CHAR = _all_frames_by_char()


def test_the_wider_corpus_is_present():
    """Guard against this whole section passing vacuously if the captures move."""
    assert _BY_CHAR.get("cycling_power_measurement"), "no CPS measurement frames found"
    assert _BY_CHAR.get("cycling_power_control_point"), "no control-point frames found"


def test_no_real_frame_is_ever_marked_as_truncated(capture_ble):
    """The marker added for issue #306 must only appear on malformed frames.

    That is the whole basis for calling the change additive: every committed capture and every
    golden vector must decode exactly as before. Verified when the marker landed -- 2,437 real
    frames re-decoded, zero marked -- and pinned here so a future decoder change cannot start
    crying wolf on good data without a test failing.
    """
    routes = {
        "cycling_power_measurement": capture_ble.decode_cp_measurement,
        "cycling_power_control_point": capture_ble.decode_cp_response,
        "csc_measurement": capture_ble.decode_csc_measurement,
    }
    marked = []
    checked = 0
    for char, decode in routes.items():
        for raw_hex in _BY_CHAR.get(char, []):
            checked += 1
            got = decode(bytes.fromhex(raw_hex))
            if "truncated_at_field" in got:
                marked.append((char, raw_hex, got["truncated_at_field"]))
    assert checked > 2000, f"expected the full corpus, checked only {checked}"
    assert not marked, (
        f"{len(marked)} of {checked} REAL frames were flagged as truncated; the marker is only "
        f"ever meant to fire on malformed frames. First: {marked[0]}"
    )