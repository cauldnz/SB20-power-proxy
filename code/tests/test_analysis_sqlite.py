"""Tests for the JSONL→SQLite analysis index.

Hermetic: imports the *real committed captures* (via the `captures_dir` fixture
in conftest) into an in-memory database and asserts on the decoded result. No
network, no hardware. Fixtures are the bytes the actual SB20 / cranks produced —
never invented (real-data-first).
"""

from __future__ import annotations

import json

import pytest

from sb20proxy.analysis import jsonl_sqlite as js

# Small, representative real captures (see findings/captures/README.md):
FTMS_FLAT = "F-ftms-hwloop-server-20260621-0116.jsonl"   # flat ble_notification, decoded inline
FTMS_ERG = "G-sb20-ftms-erg-20260621-0949.jsonl"         # nested `data`, IBD NOT pre-decoded
ADV_SURVEY = "ble-adv-survey-20260614-1607.jsonl"        # ble_advertisement only
MULTI = "QUICK-multi-20260615-064037.jsonl"              # ANT+ stages/assioma/bike_fec, one clock
CPS_RECON = "G-crank62144-ble-20260615-065556.jsonl"     # CPS cp_write/indication (op/opcode shape)


@pytest.fixture
def conn():
    c = js.connect(":memory:")
    yield c
    c.close()


def _import(conn, captures_dir, name) -> js.ImportStats:
    return js.import_capture(conn, captures_dir / name)


# --------------------------------------------------------------------------- #
# import / idempotency / losslessness
# --------------------------------------------------------------------------- #
def test_import_is_idempotent(conn, captures_dir):
    first = _import(conn, captures_dir, FTMS_FLAT)
    assert not first.skipped and first.n_records > 0
    before = conn.execute("SELECT COUNT(*) FROM record").fetchone()[0]

    second = _import(conn, captures_dir, FTMS_FLAT)
    assert second.skipped is True
    after = conn.execute("SELECT COUNT(*) FROM record").fetchone()[0]
    assert before == after
    assert conn.execute("SELECT COUNT(*) FROM capture").fetchone()[0] == 1


def test_replace_rebuilds_without_duplicating(conn, captures_dir):
    _import(conn, captures_dir, FTMS_FLAT)
    n1 = conn.execute("SELECT COUNT(*) FROM record").fetchone()[0]
    js.import_capture(conn, captures_dir / FTMS_FLAT, replace=True)
    n2 = conn.execute("SELECT COUNT(*) FROM record").fetchone()[0]
    assert n1 == n2
    assert conn.execute("SELECT COUNT(*) FROM capture").fetchone()[0] == 1


def test_raw_json_is_preserved_verbatim(conn, captures_dir):
    """Every line is stored losslessly and round-trips to the original object."""
    _import(conn, captures_dir, FTMS_FLAT)
    path = captures_dir / FTMS_FLAT
    original = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    stored = conn.execute(
        "SELECT raw_json FROM record ORDER BY rec_index"
    ).fetchall()
    assert len(stored) == len(original)
    for row, orig in zip(stored, original, strict=True):
        assert json.loads(row["raw_json"]) == orig


def test_unknown_kinds_still_land_in_record(conn, captures_dir):
    """A kind we don't flatten (ble_services) is not lost — it's in `record`."""
    _import(conn, captures_dir, FTMS_ERG)
    kinds = {r["kind"] for r in conn.execute("SELECT DISTINCT kind FROM record")}
    assert "ble_services" in kinds            # not flattened
    assert "ble_notification" in kinds         # flattened
    # record count equals the non-blank line count of the file
    path = captures_dir / FTMS_ERG
    n_lines = sum(1 for line in path.read_text().splitlines() if line.strip())
    assert conn.execute("SELECT COUNT(*) FROM record").fetchone()[0] == n_lines


def test_capture_protocol_inferred(conn, captures_dir):
    _import(conn, captures_dir, ADV_SURVEY)
    _import(conn, captures_dir, MULTI)
    rows = dict(conn.execute("SELECT filename, protocol FROM capture").fetchall())
    assert rows[ADV_SURVEY] == "ble"
    assert rows[MULTI] == "ant+multi"


