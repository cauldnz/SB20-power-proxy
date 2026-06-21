"""Hermetic tests for the pcap->SQLite importer.

No tshark needed: the tshark subprocess is the I/O seam (``tshark_att_rows`` /
``discovery_handle_map``); the parse / decode / ``load_att_rows`` path is pure and is
driven here with synthetic rows and the *real* bytes seen in the session-6 captures.
"""

from __future__ import annotations

from sb20proxy.analysis import jsonl_sqlite as js
from sb20proxy.analysis import pcap_sqlite as ps


def test_decode_stages_control_real_values():
    # real app-1713 proprietary erg writes: 02 00 <u16-LE> 00 00
    assert ps.decode_stages_control("020042010000") == 0x0142  # 322
    assert ps.decode_stages_control("0200c8020000") == 0x02C8  # 712
    assert ps.decode_stages_control("050042010000") is None    # not the 0x0200 opcode
    assert ps.decode_stages_control("0200") is None            # too short
    assert ps.decode_stages_control("") is None


def test_resolve_char_uuid16_proprietary_and_handle_fallback():
    assert ps.resolve_char("2a63", None, None, {}) == "cycling_power_measurement"
    assert ps.resolve_char("2ad2", None, None, {}) == "indoor_bike_data"
    assert ps.resolve_char("2ad9", None, None, {}) == "fitness_machine_control_point"
    # the 0c46be proprietary base, in the wire order tshark prints
    prop = "e5f4a2e1eac60eaeff48229cb1be460c"
    assert ps.resolve_char(None, prop, None, {}).startswith("stages_prop")
    # handle fallback via the discovery map; unknown handle -> labelled, not dropped
    assert ps.resolve_char(None, None, 0x39, {0x39: "stages_prop_x"}) == "stages_prop_x"
    assert ps.resolve_char(None, None, 0x99, {}) == "handle_0x0099"


def test_parse_handle_map_from_real_discovery_line():
    # real read_by_type_rsp: decl 0x0038, value 0x0039, char uuid = 0c46beb1 (wire order)
    line = "0x0038,0x0039\t\te5f4a2e1eac60eaeff48229cb1be460c"
    hmap = ps.parse_handle_map([line])
    assert 0x0039 in hmap and hmap[0x0039].startswith("stages_prop")
    assert 0x0038 not in hmap  # only the *value* handle is keyed (what data ops use)


def test_parse_att_row_tsv():
    row = ps.parse_att_row("123\t1750000000.5\t0x0a1b2c3d\t0x52\t0x0039\t\t\t020042010000\t")
    assert row["frame"] == 123
    assert row["t_epoch"] == 1750000000.5
    assert row["access_addr"] == "0x0a1b2c3d"
    assert row["opcode"] == 0x52
    assert row["handle"] == 0x39
    assert row["value"] == "020042010000"
    assert ps.parse_att_row("") is None
    assert ps.parse_att_row("   ") is None


def test_direction_of():
    assert ps.direction_of(0x52) == "write"
    assert ps.direction_of(0x12) == "write"
    assert ps.direction_of(0x1B) == "notify"
    assert ps.direction_of(0x1D) == "notify"


def test_load_att_rows_decodes_into_tables_and_power_sample():
    conn = js.connect(":memory:")
    t0 = 1_750_000_000.0
    rows = [
        # proprietary erg write — no inline UUID, resolved via the handle map
        {"frame": 1, "t_epoch": t0, "access_addr": "0xaa",
         "opcode": 0x52, "handle": 0x39, "value": "020042010000"},
        # CPS Measurement notification (flags=0x0000, power=200) -> ble_notification -> power_sample
        {"frame": 2, "t_epoch": t0 + 1, "access_addr": "0xbb",
         "opcode": 0x1B, "handle": 0x2B, "uuid16": "2a63", "value": "0000c800"},
    ]
    stats = ps.load_att_rows(conn, "T.pcap", rows, handle_map={0x39: "stages_prop_test"})
    assert stats == {"att": 2, "smp": 0, "cps": 1, "ibd": 0, "ftms_cp": 0,
                     "stages_ctrl": 1, "capture_id": stats["capture_id"]}

    cp = conn.execute("SELECT char, note FROM ble_control_point").fetchone()
    assert cp["char"].startswith("stages_prop") and cp["note"] == "stages_ctrl=322"

    # CPS power surfaces in the shared power_sample view (reconcile sees it)
    psm = conn.execute("SELECT power_w FROM power_sample WHERE protocol='cps'").fetchone()
    assert psm["power_w"] == 200

    # the raw ATT spine keeps every op (the knowledge base)
    assert conn.execute("SELECT COUNT(*) n FROM pcap_att").fetchone()["n"] == 2
    assert conn.execute(
        "SELECT n_records FROM capture WHERE filename='T.pcap'").fetchone()["n_records"] == 2


def test_load_att_rows_value_pattern_fallback_without_handle_map():
    """A 0200 write still decodes as Stages control even if the handle map missed it."""
    conn = js.connect(":memory:")
    rows = [{"frame": 1, "t_epoch": 1.0, "access_addr": "0xaa", "opcode": 0x52, "handle": 0x39,
             "uuid16": None, "uuid128": None, "value": "020092010000", "smp_opcode": None}]
    stats = ps.load_att_rows(conn, "T2.pcap", rows)  # no handle_map
    assert stats["stages_ctrl"] == 1
    row = conn.execute("SELECT char, note FROM ble_control_point").fetchone()
    assert row["char"] == "stages_prop_ctrl" and row["note"] == "stages_ctrl=402"


def test_load_att_rows_reimport_replaces():
    conn = js.connect(":memory:")
    rows = [{"frame": 1, "t_epoch": 1.0, "access_addr": "0xaa", "opcode": 0x52, "handle": 0x39,
             "uuid16": None, "uuid128": None, "value": "020092010000", "smp_opcode": None}]
    ps.load_att_rows(conn, "R.pcap", rows)
    ps.load_att_rows(conn, "R.pcap", rows)  # idempotent: replace, not duplicate
    n_cap = conn.execute("SELECT COUNT(*) n FROM capture WHERE filename='R.pcap'").fetchone()["n"]
    assert n_cap == 1
    assert conn.execute("SELECT COUNT(*) n FROM pcap_att").fetchone()["n"] == 1
