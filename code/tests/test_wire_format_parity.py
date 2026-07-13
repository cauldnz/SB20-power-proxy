"""Cross-language wire-format parity — enforce "one wire format, many deployments".

The project implements several wire formats in more than one language (C++ in
`firmware*/`, Python in `sb20proxy/`, JS in `web/index.html`). Each side has its own
golden vectors; nothing has been *cross-checking* that they agree, so a byte layout can
silently drift on one side. This mirrors the existing parity idiom
(`test_calibration_parity.py`, `test_ota_sign.py`, `test_workout_engine_parity.py`) but
points it at the BLE/ANT wire formats — the audit (decisions.md 2026-07-10) flagged these
as the gaps where the invariant was a claim, not an enforced check.

Add a format here whenever a codec gains a second-language mirror.
"""

from __future__ import annotations

import re
from pathlib import Path

from sb20proxy.ant import pages

REPO = Path(__file__).resolve().parents[2]


# --- ANT+ Bike Power: Python pages.py must produce the SAME bytes as the C++ mirror -------------
# The expected hex is exactly what firmware-nrf/lib/bridge/AntBikePower.h asserts as golden in
# firmware-nrf/test/test_bridge/test_main.cpp (the `nrfant::*` encoders). Same input -> same bytes,
# so an offset slip on either side fails here. Keep these in step with that C++ test.
def test_ant_power_only_matches_cpp_golden():
    # test_ant_power_only_golden_and_roundtrip: encodePowerOnly(10,250,1000,90,RESERVED)
    b = pages.encode_power_only(
        event_count=10, instantaneous_power_w=250, accumulated_power=1000,
        cadence_rpm=90, pedal_power=pages.RESERVED,
    )
    assert b.hex() == "100aff5ae803fa00"


def test_ant_pedal_power_byte_matches_cpp_golden():
    # test_ant_power_only_balance_and_no_cadence: pedalPowerByte(50,true) == 0xB2
    assert pages.pedal_power_byte(50, differentiated=True) == 0xB2
    # ... and the full no-cadence frame encodePowerOnly(5,200,500,-1,0xB2) would produce:
    b = pages.encode_power_only(
        event_count=5, instantaneous_power_w=200, accumulated_power=500,
        cadence_rpm=None, pedal_power=0xB2,
    )
    assert b.hex() == "1005b2fff401c800"


def test_ant_calibration_pages_match_cpp_golden():
    # test_ant_calibration_pages_golden: offset 903, manual-zero SUCCESS + the zero REQUEST.
    resp = pages.encode_page({
        "page": pages.PAGE_CALIBRATION, "calibration_id": pages.CAL_ID_MANUAL_ZERO_SUCCESS,
        "auto_zero_status": pages.RESERVED, "calibration_data": 903,
    })
    assert resp.hex() == "01acffffffff8703"
    req = pages.encode_page({
        "page": pages.PAGE_CALIBRATION, "calibration_id": pages.CAL_ID_MANUAL_ZERO_REQUEST,
        "auto_zero_status": pages.RESERVED, "calibration_data": -1,
    })
    assert req.hex() == "01aaffffffffffff"


def test_ant_manufacturer_page_matches_cpp_golden():
    # test_ant_common_pages: manufacturer info hw 3, mfg 69 (Stages), model 3.
    b = pages.encode_manufacturer_info(hw_revision=3, manufacturer_id=69, model_number=3)
    assert b.hex() == "50ffff0345000300"


# --- OBC action-option ORDER: the SPA dropdown must index-align with the firmware -------------
# The Bridge GATT Buttons char (and the ESP32 /obc/buttons.json) carry a u8 INDEX into the action
# list; the SPA writes that index by dropdown position. If web/index.html's OBC_ACTIONS order
# diverges from firmware Sb20ButtonMap.h sb20ActionOptions, every press binds to the wrong action
# with no other signal. The comment at OBC_ACTIONS claims the mirror; this enforces it.
def _spa_obc_actions() -> list[str]:
    html = (REPO / "web" / "index.html").read_text(encoding="utf-8")
    m = re.search(r"const OBC_ACTIONS = \[(.*?)\];", html, re.DOTALL)
    assert m, "OBC_ACTIONS array not found in web/index.html"
    return re.findall(r'"([^"]*)"', m.group(1))


def _firmware_action_labels() -> list[str]:
    src = (REPO / "firmware" / "lib" / "proxy" / "Sb20ButtonMap.h").read_text(encoding="utf-8")
    block = re.search(r"kOptions\[\] = \{(.*?)\n    \};", src, re.DOTALL)
    assert block, "sb20ActionOptions kOptions block not found"
    # each entry is {"token", "Label", {spec}} — the 2nd quoted string is the label
    return re.findall(r'\{"[^"]*",\s*"([^"]*)"', block.group(1))


def test_obc_action_order_matches_firmware():
    spa = _spa_obc_actions()
    fw = _firmware_action_labels()
    assert spa == fw, (
        "SPA OBC_ACTIONS drifted from firmware sb20ActionOptions order/labels — the Buttons-char "
        f"index would map to the wrong action.\n  SPA: {spa}\n  FW:  {fw}"
    )
