"""Annotation layer — committed-text segment labels over the JSONL captures.

The companion to :mod:`sb20proxy.analysis.jsonl_sqlite`. Where that module builds
a *derived, gitignored* SQLite index over the canonical JSONL captures, this one
adds **annotations**: segment labels (``erg_hold_target=150W``, ``coast``), point
flags (``suspect-artefact``) and free-text notes, bound to a capture by
**immutable coordinates** so they survive any re-import.

The one hard rule — same as the captures
----------------------------------------
The JSONL captures are canonical committed *text* and are never edited. An
annotation is *also* canonical committed text: it lives in a sidecar
``findings/annotations/<capture-filename>.jsonl`` (one JSON object per line), so a
PR shows a reviewable diff and two sessions can merge it. The ``annotation`` table
materialised into the SQLite DB is a **derived cache** — ``--rebuild`` reproduces
it losslessly from the sidecars and nothing is lost.

An annotation never references a surrogate key. It binds to:

* the capture **filename** (which never changes — captures are immutable), and
* a range, exactly one of:
    * a ``rec_index`` range — ``rec_start`` .. ``rec_end`` (0-based, inclusive), or
    * a ``monotonic_s`` range — ``t_start_s`` .. ``t_end_s`` (seconds, inclusive).

Because the table is keyed by ``filename`` (not FK-cascaded to ``capture_id``),
re-importing a capture — which drops and re-inserts its ``capture``/``record`` rows
— **does not drop its annotations**. :func:`rematerialise_annotations` reloads the
table from the committed sidecars on every build.

Sidecar record schema (one JSON object per line)
------------------------------------------------
======================  ========  ======================================================
field                   req?      meaning
======================  ========  ======================================================
``filename``            yes       the capture this binds to (``*.jsonl`` name)
``label``               yes       segment label, e.g. ``erg_hold_target=150W``, ``coast``
``rec_start``/``rec_end``  one*   inclusive 0-based ``rec_index`` range
``t_start_s``/``t_end_s``  one*   inclusive ``monotonic_s`` range (seconds)
``flag``                no        e.g. ``suspect-artefact``
``note``                no        free text
``source``              no        provenance, e.g. ``auto:erg_hold`` / ``manual``
======================  ========  ======================================================

\\* Provide **at least one** coordinate range (``rec_*`` and/or ``t_*``). The
``power_sample`` JOIN uses whichever range is populated (see :func:`annotated_samples`).

:func:`annotate` is a DB-first authoring convenience: it upserts the table *and*
appends the canonical sidecar in one call. The sidecar remains the source of truth.
"""

from __future__ import annotations

import json
import sqlite3
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from pathlib import Path

from sb20proxy.analysis._jsonl import iter_jsonl_lines, read_jsonl_text

# The annotation table is keyed by `filename` (capture coordinates), *not* by a
# FK to capture(capture_id), so a capture re-import (which CASCADE-drops record
# rows) never drops its annotations. It is re-materialised from the sidecars on
# every build, so it stays a disposable cache like the rest of the DB.
ANNOTATION_SCHEMA = """
CREATE TABLE IF NOT EXISTS annotation (
    annotation_id INTEGER PRIMARY KEY,
    filename      TEXT    NOT NULL,        -- capture filename (immutable coordinate)
    label         TEXT    NOT NULL,        -- segment label, e.g. erg_hold_target=150W
    rec_start     INTEGER,                 -- inclusive 0-based rec_index range ...
    rec_end       INTEGER,
    t_start_s     REAL,                    -- ... or inclusive monotonic_s range
    t_end_s       REAL,
    flag          TEXT,                    -- e.g. suspect-artefact
    note          TEXT,
    source        TEXT                     -- provenance, e.g. auto:erg_hold / manual
);
-- Dedup key over the coordinate. Via COALESCE-to-sentinel because a plain
-- UNIQUE(...) treats NULLs as DISTINCT in SQLite — so a single-basis annotation
-- (rec-only or time-only, the common manual case) would never dedup and would
-- duplicate on every re-materialise. rec_index >= 0 and monotonic_s >= 0, so the
-- negative sentinels can't collide with a real coordinate.
CREATE UNIQUE INDEX IF NOT EXISTS ux_annotation_coord ON annotation(
    filename, label,
    COALESCE(rec_start, -1), COALESCE(rec_end, -1),
    COALESCE(t_start_s, -1.0), COALESCE(t_end_s, -1.0)
);
CREATE INDEX IF NOT EXISTS ix_annotation_file ON annotation(filename);
CREATE INDEX IF NOT EXISTS ix_annotation_label ON annotation(label);
"""

