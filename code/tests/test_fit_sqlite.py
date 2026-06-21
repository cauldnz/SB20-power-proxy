"""Hermetic tests for the FIT->SQLite importer.

fitparse is the I/O seam (``read_fit``); ``load_fit`` and the time/unit helpers are
pure and driven here with synthetic ``record`` / ``lap`` dicts (the shape fitparse
yields), so no .fit file is needed.
"""

from __future__ import annotations

from datetime import datetime, timezone

from sb20proxy.analysis import fit_sqlite as fs
from sb20proxy.analysis import jsonl_sqlite as js

_UTC = timezone.utc


def test_time_and_unit_helpers():
    dt = datetime(2026, 6, 21, 7, 59, 48)  # naive -> treated as UTC
    assert fs._iso(dt) == "2026-06-21T07:59:48Z"
    assert fs._epoch(dt) == datetime(2026, 6, 21, 7, 59, 48, tzinfo=_UTC).timestamp()
    assert fs._iso(None) is None and fs._epoch(None) is None
    assert fs._kmh(8.0) == 28.8
    assert fs._kmh(None) is None


def test_load_fit_records_and_laps():
    conn = js.connect(":memory:")
    base = datetime(2026, 6, 21, 7, 59, 48, tzinfo=_UTC)
    later = datetime(2026, 6, 21, 8, 0, 48, tzinfo=_UTC)
    records = [
        {"timestamp": base, "power": 103, "cadence": 70, "speed": 8.0, "heart_rate": 120},
        {"timestamp": base.replace(second=49), "power": 180, "cadence": 72},
        {"timestamp": later, "power": None},  # power-less record is kept
    ]
    laps = [
        {"start_time": base, "total_elapsed_time": 60.4, "avg_power": 103, "max_power": 202},
        {"start_time": later, "total_elapsed_time": 60.7, "avg_power": 180, "max_power": 247},
    ]
    stats = fs.load_fit(conn, "G.fit", records, laps)
    assert stats["records"] == 3
    assert stats["laps"] == 2
    assert stats["power_records"] == 2

    rows = conn.execute(
        "SELECT power_w, cadence_rpm, speed_kmh, hr_bpm FROM fit_record ORDER BY rec_index"
    ).fetchall()
    assert rows[0]["power_w"] == 103 and rows[0]["cadence_rpm"] == 70
    assert rows[0]["speed_kmh"] == 28.8 and rows[0]["hr_bpm"] == 120
    assert rows[2]["power_w"] is None

    lap = conn.execute(
        "SELECT avg_power_w, elapsed_s, start_iso FROM fit_lap ORDER BY lap_index"
    ).fetchall()
    assert lap[0]["avg_power_w"] == 103 and abs(lap[0]["elapsed_s"] - 60.4) < 1e-6
    assert lap[0]["start_iso"] == "2026-06-21T07:59:48Z"

    cap = conn.execute(
        "SELECT protocol, n_records FROM capture WHERE filename='G.fit'").fetchone()
    assert cap["protocol"] == "fit" and cap["n_records"] == 3


def test_load_fit_reimport_replaces():
    conn = js.connect(":memory:")
    rec = [{"timestamp": datetime(2026, 6, 21, 8, 0, 0, tzinfo=_UTC), "power": 200}]
    fs.load_fit(conn, "R.fit", rec, [])
    fs.load_fit(conn, "R.fit", rec, [])  # idempotent
    n_cap = conn.execute("SELECT COUNT(*) n FROM capture WHERE filename='R.fit'").fetchone()["n"]
    assert n_cap == 1
    assert conn.execute("SELECT COUNT(*) n FROM fit_record").fetchone()["n"] == 1
