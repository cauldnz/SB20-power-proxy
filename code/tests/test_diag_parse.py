"""Parse a tester's /diag report (the desk half of the collaboration loop) and decode its frames.

The firmware's GET /diag text (firmware/lib/proxy/DiagReport.h) → structured config/status/frames,
then the raw frames decode with the same CPS codec the firmware mirrors. Pinned on a realistic
sample (spaced values like ``spoof_name=Stages 62144`` and real Assioma frames must survive).
"""

from __future__ import annotations

from sb20proxy.analysis.diag import parse_diag_report
from sb20proxy.ble import cps

SAMPLE = """SB20 Proxy diagnostic
====================
fw=sb20proxy-esp32  version=0.1.0  uptime_ms=12345  heap=120000  rssi=-55

[config]
  source_addr=e6:20:90:8c:f3:fe
  source_name_filter=ASSIOMA
  single_sided_double=no
  spoof_name=Stages 62144
  spoof_serial=11821518

[status]
  source=connected
  source_connected_name=ASSIOMA17039L
  src_power_w=158  src_cadence_rpm=88  src_balance_pct=44
  out_power_w=158  forwarded=42

[meter frames] (CPS 0x2A63 raw hex, oldest first; 2 captured)
  23009e005816134e4d
  23009f005a1a13915a
"""


def test_parse_config_status_frames():
    rep = parse_diag_report(SAMPLE)
    assert rep.fw == "sb20proxy-esp32"          # fw token stays whole despite the trailing version=
    assert rep.status["version"] == "0.1.0"     # build stamp parsed off the header line
    assert rep.config["source_addr"] == "e6:20:90:8c:f3:fe"
    assert rep.config["spoof_name"] == "Stages 62144"   # spaced value kept intact
    assert rep.config["single_sided_double"] == "no"
    assert rep.status["source"] == "connected"
    assert rep.status["source_connected_name"] == "ASSIOMA17039L"
    assert rep.status["src_power_w"] == "158"           # compound kv line split correctly
    assert rep.status["src_balance_pct"] == "44"
    assert rep.frames == ["23009e005816134e4d", "23009f005a1a13915a"]


def test_frames_decode_with_the_codec():
    rep = parse_diag_report(SAMPLE)
    m = cps.decode_cps_measurement(bytes.fromhex(rep.frames[0]))
    assert m.power_w == 158
    assert m.pedal_balance == 88 and m.balance_pct == 44.0   # matches the report's src_balance_pct


def test_empty_and_garbage_report_are_safe():
    assert parse_diag_report("").frames == []
    rep = parse_diag_report("not a diag report at all\nrandom text")
    assert rep.frames == [] and rep.config == {} and rep.fw == ""
