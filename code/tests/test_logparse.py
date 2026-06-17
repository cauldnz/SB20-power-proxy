"""Host tests for the /log parser. The fixture uses the firmware's real log line formats
(src/ble/BleMeterClient.cpp + BleCrankPeripheral.cpp) so the parser tracks what the device
actually emits — real-format-first, not invented."""

from __future__ import annotations

from sb20proxy.ble import cps
from sb20proxy.logparse import cp_op_name, flag_field_names, parse_log

# A realistic /log dump: WiFi boot lines, a meter found + its 0x2F frame (the captured Stages/
# Assioma shape), a consumer connect, a zero-reset control-point write, a proprietary write, a drop.
SAMPLE_LOG = """\
[wifi] joining 'Donnie Boon'
[wifi] connected; status at http://192.168.1.165/
[meter] found 'ASSIOMA17039L' E6:20:90:8C:F3:FE
[meter] cps flags=0x002f cadence=yes 2f00ae00583cf7e600be6c
[srv] connect from 11:22:33:44:55:66
[cp] write 0c
[prop fe02] write a1b2c3
[srv] disconnect reason=19
"""


def test_parse_meter_frame():
    s = parse_log(SAMPLE_LOG)
    assert len(s.meters) == 1
    m = s.meters[0]
    assert m.flags == 0x2F
    assert m.has_cadence is True
    assert "crank_rev(cadence)" in m.fields
    assert "accumulated_torque" in m.fields
    assert m.raw_hex == "2f00ae00583cf7e600be6c"


def test_parse_handshake_and_events():
    s = parse_log(SAMPLE_LOG)
    assert len(s.cp_writes) == 1
    assert s.cp_writes[0].op == cps.CP_START_OFFSET_COMPENSATION
    assert "offset" in s.cp_writes[0].op_name
    assert s.prop_writes == ["a1b2c3"]
    assert s.connects == ["11:22:33:44:55:66"]
    assert s.disconnects == [19]


def test_power_only_meter_has_no_cadence():
    s = parse_log("[meter] cps flags=0x0000 cadence=no 32000000")
    assert s.meters[0].has_cadence is False
    assert s.meters[0].fields == []


def test_flag_field_names_and_op_name():
    assert flag_field_names(cps.F_CRANK_REV) == ["crank_rev(cadence)"]
    assert flag_field_names(0) == []
    assert "offset" in cp_op_name(cps.CP_START_OFFSET_COMPENSATION)
    assert cp_op_name(0x99) == "op_0x99"


def test_render_is_readable():
    out = parse_log(SAMPLE_LOG).render()
    assert "cadence=yes" in out
    assert "zero-reset" in out  # the 0x0c control-point write, decoded
    assert "reason=19" in out
