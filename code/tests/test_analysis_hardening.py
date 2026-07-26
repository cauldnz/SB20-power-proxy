"""Robustness + correctness fixes for the analysis layer (the hardening pass).

Covers the edge/error paths the original suite left open: malformed-input
tolerance (torn lines, stray bytes, bad CP hex), per-file import isolation, the
schema-version guard, the annotation one-sided-range rejection + rec-range JOIN,
and the accurate materialise count. Synthetic captures here are deliberately
malformed test inputs (not invented protocol bytes) to exercise the guards.
"""

from __future__ import annotations

import json
import sqlite3
from pathlib import Path

import pytest

from sb20proxy.analysis import annotations as anno
from sb20proxy.analysis import jsonl_sqlite as js


@pytest.fixture
def conn():
    c = js.connect(":memory:")
    yield c
    c.close()


def _write(path: Path, records: list[dict]) -> Path:
    path.write_text("\n".join(json.dumps(r) for r in records), encoding="utf-8")
    return path


# --------------------------------------------------------------------------- #
# import robustness — a torn line / stray byte / bad hex never aborts the import
# --------------------------------------------------------------------------- #
def test_torn_and_nonutf8_lines_kept_not_fatal(conn, tmp_path):
    good = json.dumps({"kind": "broadcast", "monotonic_s": 1.0, "source": "x",
                       "data": {"instantaneous_power_w": 50}})
    raw = (good + "\n" + '{"kind": "broad' + "\n").encode("utf-8") + b"\xff stray byte\n"
    p = tmp_path / "bad.jsonl"
    p.write_bytes(raw)

    stats = js.import_capture(conn, p)
    assert stats.error is None
    assert stats.n_unparsed == 2                      # torn line + stray-byte line
    # the good record still flattened
    assert conn.execute("SELECT COUNT(*) AS n FROM ant_broadcast").fetchone()["n"] == 1
    # torn lines kept losslessly in `record`
    assert conn.execute(
        "SELECT COUNT(*) AS n FROM record WHERE kind='_unparsed'").fetchone()["n"] == 2


def test_malformed_cp_raw_hex_does_not_crash(conn, tmp_path):
    p = _write(tmp_path / "cp.jsonl", [
        {"kind": "ble_cp_indication", "monotonic_s": 1.0, "char": "control_point",
         "raw_hex": "zz", "note": "garbage"},
    ])
    stats = js.import_capture(conn, p)
    assert stats.error is None
    row = conn.execute(
        "SELECT raw_hex, target_power_w FROM ble_control_point").fetchone()
    assert row["raw_hex"] == "zz" and row["target_power_w"] is None


def test_import_dir_isolates_a_failing_file(conn, tmp_path, monkeypatch):
    _write(tmp_path / "a.jsonl", [{"kind": "broadcast", "monotonic_s": 1.0,
                                   "source": "x", "data": {"instantaneous_power_w": 50}}])
    _write(tmp_path / "b.jsonl", [{"kind": "broadcast", "monotonic_s": 1.0,
                                   "source": "y", "data": {"instantaneous_power_w": 60}}])
    real = js.import_capture

    def flaky(c, path, **kw):
        if Path(path).name == "a.jsonl":
            raise OSError("simulated unreadable file")
        return real(c, path, **kw)

    monkeypatch.setattr(js, "import_capture", flaky)
    stats = {s.filename: s for s in js.import_dir(conn, tmp_path)}
    assert stats["a.jsonl"].error == "simulated unreadable file"   # recorded, not raised
    assert stats["b.jsonl"].error is None                          # the batch continued
    assert stats["b.jsonl"].capture_id is not None


# --------------------------------------------------------------------------- #
# schema-version guard
# --------------------------------------------------------------------------- #
def test_schema_version_mismatch_is_refused(tmp_path):
    db = tmp_path / "x.sqlite"
    js.connect(db).close()                                   # created at SCHEMA_VERSION
    bumped = sqlite3.connect(str(db))                        # simulate a future schema
    bumped.execute("PRAGMA user_version = 999")
    bumped.commit()
    bumped.close()
    with pytest.raises(js.SchemaVersionError):
        js.connect(db)


# --------------------------------------------------------------------------- #
# annotation correctness — one-sided ranges + the rec-range JOIN + insert count
# --------------------------------------------------------------------------- #
@pytest.mark.parametrize("kwargs", [
    {"rec_start": 5},               # rec range missing its end
    {"rec_end": 5},                 # rec range missing its start
    {"t_start_s": 1.0},             # time range missing its end
    {"t_end_s": 1.0},               # time range missing its start
])
def test_one_sided_range_rejected(kwargs):
    with pytest.raises(ValueError):
        anno.Annotation("f.jsonl", "x", **kwargs)