# --------------------------------------------------------------------------- #
# Indoor Bike Data decode — both capture shapes
# --------------------------------------------------------------------------- #
def test_ibd_decode_flat_shape_matches_capture(conn, captures_dir):
    """Flat ble_notification (raw_hex + inline power): our decode == capture's."""
    _import(conn, captures_dir, FTMS_FLAT)
    # `4500b4000901` -> flags 0x45 (cadence+power), 90 rpm, 265 W (per the capture).
    row = conn.execute(
        "SELECT power_w, cadence_rpm, decoded FROM ble_notification "
        "WHERE raw_hex = '4500b4000901'"
    ).fetchone()
    assert row is not None
    assert row["power_w"] == 265
    assert row["cadence_rpm"] == 90.0
    assert row["decoded"] == 1


def test_ibd_decode_nested_shape_from_raw_hex(conn, captures_dir):
    """The erg capture logs IBD as raw bytes only — we must decode it ourselves."""
    _import(conn, captures_dir, FTMS_ERG)
    # `c500b40064000000` -> flags 0x00c5 (cadence+power+avg-power): 90 rpm, 100 W.
    row = conn.execute(
        "SELECT power_w, cadence_rpm FROM ble_notification WHERE raw_hex = 'c500b40064000000'"
    ).fetchone()
    assert row is not None
    assert row["power_w"] == 100
    assert row["cadence_rpm"] == 90.0


def test_ibd_distance_and_speed_frames_have_no_power(conn, captures_dir):
    """The SB20 round-robins frame layouts; non-power frames must be NULL power."""
    _import(conn, captures_dir, FTMS_ERG)
    # `00009d11` -> flags 0x0000: instantaneous speed only (45.09 km/h), no power.
    row = conn.execute(
        "SELECT power_w, speed_kmh, decoded FROM ble_notification WHERE raw_hex = '00009d11'"
    ).fetchone()
    assert row is not None
    assert row["power_w"] is None
    assert row["decoded"] == 1
    assert abs(row["speed_kmh"] - 45.09) < 1e-6
    # and at least one decoded power frame exists
    powered = conn.execute(
        "SELECT COUNT(*) FROM ble_notification "
        "WHERE char='indoor_bike_data' AND power_w IS NOT NULL"
    ).fetchone()[0]
    assert powered > 10


# --------------------------------------------------------------------------- #
# control point — writes + responses, both decode shapes
# --------------------------------------------------------------------------- #
def test_cp_write_set_target_power_decoded(conn, captures_dir):
    """ble_cp_write 'set_target_power=150W' (raw 059600) -> target_power_w 150."""
    _import(conn, captures_dir, FTMS_ERG)
    row = conn.execute(
        "SELECT note, opcode, target_power_w, is_response FROM ble_control_point "
        "WHERE direction='write' AND raw_hex='059600'"
    ).fetchone()
    assert row is not None
    assert row["opcode"] == 0x05
    assert row["target_power_w"] == 150
    assert row["is_response"] == 0


def test_cp_response_decoded_from_nested_data(conn, captures_dir):
    """CP responses on the FTMS CP char are routed to ble_control_point as responses."""
    _import(conn, captures_dir, FTMS_ERG)
    row = conn.execute(
        "SELECT result, result_name, is_response FROM ble_control_point "
        "WHERE is_response=1 AND opcode=0x05 ORDER BY rec_index LIMIT 1"
    ).fetchone()
    assert row is not None
    assert row["result"] == 1
    assert row["result_name"] == "success"
    # no CP-char rows leaked into the plain-notification table
    leaked = conn.execute(
        "SELECT COUNT(*) FROM ble_notification WHERE char='control_point'"
    ).fetchone()[0]
    assert leaked == 0


def test_cp_response_flat_shape_decoded_via_codec(conn, captures_dir):
    """The hw-loop CP responses are flat (raw_hex only) — decoded with the FTMS codec."""
    _import(conn, captures_dir, FTMS_FLAT)
    row = conn.execute(
        "SELECT opcode, result, result_name FROM ble_control_point WHERE raw_hex='800001'"
    ).fetchone()
    assert row is not None
    assert row["opcode"] == 0x00           # echoed request opcode (Request Control)
    assert row["result"] == 1
    assert row["result_name"] == "success"


