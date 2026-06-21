"""Tests for the annotation layer over the JSONL→SQLite analysis index.

Hermetic: imports the *real committed captures* (via the `captures_dir` fixture
in conftest) into an in-memory DB and exercises the annotation sidecars + table.
No network, no hardware. Annotation coordinates come from the bytes the actual
SB20 produced (the `ble_erg_hold` records in the erg captures), never invented.

All sidecar *writes* go to a `tmp_path` directory so the committed sidecars in
`findings/annotations/` are never mutated by the suite.
"""

from __future__ import annotations

import json
from statistics import median

import pytest

from sb20proxy.analysis import annotations as anno
from sb20proxy.analysis import jsonl_sqlite as js

# Real committed captures (see findings/captures/README.md):
ERG3WAY = "G-sb20-ftms-erg3way-20260621-110555.jsonl"   # ble_erg_hold 100/150/200 W (the anchor)
ERG200 = "G-sb20-ftms-erg200-20260621-104341.jsonl"     # single 200 W / 60 s hold
MULTI = "QUICK-multi-20260615-064037.jsonl"             # ANT+ stages/assioma (no erg holds)


@pytest.fixture
def conn():
    c = js.connect(":memory:")
    yield c
    c.close()


def _import(conn, captures_dir, name) -> js.ImportStats:
    return js.import_capture(conn, captures_dir / name)


# --------------------------------------------------------------------------- #
# the annotation table exists on a fresh DB (created alongside the index)
# --------------------------------------------------------------------------- #
def test_annotation_table_created_with_schema(conn):
    """`connect()` creates the annotation table so power_sample JOINs work cold."""
    tables = {
        r["name"]
        for r in conn.execute("SELECT name FROM sqlite_master WHERE type='table'")
    }
    assert "annotation" in tables


# --------------------------------------------------------------------------- #
# Annotation dataclass validation + sidecar JSON round-trip
# --------------------------------------------------------------------------- #
def test_annotation_requires_a_range():
    with pytest.raises(ValueError):
        anno.Annotation(filename=ERG3WAY, label="coast")   # no rec_* or t_* range


def test_annotation_rejects_inverted_ranges():
    with pytest.raises(ValueError):
        anno.Annotation(filename=ERG3WAY, label="x", rec_start=10, rec_end=5)
    with pytest.raises(ValueError):
        anno.Annotation(filename=ERG3WAY, label="x", t_start_s=10.0, t_end_s=5.0)


def test_sidecar_json_roundtrip_drops_none_fields():
    ann = anno.Annotation(
        filename=ERG3WAY, label="erg_hold_target=150W",
        t_start_s=52.09, t_end_s=82.09, source="auto:erg_hold",
    )
    obj = ann.to_json_obj()
    # only the populated fields are serialised (no rec_* keys, no flag/note)
    assert set(obj) == {"filename", "label", "t_start_s", "t_end_s", "source"}
    assert anno.Annotation.from_json_obj(obj) == ann


def test_sidecar_file_roundtrip(tmp_path):
    """Write a sidecar via append_sidecar, read it back identically."""
    ann = anno.Annotation(
        filename=ERG3WAY, label="coast", rec_start=200, rec_end=210,
        flag="suspect-artefact", note="release tail", source="manual",
    )
    path = anno.append_sidecar(ann, annotations_dir=tmp_path)
    assert path.name == f"{ERG3WAY}.jsonl"      # <capture-filename>.jsonl
    loaded = anno.load_sidecar(path)
    assert loaded == [ann]


def test_append_sidecar_dedupes_identical_lines(tmp_path):
    ann = anno.Annotation(filename=ERG3WAY, label="coast", rec_start=1, rec_end=2)
    anno.append_sidecar(ann, annotations_dir=tmp_path)
    anno.append_sidecar(ann, annotations_dir=tmp_path)      # identical -> no-op
    path = anno.sidecar_path(ERG3WAY, annotations_dir=tmp_path)
    assert len(anno.load_sidecar(path)) == 1


