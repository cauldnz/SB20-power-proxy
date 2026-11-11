"""Board acceptance verdict — hermetic tests for the pre-ship QA gate (no hardware).

The boards in these tests advertise ``Stages 42729`` (a MAC-derived default, #330) rather than the
old ``Stages 62144`` default: that name is bike 1's REAL crank and is now exactly what the card
must veto — see the fleet-identity tests at the bottom.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from sb20proxy.qa.acceptance import (
    DERIVED_IDENTITY_PREFIX,
    HEAP_FLOOR_BYTES,
    MAX_ANT_DEVICE_NUMBER,
    REAL_CRANK_NAMES,
    default_spoof_name,
    evaluate,
    is_ant_range_identity,
    is_real_crank_identity,
)
from sb20proxy.release import DEV_VERSION

ROOT = Path(__file__).resolve().parents[2]
FLEET_H = ROOT / "firmware" / "lib" / "proxy" / "FleetIdentity.h"

NAME = "Stages 42729"
GOOD_STATUS = {
    "fw": "sb20proxy-esp32", "source": "searching", "src_name": "",
    "heap": 120000, "ms": 12345, "rssi": -55, "power_w": 0,
}


def test_healthy_board_passes():
    r = evaluate(
        expected_spoof_name=NAME,
        advert_seen=True, advert_names=[NAME],
        status=GOOD_STATUS, cps_powers=[0, 152, 154], flash_ok=True,
    )
    assert r.passed
    assert "PASS" in r.render()


def test_no_advert_fails_even_if_status_ok():
    r = evaluate(
        expected_spoof_name=NAME,
        advert_seen=False, advert_names=["SomeOtherThing"],
        status=GOOD_STATUS,
    )
    assert not r.passed
    assert any(c.name == "advertises as spoof crank" and c.ok is False for c in r.checks)


def test_advert_alone_is_enough_to_judge():
    # No /status, no CPS sample — but the (critical) advert check ran and passed.
    r = evaluate(expected_spoof_name=NAME, advert_seen=True, advert_names=[NAME])
    assert r.passed


def test_nothing_observed_is_not_a_pass():
    # A board that never showed up and was never queried can't be called "good".
    r = evaluate(expected_spoof_name=NAME, advert_seen=None)
    assert not r.passed
    assert "no critical check ran" in r.render()


def test_low_heap_fails():
    r = evaluate(
        expected_spoof_name=NAME, advert_seen=True,
        advert_names=[NAME],
        status={**GOOD_STATUS, "heap": HEAP_FLOOR_BYTES - 1},
    )
    assert not r.passed
    assert any(c.name == "heap healthy" and c.ok is False for c in r.checks)


def test_bad_source_state_fails():
    r = evaluate(
        expected_spoof_name=NAME, advert_seen=True,
        advert_names=[NAME], status={**GOOD_STATUS, "source": "wedged"},
    )
    assert not r.passed


def test_empty_fw_string_fails():
    r = evaluate(
        expected_spoof_name=NAME, advert_seen=True,
        advert_names=[NAME], status={**GOOD_STATUS, "fw": ""},
    )
    assert not r.passed


def test_connected_but_no_frames_does_not_block():
    # A -live board with no meter near is correctly silent on CPS — that must not fail acceptance.
    r = evaluate(
        expected_spoof_name=NAME, advert_seen=True,
        advert_names=[NAME], status=GOOD_STATUS, cps_powers=[],
    )
    assert r.passed
    assert any(c.name == "CPS frames decode" and c.ok is None and not c.critical
               for c in r.checks)


def test_out_of_range_power_fails():
    # A frame that decodes to impossible power IS a real framing bug — block it.
    r = evaluate(
        expected_spoof_name=NAME, advert_seen=True,
        advert_names=[NAME], cps_powers=[150, 9000],
    )
    assert not r.passed
    assert any(c.name == "CPS frames decode" and c.ok is False for c in r.checks)


def test_good_frames_are_a_positive_non_critical_signal():
    r = evaluate(expected_spoof_name=NAME, advert_seen=True,
                 advert_names=[NAME], cps_powers=[0, 152, 300])
    assert r.passed
    assert any(c.name == "CPS frames decode" and c.ok is True for c in r.checks)


def test_flash_failure_fails():
    r = evaluate(
        expected_spoof_name=NAME, advert_seen=True,
        advert_names=[NAME], status=GOOD_STATUS, flash_ok=False,
    )
    assert not r.passed


def test_missing_heap_field_is_non_critical_skip():
    status = {k: v for k, v in GOOD_STATUS.items() if k != "heap"}
    r = evaluate(expected_spoof_name=NAME, advert_seen=True,
                 advert_names=[NAME], status=status)
    assert r.passed  # heap check skips (non-critical); the rest pass
    assert any(c.name == "heap healthy" and c.ok is None for c in r.checks)


# ---- fleet identity (#330): never bike 1's real crank, never two on one name -----------------


def test_real_crank_identity_fails_the_card():
    # The old default: a healthy board advertising bike 1's real left crank is NOT shippable.
    r = evaluate(expected_spoof_name="Stages 62144", advert_seen=True,
                 advert_names=["Stages 62144"], status=GOOD_STATUS,
                 cps_powers=[0, 150])
    assert not r.passed
    assert any(c.name == "identity is not a real crank" and c.ok is False for c in r.checks)
    assert "identity is not a real crank" in r.render()


def test_real_crank_identity_is_accepted_when_the_rescue_is_intended():
    # The single-right-crank rescue: the board is MEANT to be that crank.
    r = evaluate(expected_spoof_name="Stages 4963", advert_seen=True,
                 advert_names=["Stages 4963"], status=GOOD_STATUS, crank_rescue=True)
    assert r.passed
    assert any(c.name == "identity is not a real crank" and c.ok is True
               and "--crank-rescue" in c.detail for c in r.checks)


def test_a_real_crank_identity_reported_by_status_fails_even_when_the_expectation_differs():
    # qa_board expected the MAC-derived name, but /status says the board is on 62144: still a veto.
    r = evaluate(expected_spoof_name="Stages 42729", advert_seen=True,
                 advert_names=["Stages 42729"], status={**GOOD_STATUS, "identity": "Stages 62144"})
    assert not r.passed
    assert any(c.name == "identity is not a real crank" and c.ok is False for c in r.checks)


def test_two_advertisers_on_one_identity_fail():
    r = evaluate(expected_spoof_name=NAME, advert_seen=True, advert_names=[NAME, NAME],
                 advert_matches=["aa:aa:aa:aa:aa:aa", "bb:bb:bb:bb:bb:bb"], status=GOOD_STATUS)
    assert not r.passed
    bad = [c for c in r.checks if c.name == "identity unique on air"]
    assert bad and bad[0].ok is False and "2 advertisers" in bad[0].detail


def test_one_advertiser_is_unique():
    r = evaluate(expected_spoof_name=NAME, advert_seen=True,
                 advert_names=[NAME], advert_matches=["aa:aa:aa:aa:aa:aa"], status=GOOD_STATUS)
    assert r.passed
    assert any(c.name == "identity unique on air" and c.ok is True for c in r.checks)


def test_identity_rules_alone_are_not_evidence_of_a_working_board():
    # Both identity checks pass for a board that never came on the air — that must stay a FAIL.
    r = evaluate(expected_spoof_name=NAME, advert_seen=None, advert_matches=None)
    assert not r.passed
    assert not r.failures  # nothing failed, but nothing observed either


def test_identity_provenance_is_reported_non_critically():
    r = evaluate(expected_spoof_name=NAME, advert_seen=True, advert_names=[NAME],
                 status={**GOOD_STATUS, "identity": NAME, "identity_default": True})
    assert r.passed
    prov = [c for c in r.checks if c.name == "identity provenance"]
    assert prov and prov[0].ok is True and not prov[0].critical
    assert "MAC-derived default" in prov[0].detail
    r2 = evaluate(expected_spoof_name=NAME, advert_seen=True, advert_names=[NAME],
                  status={**GOOD_STATUS, "identity": NAME, "identity_default": False})
    assert "configured" in [c for c in r2.checks if c.name == "identity provenance"][0].detail
    # firmware that predates the field: a skip, never a failure
    r3 = evaluate(expected_spoof_name=NAME, advert_seen=True, advert_names=[NAME],
                  status=GOOD_STATUS)
    assert r3.passed
    assert [c for c in r3.checks if c.name == "identity provenance"][0].ok is None


# ---- the derivation twin: parity-locked with firmware/test/test_fleetidentity/test_main.cpp ----


def test_default_spoof_name_golden_vectors_match_the_firmware_suite():
    # Same vectors as test_fleetidentity: the S3-Touch and 0.96" C3 base MACs from BOARDS.md, and
    # the SetupPin.h example SSID "Setup-A6E9" (the identity is cut from the same two MAC bytes).
    assert default_spoof_name("A4:CB:8F:DA:E9:CC") == "Stages 59852"   # 0xE9CC = 59852
    assert default_spoof_name("10:b4:1d:ba:c9:0c") == "Stages 51468"   # lowercase is fine
    assert default_spoof_name("00-00-00-00-A6-E9") == "Stages 42729"   # dash-separated is fine
    assert default_spoof_name("00:00:00:00:00:07") == "Stages 7"       # unpadded, like a real crank
    assert default_spoof_name("00:00:00:00:00:00") == "Stages 1"       # 0 is the ANT wildcard
    assert default_spoof_name("00:00:00:00:FF:FF") == "Stages 65535"   # the app's maximum
    assert default_spoof_name("00:00:00:00:F2:C0") == "Stages 62145"   # 62144: low bit flipped
    assert default_spoof_name("00:00:00:00:13:63") == "Stages 4962"    # 4963: low bit flipped


@pytest.mark.parametrize(
    "bad", ["A6:E9", "", "not a mac", "A4:CB:8F:DA:E9:CC:00", "A4:CB:8F:DA:E9:C"])
def test_default_spoof_name_rejects_a_non_mac(bad):
    with pytest.raises(ValueError):
        default_spoof_name(bad)


def test_derived_names_fit_the_stages_app_and_are_never_a_real_crank_for_any_mac_tail():
    # The Stages app's Left/Right ANT ID fields take 0..65535 only (it rejected 99744, 2026-11-11).
    for tail in range(65536):
        name = default_spoof_name(f"00:00:00:00:{tail >> 8:02X}:{tail & 0xFF:02X}")
        assert is_ant_range_identity(name), name
        assert 1 <= int(name[len(DERIVED_IDENTITY_PREFIX):]) <= MAX_ANT_DEVICE_NUMBER
        assert not is_real_crank_identity(name)


@pytest.mark.parametrize("name,ok", [
    ("Stages 1", True), ("Stages 62145", True), ("Stages 65535", True),
    ("Stages 0", False), ("Stages 65536", False), ("Stages 99744", False), ("Stages 0123", False),
    ("Stages ", False), ("62145", False), ("SB20 Corrector", False)])
def test_is_ant_range_identity(name, ok):
    assert is_ant_range_identity(name) is ok


def test_real_crank_names_are_exactly_bike_1s_cranks():
    assert is_real_crank_identity("Stages 62144")
    assert is_real_crank_identity("Stages 4963")
    assert not is_real_crank_identity("Stages 62145")   # our own id (session 8)
    assert not is_real_crank_identity("Stages 42729")
    assert not is_real_crank_identity("")


def test_python_constants_mirror_the_firmware_header():
    """The C++ header is the source of truth; the Python twin must carry the same names, prefix and
    range, so the QA card and the firmware can never disagree about what a real crank or a default
    looks like."""
    text = FLEET_H.read_text(encoding="utf-8")
    for n in REAL_CRANK_NAMES:
        assert f'"{n}"' in text, f"{n} missing from FleetIdentity.h"
    assert f'kDerivedIdentityPrefix = "{DERIVED_IDENTITY_PREFIX}"' in text
    assert f"kMaxAntDeviceNumber = {MAX_ANT_DEVICE_NUMBER}" in text


# --- the release gate: is this board running the version we are about to publish? --------------
#
# qa_board.py and flash_c3.py FLASH but never BUILD: they push whatever is already in
# .pio/build/<env>. Without an asserted version a stale or desk image is accepted silently and the
# acceptance ride counts as evidence for a version the board is not running. See
# beta/RELEASE-AND-OTA.md and sb20proxy.release.


def _release_board(version: str | None):
    """A board that is healthy in every other respect, reporting `version` (None = no field)."""
    status = dict(GOOD_STATUS)
    if version is not None:
        status["version"] = version
    return dict(
        expected_spoof_name=NAME, advert_seen=True, advert_names=[NAME],
        status=status, cps_powers=[0, 152], flash_ok=True,
    )


def test_matching_version_passes_the_release_gate():
    r = evaluate(**_release_board("0.1.0"), expect_version="0.1.0")
    assert r.passed
    assert "0.1.0" in r.render()


def test_a_desk_build_fails_the_release_gate():
    """0.0.0-dev is Config.h's default: the board is not running a release at all."""
    r = evaluate(**_release_board(DEV_VERSION), expect_version="0.1.0")
    assert not r.passed
    assert "DESK build" in r.render()