def test_cps_control_point_op_opcode_shape(conn, captures_dir):
    """CPS ble_cp_write uses {op, opcode} (no raw_hex); we still capture it losslessly."""
    _import(conn, captures_dir, CPS_RECON)
    row = conn.execute(
        "SELECT note, opcode FROM ble_control_point "
        "WHERE direction='write' AND note='request-crank-length'"
    ).fetchone()
    assert row is not None
    assert row["opcode"] == 5
    # the matching CPS indication carries the capture's own result_name
    ind = conn.execute(
        "SELECT result_name FROM ble_control_point WHERE direction='indication' LIMIT 1"
    ).fetchone()
    assert ind is not None


# --------------------------------------------------------------------------- #
# advertisements
# --------------------------------------------------------------------------- #
def test_advertisements_flattened(conn, captures_dir):
    _import(conn, captures_dir, ADV_SURVEY)
    row = conn.execute(
        "SELECT service_uuids, manufacturer_data, rssi FROM ble_advertisement "
        "WHERE name = 'ASSIOMA17039L' ORDER BY rec_index LIMIT 1"
    ).fetchone()
    assert row is not None
    uuids = json.loads(row["service_uuids"])
    assert "00001818-0000-1000-8000-00805f9b34fb" in uuids   # Cycling Power Service
    mfg = json.loads(row["manufacturer_data"])
    assert "868" in mfg                                       # Favero company id
    assert isinstance(row["rssi"], int)


# --------------------------------------------------------------------------- #
# ANT broadcasts + reconciliation (the headline use case)
# --------------------------------------------------------------------------- #
def test_ant_broadcast_sources_and_power(conn, captures_dir):
    _import(conn, captures_dir, MULTI)
    sources = {r["source"] for r in conn.execute("SELECT DISTINCT source FROM ant_broadcast")}
    assert {"stages", "assioma", "bike_fec"} <= sources
    # power-bearing rows feed the power_sample view
    n_ps = conn.execute("SELECT COUNT(*) FROM power_sample WHERE stream='stages'").fetchone()[0]
    assert n_ps > 50


def test_reconcile_stages_vs_bike_fec_near_unity(conn, captures_dir):
    """Crank vs bike FE-C ~= pass-through (documented #7 result ~0.997).

    Validates the SQL pairing against a committed finding: same capture, per-second
    monotonic buckets, active-sample gate — exactly what 08_analyze_grid.py computes.
    """
    _import(conn, captures_dir, MULTI)
    rows = js.reconcile(
        conn, "bike_fec", "stages", capture=MULTI,
        basis="mono", bucket_s=1.0, min_power=40, min_cadence=30,
    )
    summ = js.reconcile_summary(rows)
    assert summ["n_buckets"] >= 20
    assert summ["median_ratio"] is not None
    assert 0.85 <= summ["median_ratio"] <= 1.15      # near pass-through (#7)
    # every paired row carries both meters' cadence for cadence-binned fits
    assert all(r.cadence_a is not None and r.cadence_b is not None for r in rows)


def test_reconcile_iso_basis_buckets_on_wallclock(conn, captures_dir):
    """The cross-capture path uses iso_time epoch buckets (SQLite strftime).

    No simultaneous SB20-FTMS + ANT+ pair exists yet, so exercise the iso SQL on
    two streams *within* the multi-capture — it validates the strftime('%s', …)
    bucketing the cross-file SB20-vs-Assioma reconciliation will use.
    """
    _import(conn, captures_dir, MULTI)
    rows = js.reconcile(
        conn, "stages", "assioma", capture=MULTI,
        basis="iso", bucket_s=1.0, min_power=40, min_cadence=30,
    )
    assert len(rows) > 0
    assert all(r.power_a > 0 and r.power_b > 0 for r in rows)


def test_reconcile_delta_and_ratio_helpers():
    r = js.ReconRow(bucket=0, power_a=210.0, power_b=200.0,
                    cadence_a=90.0, cadence_b=90.0, n_a=2, n_b=2)
    assert r.delta == 10.0
    assert abs(r.ratio - 1.05) < 1e-9


def test_reconcile_rejects_cross_capture_mono(conn, captures_dir):
    _import(conn, captures_dir, MULTI)
    with pytest.raises(ValueError):
        js.reconcile(conn, "stages", "assioma",
                     capture_a=MULTI, capture_b=FTMS_ERG, basis="mono")
