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


def test_resolve_char_strips_0x_prefix():
    """tshark prints btatt.uuid16 as ``0x2ad2``; UUID16_CHARS is keyed ``2ad2``.

    Without stripping the prefix the 16-bit FTMS/CPS chars never resolve — this was
    the session-7 ride bug (every char fell through to ``uuid16_0x….``, so ibd=0).
    """
    assert ps.resolve_char("0x2ad2", None, None, {}) == "indoor_bike_data"
    assert ps.resolve_char("0x2ad9", None, None, {}) == "fitness_machine_control_point"
    assert ps.resolve_char("0x2a63", None, None, {}) == "cycling_power_measurement"
    assert ps.resolve_char("2ad2", None, None, {}) == "indoor_bike_data"  # bare still works
    assert ps.resolve_char("0xBEEF", None, None, {}) == "uuid16_beef"     # unknown -> bare label


# Real GATT-discovery TSV lines from the session-7 ride pcap
# (RIDE-ble-sb20-ride-20260622.pcap), as `discovery_handle_map`'s second tshark pass
# emits them: `btatt.opcode \t btatt.handle \t btatt.uuid16 \t btatt.uuid128`, each
# field comma-joined over every occurrence in the packet (-E occurrence=a).
# frame 2086: one read_by_type_rsp carrying the SB20's whole FTMS service — three
# 16-bit chars (Indoor Bike Data 0x2ad2, FM Status 0x2ada, FM Control Point 0x2ad9),
# handles as decl,value,decl,value,…, the 0x2803 declaration type interleaved.
_DISC_FTMS = ("0x09\t0x001c,0x001d,0x001f,0x0020,0x0022,0x0023"
              "\t0x2803,0x2ad2,0x2803,0x2ada,0x2803,0x2ad9,0x2803\t")
# frame 2110: a 128-bit proprietary char (shifter 0c46be60); note uuid16 carries the
# 0x2803 declaration type that previously shadowed the real (uuid128) char.
_DISC_PROP = "0x09\t0x002e,0x002f\t0x2803,0x2803\te5f4a2e1eac60eaeff48229c60be460c"
# frame 2034: a find_information_rsp (CCCD descriptor 0x2902), handle/uuid 1:1.
_DISC_CCCD = "0x05\t0x000d\t0x2902\t"


def test_parse_handle_map_maps_every_char_in_multichar_ftms_packet():
    """The bug: occurrence=a flattened the 3-char FTMS packet into parallel comma
    lists and only the first (the bogus 0x2803 decl) was mapped. All three value
    handles must now resolve to their real chars."""
    hmap = ps.parse_handle_map([_DISC_FTMS])
    assert hmap[0x001d] == "indoor_bike_data"
    assert hmap[0x0020] == "fitness_machine_status"
    assert hmap[0x0023] == "fitness_machine_control_point"
    # declaration handles (the even positions) are never keyed — only value handles
    assert 0x001c not in hmap and 0x001f not in hmap and 0x0022 not in hmap


def test_parse_handle_map_proprietary_128bit_not_shadowed_by_decl_uuid16():
    # real read_by_type_rsp: decl 0x002e, value 0x002f, 128-bit char 0c46be60 (wire
    # order). The interleaved 0x2803 declaration type must NOT win over the uuid128.
    hmap = ps.parse_handle_map([_DISC_PROP])
    assert 0x002f in hmap and hmap[0x002f].startswith("stages_prop")
    assert 0x002e not in hmap  # only the value handle is keyed (what data ops use)


def test_parse_handle_map_find_information_rsp_pairs_one_to_one():
    hmap = ps.parse_handle_map([_DISC_CCCD])
    assert hmap == {0x000d: "cccd"}  # find_info handle/uuid are 1:1, no decl/value split


def test_parse_handle_map_full_discovery_resolves_ftms_value_handles():
    """All three lines together — the value handles the importer needs for decode."""
    hmap = ps.parse_handle_map([_DISC_FTMS, _DISC_PROP, _DISC_CCCD])
    assert hmap[0x001d] == "indoor_bike_data"
    assert hmap[0x0023] == "fitness_machine_control_point"
    assert hmap[0x002f].startswith("stages_prop")


def test_load_att_rows_decodes_real_ftms_ibd_via_inline_uuid_and_handle_map():
    """A real Indoor Bike Data notification (handle 0x1d, value c500a000f3000000 ->
    243 W, 80 rpm) decodes both via its inline uuid16 (0x2ad2, the prefix fix) and,
    when the op carries no inline UUID, via the discovery handle map (the map fix)."""
    hmap = ps.parse_handle_map([_DISC_FTMS])
    conn = js.connect(":memory:")
    t0 = 1_750_000_000.0
    rows = [
        # as tshark really emits it: the notification carries the resolved uuid16 0x2ad2
        {"frame": 1, "t_epoch": t0, "access_addr": "0xaa", "opcode": 0x1B,
         "handle": 0x001D, "uuid16": "0x2ad2", "value": "c500a000f3000000"},
        # same byte payload but no inline UUID -> must resolve via the handle map alone
        {"frame": 2, "t_epoch": t0 + 1, "access_addr": "0xaa", "opcode": 0x1B,
         "handle": 0x001D, "uuid16": None, "value": "c500a000f3000000"},
    ]
    stats = ps.load_att_rows(conn, "FTMS.pcap", rows, handle_map=hmap)
    assert stats["ibd"] == 2

    decoded = conn.execute(
        "SELECT char, power_w, cadence_rpm FROM ble_notification ORDER BY rec_index").fetchall()
    assert [d["char"] for d in decoded] == ["indoor_bike_data", "indoor_bike_data"]
    assert all(d["power_w"] == 243 for d in decoded)
    # power surfaces in the shared power_sample view (reconcile picks the FTMS stream up)
    psm = conn.execute("SELECT power_w FROM power_sample WHERE protocol='ftms'").fetchall()
    assert [p["power_w"] for p in psm] == [243, 243]


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