def test_a_stale_release_image_fails_the_release_gate():
    r = evaluate(**_release_board("0.0.9"), expect_version="0.1.0")
    assert not r.passed
    assert "stale image" in r.render()


def test_a_firmware_with_no_version_field_fails_the_release_gate():
    r = evaluate(**_release_board(None), expect_version="0.1.0")
    assert not r.passed
    assert "no version field" in r.render()


def test_gating_a_release_without_status_is_a_failure_not_a_skip():
    """Asked to gate a version but given no way to read one: the card must not quietly pass."""
    r = evaluate(
        expected_spoof_name=NAME, advert_seen=True, advert_names=[NAME],
        status=None, expect_version="0.1.0",
    )
    assert not r.passed
    assert "/status was not fetched" in r.render()


def test_version_is_reported_but_not_judged_when_no_release_is_being_gated():
    """The ordinary pre-ship card still shows the version; it just does not fail on it."""
    r = evaluate(**_release_board(DEV_VERSION))
    assert r.passed
    assert DEV_VERSION in r.render()


def test_the_release_version_check_is_critical():
    """A non-critical check could not block shipping, which would defeat the point."""
    r = evaluate(**_release_board("0.0.9"), expect_version="0.1.0")
    failed = [c for c in r.checks if c.ok is False]
    assert [c.name for c in failed] == ["firmware version"]
    assert all(c.critical for c in failed)