# Default location of the committed sidecars, relative to this file:
# code/src/sb20proxy/analysis/annotations.py -> code/findings/annotations
_DEFAULT_ANNOTATIONS_DIR = (
    Path(__file__).resolve().parents[3] / "findings" / "annotations"
)

# Fields that round-trip through the sidecar JSON.
_FIELDS = (
    "filename", "label", "rec_start", "rec_end",
    "t_start_s", "t_end_s", "flag", "note", "source",
)


# --------------------------------------------------------------------------- #
# the annotation record
# --------------------------------------------------------------------------- #
@dataclass
class Annotation:
    """One segment label / flag / note bound to a capture by immutable coordinates."""

    filename: str
    label: str
    rec_start: int | None = None
    rec_end: int | None = None
    t_start_s: float | None = None
    t_end_s: float | None = None
    flag: str | None = None
    note: str | None = None
    source: str | None = None

    def __post_init__(self) -> None:
        if not self.filename:
            raise ValueError("annotation needs a capture filename")
        if not self.label:
            raise ValueError("annotation needs a label")
        # A range needs BOTH ends — a half-open range silently JOINs to nothing
        # (`x BETWEEN 5 AND NULL` is never true in SQLite), so reject it at source.
        if (self.rec_start is None) != (self.rec_end is None):
            raise ValueError(
                f"annotation {self.label!r}: a rec range needs both rec_start and rec_end"
            )
        if (self.t_start_s is None) != (self.t_end_s is None):
            raise ValueError(
                f"annotation {self.label!r}: a time range needs both t_start_s and t_end_s"
            )
        has_rec = self.rec_start is not None and self.rec_end is not None
        has_time = self.t_start_s is not None and self.t_end_s is not None
        if not (has_rec or has_time):
            raise ValueError(
                f"annotation {self.label!r} needs a complete rec_index or monotonic_s range"
            )
        if has_rec and self.rec_end < self.rec_start:
            raise ValueError(f"annotation {self.label!r}: rec_end < rec_start")
        if has_time and self.t_end_s < self.t_start_s:
            raise ValueError(f"annotation {self.label!r}: t_end_s < t_start_s")

    def to_json_obj(self) -> dict:
        """The sidecar line shape — only the populated fields, stable key order."""
        obj: dict = {"filename": self.filename, "label": self.label}
        for key in _FIELDS[2:]:
            val = getattr(self, key)
            if val is not None:
                obj[key] = val
        return obj

    @classmethod
    def from_json_obj(cls, obj: dict) -> Annotation:
        return cls(**{k: v for k, v in obj.items() if k in _FIELDS})


# --------------------------------------------------------------------------- #
# schema + (re)materialisation
# --------------------------------------------------------------------------- #
def create_annotation_schema(conn: sqlite3.Connection) -> None:
    """Create the ``annotation`` table + indexes (idempotent)."""
    conn.executescript(ANNOTATION_SCHEMA)
    conn.commit()


def _insert(conn: sqlite3.Connection, ann: Annotation) -> None:
    conn.execute(
        """INSERT OR IGNORE INTO annotation
           (filename, label, rec_start, rec_end, t_start_s, t_end_s, flag, note, source)
           VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)""",
        (ann.filename, ann.label, ann.rec_start, ann.rec_end,
         ann.t_start_s, ann.t_end_s, ann.flag, ann.note, ann.source),
    )


