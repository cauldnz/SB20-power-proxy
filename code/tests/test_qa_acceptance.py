"""Board acceptance verdict — hermetic tests for the pre-ship QA gate (no hardware)."""

from __future__ import annotations

from sb20proxy.qa.acceptance import HEAP_FLOOR_BYTES, evaluate

GOOD_STATUS = {
    "fw": "sb20proxy-esp32", "source": "searching", "src_name": "",
    "heap": 120000, "ms": 12345, "rssi": -55, "power_w": 0,
}


def test_healthy_board_passes():
    r = evaluate(
        expected_spoof_name="Stages 62144",
        advert_seen=True, advert_names=["Stages 62144"],
        status=GOOD_STATUS, cps_powers=[0, 152, 154], flash_ok=True,
    )
    assert r.passed
    assert "PASS" in r.render()


def test_no_advert_fails_even_if_status_ok():
    r = evaluate(
        expected_spoof_name="Stages 62144",
        advert_seen=False, advert_names=["SomeOtherThing"],
        status=GOOD_STATUS,
    )
    assert not r.passed
    assert any(c.name == "advertises as spoof crank" and c.ok is False for c in r.checks)


def test_advert_alone_is_enough_to_judge():
    # No /status, no CPS sample — but the (critical) advert check ran and passed.
    r = evaluate(expected_spoof_name="Stages 62144", advert_seen=True,
                 advert_names=["Stages 62144"])
    assert r.passed


def test_nothing_observed_is_not_a_pass():
    # A board that never showed up and was never queried can't be called "good".
    r = evaluate(expected_spoof_name="Stages 62144", advert_seen=None)
    assert not r.passed
    assert "no critical check ran" in r.render()


def test_low_heap_fails():
    r = evaluate(
        expected_spoof_name="Stages 62144", advert_seen=True,
        advert_names=["Stages 62144"],
        status={**GOOD_STATUS, "heap": HEAP_FLOOR_BYTES - 1},
    )
    assert not r.passed
    assert any(c.name == "heap healthy" and c.ok is False for c in r.checks)


def test_bad_source_state_fails():
    r = evaluate(
        expected_spoof_name="Stages 62144", advert_seen=True,
        advert_names=["Stages 62144"], status={**GOOD_STATUS, "source": "wedged"},
    )
    assert not r.passed


def test_empty_fw_string_fails():
    r = evaluate(
        expected_spoof_name="Stages 62144", advert_seen=True,
        advert_names=["Stages 62144"], status={**GOOD_STATUS, "fw": ""},
    )
    assert not r.passed


def test_connected_but_no_frames_does_not_block():
    # A -live board with no meter near is correctly silent on CPS — that must not fail acceptance.
    r = evaluate(
        expected_spoof_name="Stages 62144", advert_seen=True,
        advert_names=["Stages 62144"], status=GOOD_STATUS, cps_powers=[],
    )
    assert r.passed
    assert any(c.name == "CPS frames decode" and c.ok is None and not c.critical
               for c in r.checks)


def test_out_of_range_power_fails():
    # A frame that decodes to impossible power IS a real framing bug — block it.
    r = evaluate(
        expected_spoof_name="Stages 62144", advert_seen=True,
        advert_names=["Stages 62144"], cps_powers=[150, 9000],
    )
    assert not r.passed
    assert any(c.name == "CPS frames decode" and c.ok is False for c in r.checks)


def test_good_frames_are_a_positive_non_critical_signal():
    r = evaluate(expected_spoof_name="Stages 62144", advert_seen=True,
                 advert_names=["Stages 62144"], cps_powers=[0, 152, 300])
    assert r.passed
    assert any(c.name == "CPS frames decode" and c.ok is True for c in r.checks)


def test_flash_failure_fails():
    r = evaluate(
        expected_spoof_name="Stages 62144", advert_seen=True,
        advert_names=["Stages 62144"], status=GOOD_STATUS, flash_ok=False,
    )
    assert not r.passed


def test_missing_heap_field_is_non_critical_skip():
    status = {k: v for k, v in GOOD_STATUS.items() if k != "heap"}
    r = evaluate(expected_spoof_name="Stages 62144", advert_seen=True,
                 advert_names=["Stages 62144"], status=status)
    assert r.passed  # heap check skips (non-critical); the rest pass
    assert any(c.name == "heap healthy" and c.ok is None for c in r.checks)
