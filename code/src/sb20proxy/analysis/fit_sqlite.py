#!/usr/bin/env python3
"""Import a Garmin ``.fit`` into the SQLite index: per-record power + the laps.

The ``.fit`` is the committed reference stream (e.g. the Assioma over a head unit);
this derives a queryable table so a sniffed BLE power stream can be reconciled against
it with a timestamp JOIN — the same index the pcap importer feeds. ``fitparse`` is the
I/O seam (``read_fit``); ``load_fit`` is pure and unit-tested with synthetic records.

Tables (created idempotently; no core schema bump):
  * ``fit_record`` — one row per ``record`` message (t, power, cadence, speed, hr)
  * ``fit_lap``    — one row per ``lap`` message (start, elapsed, avg/max power)
"""

from __future__ import annotations

import hashlib
import sqlite3
from collections.abc import Iterable
from datetime import datetime, timezone
from pathlib import Path

_FIT_SCHEMA = """
CREATE TABLE IF NOT EXISTS fit_record (
    capture_id  INTEGER NOT NULL REFERENCES capture(capture_id) ON DELETE CASCADE,
    rec_index   INTEGER NOT NULL,
    t_epoch     REAL,
    iso_time    TEXT,
    power_w      INTEGER,
    cadence_rpm  INTEGER,
    speed_kmh    REAL,
    hr_bpm       INTEGER,
    PRIMARY KEY (capture_id, rec_index)
);
CREATE INDEX IF NOT EXISTS ix_fit_rec_t ON fit_record(capture_id, t_epoch);

CREATE TABLE IF NOT EXISTS fit_lap (
    capture_id   INTEGER NOT NULL REFERENCES capture(capture_id) ON DELETE CASCADE,
    lap_index    INTEGER NOT NULL,
    start_iso    TEXT,
    elapsed_s    REAL,
    avg_power_w  INTEGER,
    max_power_w  INTEGER,
    PRIMARY KEY (capture_id, lap_index)
);
"""


# --------------------------------------------------------------------------- #
# pure helpers
# --------------------------------------------------------------------------- #
def _as_utc(dt: datetime | None) -> datetime | None:
    if dt is None:
        return None
    return dt.replace(tzinfo=timezone.utc) if dt.tzinfo is None else dt


def _epoch(dt: datetime | None) -> float | None:
    dt = _as_utc(dt)
    return dt.timestamp() if dt else None


def _iso(dt: datetime | None) -> str | None:
    dt = _as_utc(dt)
    return dt.isoformat().replace("+00:00", "Z") if dt else None


def _kmh(speed_ms: float | None) -> float | None:
    return round(speed_ms * 3.6, 2) if isinstance(speed_ms, (int, float)) else None


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


# --------------------------------------------------------------------------- #
# fitparse I/O seam
# --------------------------------------------------------------------------- #
def read_fit(fit_path: str | Path) -> tuple[list[dict], list[dict]]:
    """I/O seam: parse a .fit with fitparse into (records, laps) dict lists."""
    import fitparse  # lazy: the ``analysis`` extra; not needed for the unit tests

    fit = fitparse.FitFile(str(fit_path))
    records = [{f.name: f.value for f in m.fields} for m in fit.get_messages("record")]
    laps = [{f.name: f.value for f in m.fields} for m in fit.get_messages("lap")]
    return records, laps


# --------------------------------------------------------------------------- #
# DB load (pure; synthetic-testable)
# --------------------------------------------------------------------------- #
def load_fit(conn: sqlite3.Connection, filename: str, records: Iterable[dict],
             laps: Iterable[dict], *, sha256: str = "") -> dict:
    """Insert FIT records + laps under a new ``capture`` row. Returns a stats dict."""
    conn.executescript(_FIT_SCHEMA)
    records = list(records)
    laps = list(laps)

    epochs = [_epoch(r.get("timestamp")) for r in records]
    epochs = [e for e in epochs if e is not None]
    started_iso = _iso(records[0].get("timestamp")) if records else None

    if conn.execute("SELECT 1 FROM capture WHERE filename = ?", (filename,)).fetchone():
        conn.execute("DELETE FROM capture WHERE filename = ?", (filename,))  # cascades

    cur = conn.execute(
        """INSERT INTO capture (filename, sha256, protocol, n_records, started_iso, imported_at)
           VALUES (?, ?, 'fit', ?, ?, ?)""",
        (filename, sha256, len(records), started_iso,
         datetime.now(tz=timezone.utc).isoformat().replace("+00:00", "Z")),
    )
    cid = cur.lastrowid

    n_power = 0
    for idx, r in enumerate(records):
        power = r.get("power")
        if isinstance(power, (int, float)):
            n_power += 1
        conn.execute(
            """INSERT INTO fit_record
               (capture_id, rec_index, t_epoch, iso_time, power_w, cadence_rpm, speed_kmh, hr_bpm)
               VALUES (?,?,?,?,?,?,?,?)""",
            (cid, idx, _epoch(r.get("timestamp")), _iso(r.get("timestamp")),
             power if isinstance(power, (int, float)) else None,
             r.get("cadence"), _kmh(r.get("speed")), r.get("heart_rate")),
        )
    for i, lp in enumerate(laps):
        conn.execute(
            """INSERT INTO fit_lap
               (capture_id, lap_index, start_iso, elapsed_s, avg_power_w, max_power_w)
               VALUES (?,?,?,?,?,?)""",
            (cid, i, _iso(lp.get("start_time")), lp.get("total_elapsed_time"),
             lp.get("avg_power"), lp.get("max_power")),
        )
    conn.commit()
    return {"records": len(records), "laps": len(laps), "power_records": n_power,
            "capture_id": cid}


def import_fit(conn: sqlite3.Connection, fit_path: str | Path) -> dict:
    """Top-level: fitparse -> load. ``filename`` (basename) is the capture key."""
    fit_path = Path(fit_path)
    records, laps = read_fit(fit_path)
    return load_fit(conn, fit_path.name, records, laps, sha256=_sha256(fit_path))