def materialise_annotations(conn: sqlite3.Connection, anns: Iterable[Annotation]) -> int:
    """Upsert annotations into the table (dedup on the coordinate UNIQUE key).

    Returns the number of rows actually **inserted** (an ``INSERT OR IGNORE`` that
    hits the UNIQUE key changes nothing and is not counted), not the number tried —
    so a re-run over already-present annotations correctly reports 0.
    """
    create_annotation_schema(conn)
    before = conn.total_changes
    with conn:
        for ann in anns:
            _insert(conn, ann)
    return conn.total_changes - before


def rematerialise_annotations(
    conn: sqlite3.Connection, annotations_dir: str | Path | None = None
) -> int:
    """Rebuild the ``annotation`` table from the committed sidecars (lossless).

    Clears the table and reloads every ``*.jsonl`` sidecar, so the DB is a pure
    function of the committed text — exactly what ``--rebuild`` needs. Returns the
    number of annotations loaded.
    """
    create_annotation_schema(conn)
    with conn:
        conn.execute("DELETE FROM annotation")
    return materialise_annotations(conn, load_dir(annotations_dir))


# --------------------------------------------------------------------------- #
# sidecar I/O (the canonical committed text)
# --------------------------------------------------------------------------- #
def sidecar_path(capture_filename: str, annotations_dir: str | Path | None = None) -> Path:
    """The sidecar path for a capture: ``<annotations_dir>/<capture-filename>.jsonl``."""
    base = Path(annotations_dir) if annotations_dir is not None else _DEFAULT_ANNOTATIONS_DIR
    # Key on the bare capture filename (a path component), never a directory.
    return base / f"{Path(capture_filename).name}.jsonl"


def load_sidecar(path: str | Path) -> list[Annotation]:
    """Read one sidecar file into :class:`Annotation` objects.

    Tolerant of a torn / non-object line (it's skipped, not fatal) — a single bad
    sidecar line must not abort a whole ``--rebuild``.
    """
    path = Path(path)
    if not path.exists():
        return []
    out: list[Annotation] = []
    for _line, obj in iter_jsonl_lines(read_jsonl_text(path)):
        if obj is not None:
            out.append(Annotation.from_json_obj(obj))
    return out


def load_dir(annotations_dir: str | Path | None = None) -> Iterator[Annotation]:
    """Yield every annotation across all sidecars in the directory (sorted)."""
    base = Path(annotations_dir) if annotations_dir is not None else _DEFAULT_ANNOTATIONS_DIR
    if not base.exists():
        return
    for path in sorted(base.glob("*.jsonl")):
        yield from load_sidecar(path)


def append_sidecar(ann: Annotation, annotations_dir: str | Path | None = None) -> Path:
    """Append one annotation to its capture's sidecar (creating the dir/file).

    De-duplicates: an identical line already present is not re-appended, so
    :func:`annotate` / :func:`auto_annotate` are safe to call twice. Returns the
    sidecar path.
    """
    path = sidecar_path(ann.filename, annotations_dir)
    path.parent.mkdir(parents=True, exist_ok=True)
    line = json.dumps(ann.to_json_obj(), separators=(", ", ": "))
    existing = (
        {ln.strip() for ln in path.read_text(encoding="utf-8").splitlines()}
        if path.exists()
        else set()
    )
    if line in existing:
        return path
    with path.open("a", encoding="utf-8") as fh:
        fh.write(line + "\n")
    return path


