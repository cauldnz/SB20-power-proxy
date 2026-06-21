"""Multi-device BLE capture: the pure helpers + the SQLite ingest of a multi-BLE
capture (per-device streams, BLE CPS power surfaced into power_sample).

The bleak I/O (scripts/capture_ble_multi.py) is the hardware seam; here we test the
spec parser, the notification decoder, and that a multi-device capture reconciles.
"""

from __future__ import annotations

import json

import pytest

from sb20proxy.analysis import jsonl_sqlite as js
from sb20proxy.ble import cps, ftms
from sb20proxy.ble.multi_capture import (
    build_notification_data,
    parse_device_spec,
    subscriptions_for,
)


# --------------------------------------------------------------------------- #
# pure helpers
# --------------------------------------------------------------------------- #
def test_parse_device_spec_keeps_mac_colons():
    d = parse_device_spec("sb20:all:E4:AA:5A:D6:0E:D4")
    assert d.label == "sb20" and d.kind == "all" and d.address == "E4:AA:5A:D6:0E:D4"
    d2 = parse_device_spec("assioma:cps:11:22:33:44:55:66")
    assert d2.kind == "cps" and d2.address == "11:22:33:44:55:66"


@pytest.mark.parametrize("bad", [
    "sb20:E4:AA:5A:D6:0E:D4",       # missing kind
    "sb20:nope:11:22:33:44:55:66",  # bad kind
    "sb20:cps:",                    # no address
    ":cps:11:22",                   # no label
])
def test_parse_device_spec_rejects_bad(bad):
    with pytest.raises(ValueError):
        parse_device_spec(bad)


def test_subscription_map():
    assert any(lbl == "indoor_bike_data" for _u, lbl, _i in subscriptions_for("ftms"))
    assert any(lbl == "cycling_power_measurement" for _u, lbl, _i in subscriptions_for("cps"))
    assert subscriptions_for("all") == []  # handled by subscribe-all


def test_build_notification_data_decodes_power():
    ibd = ftms.encode_indoor_bike_data(power_w=200, cadence_rpm=90)
    d = build_notification_data("indoor_bike_data", ibd)
    assert d["power_w"] == 200 and d["cadence_rpm"] == 90.0 and d["raw_hex"] == ibd.hex()

    cpm = cps.encode_cps_measurement(150)
    d2 = build_notification_data("cycling_power_measurement", cpm)
    assert d2["power_w"] == 150

    # unknown / exploratory char (e.g. the shifter) -> raw + ascii, never raises
    d3 = build_notification_data("shifter", bytes.fromhex("01000100"))
    assert d3["raw_hex"] == "01000100" and "ascii" in d3


def test_build_notification_data_never_raises_on_bad_frame():
    d = build_notification_data("indoor_bike_data", b"")     # too short to decode
    assert d["raw_hex"] == "" and "decode_error" in d


# --------------------------------------------------------------------------- #
# SQLite ingest of a multi-device BLE capture
# --------------------------------------------------------------------------- #
def test_multi_device_ble_capture_reconciles(tmp_path):
    ibd = ftms.encode_indoor_bike_data(power_w=200, cadence_rpm=90).hex()
    cpm = cps.encode_cps_measurement(260).hex()
    records = [{"kind": "session_start", "protocol": "ble+multi"}]
    for t in (1.0, 2.0, 3.0):
        records.append({"kind": "ble_notification", "monotonic_s": t, "device": "sb20",
                        "char": "indoor_bike_data", "data": {"raw_hex": ibd}})
        records.append({"kind": "ble_notification", "monotonic_s": t, "device": "assioma",
                        "char": "cycling_power_measurement", "data": {"raw_hex": cpm}})
    p = tmp_path / "MULTI-ble.jsonl"
    p.write_text("\n".join(json.dumps(r) for r in records), encoding="utf-8")

    conn = js.connect(":memory:")
    js.import_capture(conn, p)

    # both devices surface as distinct power streams (FTMS + CPS, keyed by label)
    streams = {r["stream"] for r in conn.execute("SELECT DISTINCT stream FROM power_sample")}
    assert streams == {"sb20", "assioma"}

    # reconcile the two on the shared clock — the SB20 reads ~0.77x the Assioma
    rows = js.reconcile(conn, "sb20", "assioma", capture="MULTI-ble.jsonl", basis="mono")
    assert len(rows) == 3
    summ = js.reconcile_summary(rows)
    assert summ["median_ratio"] == pytest.approx(200 / 260, abs=0.001)
    conn.close()