# --------------------------------------------------------------------------- #
# auto-annotation labels the real erg holds (the use-case anchor)
# --------------------------------------------------------------------------- #
def test_auto_annotate_labels_real_erg_holds(conn, captures_dir, tmp_path):
    """The anchor capture's three holds (100/150/200 W) are labelled with the
    right targets and monotonic windows — the power-topology Phase-2 cells."""
    _import(conn, captures_dir, ERG3WAY)
    derived = anno.auto_annotate(conn, annotations_dir=tmp_path)
    by_label = {a.label: a for a in derived if a.filename == ERG3WAY}
    assert set(by_label) == {
        "erg_hold_target=100W", "erg_hold_target=150W", "erg_hold_target=200W",
    }
    # each window starts at the ble_erg_hold record's monotonic_s and lasts hold_s
    holds = [
        json.loads(r["raw_json"])
        for r in conn.execute(
            "SELECT raw_json FROM record WHERE kind='ble_erg_hold' ORDER BY rec_index"
        )
    ]
    assert len(holds) == 3
    for h in holds:
        ann = by_label[f"erg_hold_target={h['target_w']}W"]
        assert ann.t_start_s == pytest.approx(h["monotonic_s"])
        assert ann.t_end_s == pytest.approx(h["monotonic_s"] + h["hold_s"])
        assert ann.source == "auto:erg_hold"
        # the rec_index window is non-empty and inside the file
        assert ann.rec_start is not None and ann.rec_end >= ann.rec_start


def test_auto_annotate_is_idempotent(conn, captures_dir, tmp_path):
    """Running the derive pass twice doesn't duplicate rows or sidecar lines."""
    _import(conn, captures_dir, ERG3WAY)
    anno.auto_annotate(conn, annotations_dir=tmp_path)
    n1 = conn.execute("SELECT COUNT(*) FROM annotation").fetchone()[0]
    anno.auto_annotate(conn, annotations_dir=tmp_path)
    n2 = conn.execute("SELECT COUNT(*) FROM annotation").fetchone()[0]
    assert n1 == n2 == 3
    path = anno.sidecar_path(ERG3WAY, annotations_dir=tmp_path)
    assert len(anno.load_sidecar(path)) == 3


def test_auto_annotate_skips_captures_without_holds(conn, captures_dir, tmp_path):
    """A capture with no ble_erg_hold records yields no annotations."""
    _import(conn, captures_dir, MULTI)
    derived = anno.auto_annotate(conn, annotations_dir=tmp_path)
    assert derived == []
    assert conn.execute("SELECT COUNT(*) FROM annotation").fetchone()[0] == 0


# --------------------------------------------------------------------------- #
# the JOIN against power_sample (the note's headline query)
# --------------------------------------------------------------------------- #
def test_annotated_samples_join_ftms_inside_a_hold(conn, captures_dir, tmp_path):
    """The 150 W and 200 W holds' FTMS samples JOIN out at ~their targets.

    This is the note's "median ratio inside a labelled cell" query, here as a
    single-stream median power inside the labelled erg cell.
    """
    _import(conn, captures_dir, ERG3WAY)
    anno.auto_annotate(conn, annotations_dir=tmp_path)

    s150 = anno.annotated_samples(conn, ERG3WAY, "erg_hold_target=150W", stream="ftms")
    s200 = anno.annotated_samples(conn, ERG3WAY, "erg_hold_target=200W", stream="ftms")
    assert len(s150) > 10 and len(s200) > 10
    assert all(s.stream == "ftms" for s in s150)
    m150 = median([s.power_w for s in s150 if s.power_w is not None])
    m200 = median([s.power_w for s in s200 if s.power_w is not None])
    # SB20 FTMS holds near its commanded target during the labelled window
    assert 130 <= m150 <= 175
    assert 180 <= m200 <= 230
    # higher target -> higher median power inside the cell
    assert m200 > m150


def test_annotated_samples_respects_label_and_capture(conn, captures_dir, tmp_path):
    """A label only returns rows from its own capture's window."""
    _import(conn, captures_dir, ERG3WAY)
    _import(conn, captures_dir, ERG200)
    anno.auto_annotate(conn, annotations_dir=tmp_path)
    # both ERG3WAY and ERG200 have a 200 W label; querying ERG200 must not bleed
    s = anno.annotated_samples(conn, ERG200, "erg_hold_target=200W", stream="ftms")
    assert len(s) > 10
    # the ERG200 hold is 60 s — more samples than the 30 s erg3way hold
    s_3way = anno.annotated_samples(conn, ERG3WAY, "erg_hold_target=200W", stream="ftms")
    assert len(s) > len(s_3way)