def test_complete_ranges_accepted():
    anno.Annotation("f.jsonl", "x", rec_start=1, rec_end=3)
    anno.Annotation("f.jsonl", "x", t_start_s=1.0, t_end_s=3.0)


def test_materialise_returns_actual_inserted_count(conn):
    anns = [anno.Annotation("f.jsonl", f"L{i}", rec_start=i, rec_end=i + 1) for i in range(3)]
    assert anno.materialise_annotations(conn, anns) == 3
    assert anno.materialise_annotations(conn, anns) == 0     # all dedup'd -> nothing new


def test_rec_range_annotation_joins_power_sample(conn, tmp_path):
    # a capture with no FTMS time-basis trick — just ANT broadcasts at rec_index 1..3
    p = _write(tmp_path / "ant.jsonl", [
        {"kind": "session_start", "sources": [{"label": "stages"}]},
        {"kind": "broadcast", "monotonic_s": 1.0, "source": "stages",
         "data": {"instantaneous_power_w": 100, "instantaneous_cadence_rpm": 90}},
        {"kind": "broadcast", "monotonic_s": 2.0, "source": "stages",
         "data": {"instantaneous_power_w": 110, "instantaneous_cadence_rpm": 91}},
        {"kind": "broadcast", "monotonic_s": 3.0, "source": "stages",
         "data": {"instantaneous_power_w": 120, "instantaneous_cadence_rpm": 92}},
    ])
    js.import_capture(conn, p)
    # a rec-only annotation (no time range) must hit the rec_index JOIN branch
    block = anno.Annotation("ant.jsonl", "block", rec_start=1, rec_end=3)
    anno.materialise_annotations(conn, [block])
    samples = anno.annotated_samples(conn, "ant.jsonl", "block", stream="stages")
    assert [s.power_w for s in samples] == [100, 110, 120]


def test_load_sidecar_skips_torn_line(tmp_path):
    p = tmp_path / "cap.jsonl.jsonl"
    good = json.dumps({"filename": "cap.jsonl", "label": "x", "rec_start": 1, "rec_end": 3})
    p.write_text(good + "\n" + '{"filename": "tor' + "\n", encoding="utf-8")
    anns = anno.load_sidecar(p)
    assert len(anns) == 1 and anns[0].label == "x"


# --------------------------------------------------------------------------- #
# control-point routing — the alias table and the CP set must not drift apart
# --------------------------------------------------------------------------- #
@pytest.mark.parametrize("spelling", sorted(
    js._CP_CHARS | {k for k, v in js._CHAR_ALIASES.items() if v in js._CP_CHARS}))
def test_every_control_point_spelling_routes_to_the_cp_table(conn, tmp_path, spelling):
    """A `ble_notification` on any known CP spelling is a CP *response*.

    Parametrised over the real maps, so a spelling added to `_CHAR_ALIASES` is
    covered automatically. This is the test that fails if the routing check ever
    goes back to matching the raw (pre-alias) char name.
    """
    p = _write(tmp_path / f"{spelling}.jsonl", [
        {"kind": "ble_notification", "monotonic_s": 1.0, "char": spelling,
         "raw_hex": "8004"},
    ])
    stats = js.import_capture(conn, p)
    assert stats.error is None

    assert conn.execute(
        "SELECT COUNT(*) AS n FROM ble_control_point").fetchone()["n"] == 1
    assert conn.execute(
        "SELECT COUNT(*) AS n FROM ble_notification").fetchone()["n"] == 0


def test_control_point_set_holds_canonical_names_only(conn):
    """`_CP_CHARS` is checked *after* aliasing, so a non-canonical member is dead.

    A member that is itself an alias key could never match, which is exactly the
    silent-misroute bug this arrangement removes.
    """
    assert all(js._norm_char(c) == c for c in js._CP_CHARS)


def test_a_non_control_point_notification_still_lands_in_ble_notification(conn, tmp_path):
    p = _write(tmp_path / "n.jsonl", [
        {"kind": "ble_notification", "monotonic_s": 1.0,
         "char": "cycling_power_measurement", "raw_hex": "0000"},
    ])
    assert js.import_capture(conn, p).error is None
    assert conn.execute(
        "SELECT COUNT(*) AS n FROM ble_notification").fetchone()["n"] == 1
    assert conn.execute(
        "SELECT COUNT(*) AS n FROM ble_control_point").fetchone()["n"] == 0