# --------------------------------------------------------------------------- #
# DB-first authoring convenience
# --------------------------------------------------------------------------- #
def annotate(
    conn: sqlite3.Connection,
    filename: str,
    label: str,
    *,
    rec_start: int | None = None,
    rec_end: int | None = None,
    t_start_s: float | None = None,
    t_end_s: float | None = None,
    flag: str | None = None,
    note: str | None = None,
    source: str | None = "manual",
    annotations_dir: str | Path | None = None,
) -> Annotation:
    """Author one annotation: upsert the table **and** append the canonical sidecar.

    DB-first convenience — but the committed sidecar is the source of truth, so a
    later ``--rebuild`` reproduces this exact row from it. Returns the
    :class:`Annotation`.
    """
    ann = Annotation(
        filename=filename, label=label, rec_start=rec_start, rec_end=rec_end,
        t_start_s=t_start_s, t_end_s=t_end_s, flag=flag, note=note, source=source,
    )
    create_annotation_schema(conn)
    with conn:
        _insert(conn, ann)
    append_sidecar(ann, annotations_dir)
    return ann


# --------------------------------------------------------------------------- #
# auto-annotation — derive labels from the captures
# --------------------------------------------------------------------------- #
def auto_annotations_for_capture(conn: sqlite3.Connection, filename: str) -> list[Annotation]:
    """Derive annotations for one already-imported capture.

    Currently derives **erg holds**: each ``ble_erg_hold`` record (``target_w`` +
    ``hold_s``) becomes an ``erg_hold_target=<N>W`` segment spanning the hold's
    ``monotonic_s`` window — and its ``rec_index`` window, so the label JOINs
    against ``power_sample`` on either basis. These are exactly the windows a
    per-hold SB20-vs-Assioma ratio (power-topology Phase 2) needs.
    """
    cap = conn.execute(
        "SELECT capture_id FROM capture WHERE filename = ?", (filename,)
    ).fetchone()
    if cap is None:
        return []
    return _erg_hold_annotations(conn, cap["capture_id"], filename)


def _erg_hold_annotations(
    conn: sqlite3.Connection, capture_id: int, filename: str
) -> list[Annotation]:
    """One ``erg_hold_target=<N>W`` segment per ``ble_erg_hold`` record.

    The hold record marks the *start* of the hold (its ``monotonic_s``) and carries
    ``hold_s``; the window is ``[t, t + hold_s]``. The ``rec_index`` window is the
    span of records whose ``monotonic_s`` falls in that window (so a rec-based JOIN
    works even where ``monotonic_s`` is absent).
    """
    holds = conn.execute(
        """SELECT rec_index, monotonic_s, raw_json FROM record
           WHERE capture_id = ? AND kind = 'ble_erg_hold' ORDER BY rec_index""",
        (capture_id,),
    ).fetchall()
    out: list[Annotation] = []
    for h in holds:
        rec = json.loads(h["raw_json"])
        target_w = rec.get("target_w")
        hold_s = rec.get("hold_s")
        t_start = h["monotonic_s"] if h["monotonic_s"] is not None else rec.get("monotonic_s")
        if target_w is None or t_start is None or hold_s is None:
            continue
        t_end = float(t_start) + float(hold_s)
        rec_start, rec_end = _rec_span_for_window(conn, capture_id, float(t_start), t_end)
        out.append(
            Annotation(
                filename=filename,
                label=f"erg_hold_target={int(target_w)}W",
                rec_start=rec_start,
                rec_end=rec_end,
                t_start_s=float(t_start),
                t_end_s=t_end,
                note=f"erg hold {int(target_w)} W for {float(hold_s):g} s",
                source="auto:erg_hold",
            )
        )
    return out


def _rec_span_for_window(
    conn: sqlite3.Connection, capture_id: int, t_start: float, t_end: float
) -> tuple[int | None, int | None]:
    """Min/max ``rec_index`` of records whose ``monotonic_s`` is in ``[t_start, t_end]``."""
    row = conn.execute(
        """SELECT MIN(rec_index) AS lo, MAX(rec_index) AS hi FROM record
           WHERE capture_id = ? AND monotonic_s IS NOT NULL
             AND monotonic_s BETWEEN ? AND ?""",
        (capture_id, t_start, t_end),
    ).fetchone()
    return (row["lo"], row["hi"]) if row is not None else (None, None)