# --------------------------------------------------------------------------- #
# filename-keying: re-importing a capture does NOT drop its annotations
# --------------------------------------------------------------------------- #
def test_reimport_capture_does_not_drop_annotations(conn, captures_dir, tmp_path):
    """The headline invariant: annotation is keyed by filename, not capture_id,
    so a capture re-import (which CASCADE-drops record rows) keeps annotations."""
    _import(conn, captures_dir, ERG3WAY)
    anno.auto_annotate(conn, annotations_dir=tmp_path)
    before = conn.execute("SELECT COUNT(*) FROM annotation").fetchone()[0]
    assert before == 3

    # force a wholesale re-import (drops + re-inserts capture/record rows)
    js.import_capture(conn, captures_dir / ERG3WAY, replace=True)
    after = conn.execute("SELECT COUNT(*) FROM annotation").fetchone()[0]
    assert after == before                     # annotations survived
    # and they still JOIN (the new capture_id resolves through filename)
    s = anno.annotated_samples(conn, ERG3WAY, "erg_hold_target=150W", stream="ftms")
    assert len(s) > 10


# --------------------------------------------------------------------------- #
# --rebuild survival: the table re-materialises losslessly from the sidecars
# --------------------------------------------------------------------------- #
def test_rematerialise_from_sidecars_is_lossless(conn, captures_dir, tmp_path):
    """Dropping + rebuilding the table from the committed sidecars reproduces it
    exactly — the property that lets the .sqlite stay derived/gitignored."""
    _import(conn, captures_dir, ERG3WAY)
    derived = anno.auto_annotate(conn, annotations_dir=tmp_path)
    snapshot = _annotation_rows(conn)
    assert len(snapshot) == len(derived) == 3

    # simulate `--rebuild`: blow the table away, rebuild from sidecars alone
    loaded = anno.rematerialise_annotations(conn, annotations_dir=tmp_path)
    assert loaded == 3
    assert _annotation_rows(conn) == snapshot


def test_rebuild_after_dropping_whole_db_recovers_from_sidecars(captures_dir, tmp_path):
    """A brand-new DB (the real --rebuild: delete .sqlite, re-import) recovers the
    annotation table from the committed sidecars, not from any prior DB state."""
    # author into sidecars via DB #1
    c1 = js.connect(":memory:")
    js.import_capture(c1, captures_dir / ERG3WAY)
    anno.auto_annotate(c1, annotations_dir=tmp_path)
    c1.close()

    # DB #2 starts empty; only the sidecars carry the annotations
    c2 = js.connect(":memory:")
    assert c2.execute("SELECT COUNT(*) FROM annotation").fetchone()[0] == 0
    js.import_capture(c2, captures_dir / ERG3WAY)
    loaded = anno.rematerialise_annotations(c2, annotations_dir=tmp_path)
    assert loaded == 3
    s = anno.annotated_samples(c2, ERG3WAY, "erg_hold_target=200W", stream="ftms")
    assert len(s) > 10
    c2.close()


# --------------------------------------------------------------------------- #
# annotate(): upsert table + append sidecar in one call
# --------------------------------------------------------------------------- #
def test_annotate_upserts_table_and_appends_sidecar(conn, captures_dir, tmp_path):
    _import(conn, captures_dir, ERG3WAY)
    ann = anno.annotate(
        conn, ERG3WAY, "coast",
        rec_start=224, rec_end=240, flag="suspect-artefact",
        note="release tail after 200 W hold", annotations_dir=tmp_path,
    )
    # table row present
    row = conn.execute(
        "SELECT label, flag, note, source FROM annotation WHERE label='coast'"
    ).fetchone()
    assert row is not None
    assert row["flag"] == "suspect-artefact"
    assert row["source"] == "manual"
    # sidecar appended with the same record
    path = anno.sidecar_path(ERG3WAY, annotations_dir=tmp_path)
    assert ann in anno.load_sidecar(path)


def test_annotate_then_rematerialise_reproduces_manual_annotation(conn, captures_dir, tmp_path):
    """A DB-first manual annotate() survives a rematerialise (sidecar is canonical)."""
    _import(conn, captures_dir, ERG3WAY)
    anno.annotate(
        conn, ERG3WAY, "steady_block",
        t_start_s=20.0, t_end_s=40.0, note="manual steady call",
        annotations_dir=tmp_path,
    )
    loaded = anno.rematerialise_annotations(conn, annotations_dir=tmp_path)
    assert loaded == 1
    row = conn.execute(
        "SELECT label, t_start_s, t_end_s FROM annotation WHERE label='steady_block'"
    ).fetchone()
    assert row is not None
    assert row["t_start_s"] == pytest.approx(20.0)


def _annotation_rows(conn) -> list[tuple]:
    """The annotation table as comparable tuples, ordered stably (ignoring the id)."""
    return conn.execute(
        "SELECT filename, label, rec_start, rec_end, t_start_s, t_end_s, flag, note, source "
        "FROM annotation ORDER BY filename, label, rec_start, t_start_s"
    ).fetchall()
