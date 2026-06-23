"""Pure route-check verdicts (the host-tested half of the live route smoke test)."""

from __future__ import annotations

from sb20proxy.qa.route_check import (
    check_form_persisted,
    check_get,
    check_status_json,
    overall_pass,
    render,
)

_GOOD_STATUS = '{"fw":"sb20proxy-esp32","source":"searching","heap":120000,"power_w":0}'
_DIAG = """SB20 Proxy diagnostic
fw=sb20proxy-esp32 uptime_ms=1 heap=120000 rssi=-55
[config]
  source_addr=
  source_name_filter=ASSIOMA
  spoof_name=RouteSmoke9
  spoof_serial=11821518
[status]
  source=searching
"""


def test_get_route_ok_and_missing_marker():
    assert check_get("/calibrate", 200, "...Meter calibration...", ["Meter calibration"]).ok
    bad = check_get("/calibrate", 200, "<html>nope</html>", ["Meter calibration"])
    assert not bad.ok and "missing" in bad.detail
    assert not check_get("/calibrate", 404, "", ["x"]).ok  # non-200 fails


def test_status_json_check():
    assert check_status_json(200, _GOOD_STATUS).ok
    assert not check_status_json(200, "not json").ok
    assert not check_status_json(200, '{"fw":"x"}').ok        # missing required keys
    assert not check_status_json(500, _GOOD_STATUS).ok


def test_form_persisted_is_the_regression_guard():
    # /diag shows spoof_name=RouteSmoke9 -> the POST took
    assert check_form_persisted("spoof_name", "RouteSmoke9", _DIAG).ok
    # if the route had ignored the body, /diag would still show the old value -> FAIL with a hint
    bad = check_form_persisted("spoof_name", "WANTED", _DIAG)
    assert not bad.ok and "ignored" in bad.detail


def test_overall_and_render():
    checks = [check_status_json(200, _GOOD_STATUS),
              check_form_persisted("spoof_name", "RouteSmoke9", _DIAG)]
    assert overall_pass(checks)
    assert "PASS" in render(checks)
    assert not overall_pass([])  # nothing checked is not a pass