def auto_annotate(
    conn: sqlite3.Connection,
    *,
    annotations_dir: str | Path | None = None,
    write_sidecars: bool = True,
) -> list[Annotation]:
    """Derive annotations for every imported capture, materialise + persist them.

    Materialises into the ``annotation`` table (dedup on the coordinate key, so
    re-running is a no-op) and — unless ``write_sidecars=False`` — appends each to
    its capture's committed sidecar. Returns the derived annotations.
    """
    captures = conn.execute("SELECT filename FROM capture ORDER BY filename").fetchall()
    derived: list[Annotation] = []
    for cap in captures:
        derived.extend(auto_annotations_for_capture(conn, cap["filename"]))
    materialise_annotations(conn, derived)
    if write_sidecars:
        for ann in derived:
            append_sidecar(ann, annotations_dir)
    return derived


# --------------------------------------------------------------------------- #
# the JOIN against power_sample
# --------------------------------------------------------------------------- #
@dataclass
class AnnotatedSample:
    """One ``power_sample`` row falling inside an annotated segment."""

    label: str
    stream: str
    monotonic_s: float | None
    iso_time: str | None
    power_w: float | None
    cadence_rpm: float | None


def annotated_samples(
    conn: sqlite3.Connection,
    filename: str,
    label: str,
    *,
    stream: str | None = None,
) -> list[AnnotatedSample]:
    """Return the ``power_sample`` rows inside a labelled segment (the note's JOIN).

    Joins ``annotation`` to ``power_sample`` on the capture filename and the
    segment's range — by ``monotonic_s`` when the annotation carries a time range,
    else by ``rec_index``. Optionally restrict to one ``stream`` (e.g. ``ftms``).
    This is the machine-queryable form of "median ratio inside a labelled cell".
    """
    where = ["a.label = ?", "a.filename = ?"]
    params: list = [label, filename]
    if stream is not None:
        where.append("ps.stream = ?")
        params.append(stream)
    # Prefer the time range when it's fully populated, else fall back to the rec
    # range. Both ends are required per branch (a half-range never matches), and
    # __post_init__ already rejects partial ranges — this is belt-and-suspenders.
    range_clause = (
        "((a.t_start_s IS NOT NULL AND a.t_end_s IS NOT NULL"
        "  AND ps.monotonic_s BETWEEN a.t_start_s AND a.t_end_s)"
        " OR ((a.t_start_s IS NULL OR a.t_end_s IS NULL)"
        "     AND a.rec_start IS NOT NULL AND a.rec_end IS NOT NULL"
        "     AND ps.rec_index BETWEEN a.rec_start AND a.rec_end))"
    )
    sql = (
        "SELECT a.label AS label, ps.stream AS stream, ps.monotonic_s AS monotonic_s, "
        "       ps.iso_time AS iso_time, ps.power_w AS power_w, ps.cadence_rpm AS cadence_rpm "
        "FROM annotation a "
        "JOIN capture c ON c.filename = a.filename "
        "JOIN power_sample ps ON ps.capture_id = c.capture_id "
        f"WHERE {' AND '.join(where)} AND {range_clause} "
        "ORDER BY ps.rec_index"
    )
    rows = conn.execute(sql, params).fetchall()
    return [
        AnnotatedSample(
            label=r["label"], stream=r["stream"], monotonic_s=r["monotonic_s"],
            iso_time=r["iso_time"], power_w=r["power_w"], cadence_rpm=r["cadence_rpm"],
        )
        for r in rows
    ]


def as_sidecar_lines(anns: Iterable[Annotation]) -> str:
    """Render annotations as sidecar text (one JSON object per line, trailing NL)."""
    return "".join(
        json.dumps(a.to_json_obj(), separators=(", ", ": ")) + "\n" for a in anns
    )
