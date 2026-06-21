"""JSONL captures → a rebuildable SQLite *analysis index*.

The JSONL files in ``findings/captures/`` are the canonical, lossless record and
are **never** edited. This module builds a derived SQLite database *on top of*
them so desk-side work — especially time-aligned meter-to-meter reconciliation —
is a SQL query instead of a hand-rolled pass over log lines. The database is a
cache: throw it away and rebuild it from the JSONL at any time.

Design invariants
-----------------
* **JSONL is the source of truth.** Every imported line is stored verbatim in
  ``record.raw_json`` (lossless), so the flattened tables can be re-derived and
  nothing in a capture is lost even if its ``kind`` isn't one we flatten.
* **Idempotent.** A capture is keyed by ``filename``; every row is keyed by
  ``(capture_id, rec_index)`` (its 0-based line number). Re-importing an
  unchanged file is a no-op; a changed file (captures are immutable, so this is
  rare — a re-cut or a torn write) is replaced wholesale.
* **Decode is grounded in real captures.** The Indoor Bike Data and control-point
  decoders are *reused* from :mod:`sb20proxy.ble.ftms` (the validated codec), not
  re-implemented here. Where a capture already carries the capture script's own
  decode (e.g. CP ``data.result_name``) we prefer it; where it doesn't (the erg
  capture logs IBD as raw bytes only) we decode from ``raw_hex``.

What gets flattened (everything else still lands losslessly in ``record``):

==================  ==================================================  ===============
JSONL ``kind``      table                                               key columns
==================  ==================================================  ===============
``session_start``   ``capture`` (one row/file)                          protocol, …
``broadcast`` /     ``ant_broadcast``                                   source, power_w
``acknowledged``
``ble_notification``  ``ble_notification`` (IBD decoded) **or**         char, power_w
                      ``ble_control_point`` (if on the CP characteristic)
``ble_cp_write`` /    ``ble_control_point``                             direction, …
``ble_cp_indication``
``ble_advertisement`` ``ble_advertisement``                            address, rssi
==================  ==================================================  ===============

The ``power_sample`` view unifies ANT broadcasts and FTMS Indoor Bike Data into
one ``(stream, power_w, cadence_rpm)`` stream so :func:`reconcile` can align two
meters on a time bucket and report the per-bucket delta/ratio — the same pairing
``scripts/08_analyze_grid.py`` does by hand, expressed as SQL.
"""

from __future__ import annotations

import hashlib
import json
import sqlite3
from collections.abc import Iterable
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from statistics import median

from sb20proxy.ble.ftms import (
    CP_RESPONSE,
    CP_SET_TARGET_POWER,
    ControlPointResponse,
    decode_control_point,
    decode_indoor_bike_data,
)

SCHEMA_VERSION = 1

# Control-point characteristics: a `ble_notification` on one of these is a CP
# *response*, routed to `ble_control_point` (not `ble_notification`). Both the
# verbose ("fitness_machine_control_point") and short ("control_point") spellings
# appear across captures.
_CP_CHARS = {"control_point", "fitness_machine_control_point"}

# Char name normalisation — collapse the spelling drift seen across captures.
_CHAR_ALIASES = {
    "fitness_machine_control_point": "control_point",
}

_SCHEMA = """
CREATE TABLE IF NOT EXISTS capture (
    capture_id   INTEGER PRIMARY KEY,
    filename     TEXT    NOT NULL UNIQUE,
    sha256       TEXT    NOT NULL,
    protocol     TEXT,            -- ble / ant / ant+multi (from session_start)
    n_records    INTEGER NOT NULL,
    started_iso  TEXT,            -- iso_time of the first record
    imported_at  TEXT    NOT NULL,
    session_json TEXT             -- the raw session_start record, if any
);

CREATE TABLE IF NOT EXISTS record (
    capture_id  INTEGER NOT NULL REFERENCES capture(capture_id) ON DELETE CASCADE,
    rec_index   INTEGER NOT NULL,         -- 0-based line number in the file
    iso_time    TEXT,
    monotonic_s REAL,
    kind        TEXT,
    raw_json    TEXT    NOT NULL,         -- the verbatim JSONL line (lossless)
    PRIMARY KEY (capture_id, rec_index)
);
CREATE INDEX IF NOT EXISTS ix_record_kind ON record(capture_id, kind);

CREATE TABLE IF NOT EXISTS ble_notification (
    capture_id       INTEGER NOT NULL REFERENCES capture(capture_id) ON DELETE CASCADE,
    rec_index        INTEGER NOT NULL,
    monotonic_s      REAL,
    iso_time         TEXT,
    char             TEXT,               -- normalised characteristic name
    raw_hex          TEXT,
    flags            INTEGER,            -- IBD flags (NULL if not Indoor Bike Data)
    power_w          INTEGER,            -- instantaneous power, W (s16)
    cadence_rpm      REAL,
    speed_kmh        REAL,
    avg_power_w      INTEGER,
    total_distance_m INTEGER,
    decoded          INTEGER NOT NULL DEFAULT 0,   -- 1 = IBD decoded from raw_hex
    PRIMARY KEY (capture_id, rec_index)
);
CREATE INDEX IF NOT EXISTS ix_notif_char ON ble_notification(capture_id, char, monotonic_s);

CREATE TABLE IF NOT EXISTS ble_control_point (
    capture_id     INTEGER NOT NULL REFERENCES capture(capture_id) ON DELETE CASCADE,
    rec_index      INTEGER NOT NULL,
    monotonic_s    REAL,
    iso_time       TEXT,
    direction      TEXT,                 -- 'write' (host->machine) | 'response' | 'indication'
    char           TEXT,
    raw_hex        TEXT,
    note           TEXT,                 -- human note/op (e.g. set_target_power=225)
    opcode         INTEGER,              -- request opcode (write) / echoed req-op (response)
    is_response    INTEGER NOT NULL DEFAULT 0,
    result         INTEGER,
    result_name    TEXT,
    target_power_w INTEGER,              -- decoded Set Target Power watts, when applicable
    PRIMARY KEY (capture_id, rec_index)
);

CREATE TABLE IF NOT EXISTS ble_advertisement (
    capture_id        INTEGER NOT NULL REFERENCES capture(capture_id) ON DELETE CASCADE,
    rec_index         INTEGER NOT NULL,
    monotonic_s       REAL,
    iso_time          TEXT,
    address           TEXT,
    name              TEXT,
    rssi              INTEGER,
    service_uuids     TEXT,              -- JSON array text
    manufacturer_data TEXT,             -- JSON object text (company_id -> hex)
    seen_count        INTEGER,
    PRIMARY KEY (capture_id, rec_index)
);
CREATE INDEX IF NOT EXISTS ix_adv_addr ON ble_advertisement(capture_id, address);

CREATE TABLE IF NOT EXISTS ant_broadcast (
    capture_id         INTEGER NOT NULL REFERENCES capture(capture_id) ON DELETE CASCADE,
    rec_index          INTEGER NOT NULL,
    monotonic_s        REAL,
    iso_time           TEXT,
    source             TEXT,             -- channel label in multi captures, else NULL
    page               INTEGER,
    raw_hex            TEXT,
    power_w            INTEGER,          -- instantaneous_power_w
    cadence_rpm        INTEGER,          -- instantaneous_cadence_rpm
    accumulated_power  INTEGER,
    accumulated_torque INTEGER,
    event_count        INTEGER,
    ext_device_number  INTEGER,          -- the ANT device id the page came from
    PRIMARY KEY (capture_id, rec_index)
);
CREATE INDEX IF NOT EXISTS ix_ant_src ON ant_broadcast(capture_id, source, monotonic_s);

-- Unified power/cadence stream for reconciliation. `stream` is the meter
-- identity: the ANT channel label (or 'ant' for a single-source capture) and
-- 'ftms' for the SB20's Indoor Bike Data. Only rows that actually carry power
-- are surfaced.
CREATE VIEW IF NOT EXISTS power_sample AS
    SELECT capture_id, rec_index, monotonic_s, iso_time,
           COALESCE(source, 'ant') AS stream, 'ant' AS protocol,
           power_w, CAST(cadence_rpm AS REAL) AS cadence_rpm
      FROM ant_broadcast
     WHERE power_w IS NOT NULL
    UNION ALL
    SELECT capture_id, rec_index, monotonic_s, iso_time,
           'ftms' AS stream, 'ftms' AS protocol,
           power_w, cadence_rpm
      FROM ble_notification
     WHERE char = 'indoor_bike_data' AND power_w IS NOT NULL;
"""


# --------------------------------------------------------------------------- #
# connection / schema
# --------------------------------------------------------------------------- #
def connect(db_path: str | Path = ":memory:") -> sqlite3.Connection:
    """Open (creating if needed) the analysis DB with the schema applied."""
    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    create_schema(conn)
    return conn


def create_schema(conn: sqlite3.Connection) -> None:
    conn.executescript(_SCHEMA)
    # The annotation layer's table (committed-text sidecars -> derived table) is
    # created alongside the index so a fresh DB can JOIN annotations against
    # `power_sample`. Its logic lives in the sibling `annotations` module; the
    # import is one-directional (annotations never imports jsonl_sqlite).
    from sb20proxy.analysis.annotations import ANNOTATION_SCHEMA

    conn.executescript(ANNOTATION_SCHEMA)
    conn.execute(f"PRAGMA user_version = {SCHEMA_VERSION}")
    conn.commit()


# --------------------------------------------------------------------------- #
# import
# --------------------------------------------------------------------------- #
@dataclass
class ImportStats:
    """What one :func:`import_capture` call did."""

    filename: str
    capture_id: int | None = None
    n_records: int = 0
    skipped: bool = False          # already imported, unchanged
    replaced: bool = False         # existing rows were dropped and re-imported
    n_unparsed: int = 0            # torn/non-JSON lines kept as kind='_unparsed'


def import_capture(
    conn: sqlite3.Connection, path: str | Path, *, replace: bool = False
) -> ImportStats:
    """Import one JSONL capture. Idempotent: an unchanged file is skipped.

    The file is keyed by name; if its content hash differs from a prior import
    (or ``replace`` is set) the previous rows are dropped and it is re-imported.
    """
    path = Path(path)
    raw = path.read_bytes()
    sha = hashlib.sha256(raw).hexdigest()
    filename = path.name

    prior = conn.execute(
        "SELECT capture_id, sha256 FROM capture WHERE filename = ?", (filename,)
    ).fetchone()
    if prior is not None and prior["sha256"] == sha and not replace:
        return ImportStats(filename=filename, capture_id=prior["capture_id"], skipped=True)

    # Parse first so we can fill the capture header (protocol/start/count).
    records: list[tuple[str, dict | None]] = []
    session_json: str | None = None
    protocol: str | None = None
    started_iso: str | None = None
    n_unparsed = 0
    for line in raw.decode("utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rec = json.loads(line)
        except json.JSONDecodeError:
            records.append((line, None))   # keep torn lines losslessly
            n_unparsed += 1
            continue
        if started_iso is None and rec.get("iso_time"):
            started_iso = rec["iso_time"]
        if rec.get("kind") == "session_start" and session_json is None:
            session_json = line
            protocol = rec.get("protocol") or _infer_protocol(rec)
        records.append((line, rec))

    with conn:  # one transaction; commits on success, rolls back on error
        if prior is not None:
            conn.execute("DELETE FROM capture WHERE capture_id = ?", (prior["capture_id"],))
        cur = conn.execute(
            """INSERT INTO capture
               (filename, sha256, protocol, n_records, started_iso, imported_at, session_json)
               VALUES (?, ?, ?, ?, ?, ?, ?)""",
            (filename, sha, protocol, len(records), started_iso, _now_iso(), session_json),
        )
        capture_id = int(cur.lastrowid)
        for idx, (line, rec) in enumerate(records):
            kind = rec.get("kind") if rec else "_unparsed"
            iso = rec.get("iso_time") if rec else None
            mono = rec.get("monotonic_s") if rec else None
            conn.execute(
                "INSERT INTO record (capture_id, rec_index, iso_time, monotonic_s, kind, raw_json)"
                " VALUES (?, ?, ?, ?, ?, ?)",
                (capture_id, idx, iso, mono, kind, line),
            )
            if rec is not None:
                _route(conn, capture_id, idx, rec)

    return ImportStats(
        filename=filename,
        capture_id=capture_id,
        n_records=len(records),
        replaced=prior is not None,
        n_unparsed=n_unparsed,
    )


def import_dir(
    conn: sqlite3.Connection, captures_dir: str | Path, *, replace: bool = False
) -> list[ImportStats]:
    """Import every ``*.jsonl`` in a directory (sorted). Idempotent."""
    out = []
    for path in sorted(Path(captures_dir).glob("*.jsonl")):
        out.append(import_capture(conn, path, replace=replace))
    return out


def _infer_protocol(session: dict) -> str | None:
    if "sources" in session:
        return "ant+multi"
    if "device_id" in session:
        return "ant"
    return session.get("protocol")


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


# --------------------------------------------------------------------------- #
# routing — one decoded row per flattened kind
# --------------------------------------------------------------------------- #
def _route(conn: sqlite3.Connection, cid: int, idx: int, rec: dict) -> None:
    kind = rec.get("kind")
    if kind == "ble_notification":
        _route_notification(conn, cid, idx, rec)
    elif kind == "ble_cp_write":
        _insert_cp(conn, cid, idx, rec, direction="write")
    elif kind == "ble_cp_indication":
        _insert_cp(conn, cid, idx, rec, direction="indication")
    elif kind == "ble_advertisement":
        _route_advertisement(conn, cid, idx, rec)
    elif kind in ("broadcast", "acknowledged"):
        _route_ant(conn, cid, idx, rec)


def _norm_char(char: str | None) -> str | None:
    if char is None:
        return None
    return _CHAR_ALIASES.get(char, char)


def _route_notification(conn: sqlite3.Connection, cid: int, idx: int, rec: dict) -> None:
    raw_char = rec.get("char")
    data = rec.get("data") if isinstance(rec.get("data"), dict) else {}
    raw_hex = rec.get("raw_hex") or data.get("raw_hex")

    # A notification on the control-point characteristic is a CP response.
    if raw_char in _CP_CHARS:
        _insert_cp(conn, cid, idx, rec, direction="response", raw_hex=raw_hex, data=data)
        return

    char = _norm_char(raw_char)
    flags = power = cadence = speed = avg_power = distance = None
    decoded = 0
    if char == "indoor_bike_data" and raw_hex:
        try:
            ibd = decode_indoor_bike_data(bytes.fromhex(raw_hex))
        except ValueError:  # truncated / malformed frame
            decoded = 0
        else:
            flags = ibd.flags
            power = ibd.power_w
            cadence = ibd.cadence_rpm
            speed = ibd.speed_kmh
            avg_power = ibd.average_power
            distance = ibd.total_distance
            decoded = 1

    conn.execute(
        """INSERT INTO ble_notification
           (capture_id, rec_index, monotonic_s, iso_time, char, raw_hex, flags,
            power_w, cadence_rpm, speed_kmh, avg_power_w, total_distance_m, decoded)
           VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
        (cid, idx, rec.get("monotonic_s"), rec.get("iso_time"), char, raw_hex, flags,
         power, cadence, speed, avg_power, distance, decoded),
    )


def _insert_cp(
    conn: sqlite3.Connection,
    cid: int,
    idx: int,
    rec: dict,
    *,
    direction: str,
    raw_hex: str | None = None,
    data: dict | None = None,
) -> None:
    """Insert a control-point row (write / response / indication).

    Decode hybrid: prefer fields the capture already decoded into ``data``; only
    fall back to the FTMS codec for the *flat* CP responses (raw_hex only, no
    ``data``) seen in the hw-loop capture. CPS calibration frames (``op``/``opcode``,
    response code 0x20) keep their raw bytes + the capture's own ``result_name``.
    """
    if data is None:
        rec_data = rec.get("data")
        data = rec_data if isinstance(rec_data, dict) else {}
    raw_hex = raw_hex or rec.get("raw_hex") or data.get("raw_hex")
    raw = bytes.fromhex(raw_hex) if raw_hex else b""
    char = _norm_char(rec.get("char"))
    note = rec.get("note") or rec.get("op")
    is_response = 0 if direction == "write" else 1

    opcode = rec.get("opcode")
    result = data.get("result")
    result_name = data.get("result_name")
    target_power_w = None
    if data.get("request_opcode") is not None:
        opcode = data["request_opcode"]

    if direction == "write":
        if opcode is None and raw:
            opcode = raw[0]
        if opcode == CP_SET_TARGET_POWER and len(raw) >= 3:
            target_power_w = int.from_bytes(raw[1:3], "little", signed=True)
    else:
        # Flat FTMS response (no pre-decoded `data`): decode the 0x80 indication.
        if not data and raw:
            dec = decode_control_point(raw)
            if isinstance(dec, ControlPointResponse):
                opcode, result = dec.request_opcode, dec.result
                result_name = dec.result_name
        # Set-Target-Power response echoes the watts in its params.
        if opcode == CP_SET_TARGET_POWER and len(raw) >= 5 and raw[0] == CP_RESPONSE:
            target_power_w = int.from_bytes(raw[3:5], "little", signed=True)

    conn.execute(
        """INSERT INTO ble_control_point
           (capture_id, rec_index, monotonic_s, iso_time, direction, char, raw_hex,
            note, opcode, is_response, result, result_name, target_power_w)
           VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
        (cid, idx, rec.get("monotonic_s"), rec.get("iso_time"), direction, char, raw_hex,
         note, opcode, is_response, result, result_name, target_power_w),
    )


def _route_advertisement(conn: sqlite3.Connection, cid: int, idx: int, rec: dict) -> None:
    uuids = rec.get("service_uuids")
    mfg = rec.get("manufacturer_data")
    conn.execute(
        """INSERT INTO ble_advertisement
           (capture_id, rec_index, monotonic_s, iso_time, address, name, rssi,
            service_uuids, manufacturer_data, seen_count)
           VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
        (cid, idx, rec.get("monotonic_s"), rec.get("iso_time"), rec.get("address"),
         rec.get("name"), rec.get("rssi"),
         json.dumps(uuids) if uuids is not None else None,
         json.dumps(mfg) if mfg is not None else None,
         rec.get("seen_count")),
    )


def _route_ant(conn: sqlite3.Connection, cid: int, idx: int, rec: dict) -> None:
    data = rec.get("data") if isinstance(rec.get("data"), dict) else {}
    conn.execute(
        """INSERT INTO ant_broadcast
           (capture_id, rec_index, monotonic_s, iso_time, source, page, raw_hex,
            power_w, cadence_rpm, accumulated_power, accumulated_torque,
            event_count, ext_device_number)
           VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
        (cid, idx, rec.get("monotonic_s"), rec.get("iso_time"), rec.get("source"),
         data.get("page"), data.get("raw_hex"),
         data.get("instantaneous_power_w"), data.get("instantaneous_cadence_rpm"),
         data.get("accumulated_power"), data.get("accumulated_torque"),
         data.get("event_count"), data.get("ext_device_number")),
    )


# --------------------------------------------------------------------------- #
# reconciliation
# --------------------------------------------------------------------------- #
@dataclass
class ReconRow:
    """Two meters' averaged power in one time bucket."""

    bucket: int
    power_a: float
    power_b: float
    cadence_a: float | None
    cadence_b: float | None
    n_a: int
    n_b: int

    @property
    def delta(self) -> float:
        """power_a − power_b (watts)."""
        return self.power_a - self.power_b

    @property
    def ratio(self) -> float | None:
        """power_a / power_b (the calibration factor)."""
        return self.power_a / self.power_b if self.power_b else None


def reconcile(
    conn: sqlite3.Connection,
    stream_a: str,
    stream_b: str,
    *,
    capture: str | None = None,
    capture_a: str | None = None,
    capture_b: str | None = None,
    basis: str = "mono",
    bucket_s: float = 1.0,
    min_power: float = 1.0,
    max_power: float = 2500.0,
    min_cadence: float = 0.0,
) -> list[ReconRow]:
    """Time-align two power streams and return per-bucket pairs.

    ``basis="mono"`` buckets on ``monotonic_s`` — use for two streams in the *same*
    capture (the §D ANT+ grid: stages vs assioma; or stages vs bike_fec). ``basis="iso"``
    buckets on the wall-clock ``iso_time`` second — use to align two *separate*
    captures recorded at the same time (the SB20 FTMS feed vs an independent ANT+
    Assioma feed). ``min_power``/``max_power``/``min_cadence`` drop idle and
    decode-artefact samples (mirrors ``08_analyze_grid.py``'s active-sample gate).

    Pass ``capture`` to pin both streams to one file, or ``capture_a``/``capture_b``
    to pin each (required when ``basis="iso"`` across two files).
    """
    if basis not in ("mono", "iso"):
        raise ValueError(f"basis must be 'mono' or 'iso', not {basis!r}")
    cap_a = capture_a or capture
    cap_b = capture_b or capture
    if cap_a is None or cap_b is None:
        raise ValueError("provide capture=... (both) or capture_a=/capture_b=")
    if basis == "mono" and cap_a != cap_b:
        raise ValueError("basis='mono' compares within one capture; use basis='iso' across files")

    sql_a, params_a = _recon_side(
        stream_a, cap_a, basis, bucket_s, min_power, max_power, min_cadence)
    sql_b, params_b = _recon_side(
        stream_b, cap_b, basis, bucket_s, min_power, max_power, min_cadence)
    query = (
        f"WITH a AS ({sql_a}), b AS ({sql_b}) "
        "SELECT a.b AS bucket, a.p AS pa, b.p AS pb, a.c AS ca, b.c AS cb, a.n AS na, b.n AS nb "
        "FROM a JOIN b ON a.b = b.b ORDER BY a.b"
    )
    rows = conn.execute(query, (*params_a, *params_b)).fetchall()
    return [
        ReconRow(bucket=r["bucket"], power_a=r["pa"], power_b=r["pb"],
                 cadence_a=r["ca"], cadence_b=r["cb"], n_a=r["na"], n_b=r["nb"])
        for r in rows
    ]


def _recon_side(
    stream: str, capture: str, basis: str, bucket_s: float,
    min_power: float, max_power: float, min_cadence: float,
) -> tuple[str, list]:
    """Build one bucketed-average subquery + its bound params (textual order)."""
    params: list = []
    if basis == "mono":
        bucket_expr = "CAST(monotonic_s / ? AS INTEGER)"
        params.append(float(bucket_s))
    else:  # iso: integer epoch-second buckets
        bucket_expr = "(CAST(strftime('%s', iso_time) AS INTEGER) / ?)"
        params.append(max(1, int(bucket_s)))

    where = [
        "stream = ?",
        "capture_id = (SELECT capture_id FROM capture WHERE filename = ?)",
        "power_w BETWEEN ? AND ?",
    ]
    params += [stream, capture, min_power, max_power]
    if min_cadence > 0:
        where.append("cadence_rpm >= ?")
        params.append(min_cadence)

    sql = (
        f"SELECT {bucket_expr} AS b, AVG(power_w) AS p, AVG(cadence_rpm) AS c, COUNT(*) AS n "
        f"FROM power_sample WHERE {' AND '.join(where)} GROUP BY b"
    )
    return sql, params


def reconcile_summary(rows: Iterable[ReconRow]) -> dict:
    """Aggregate :func:`reconcile` rows: paired-bucket count, median ratio, deltas."""
    rows = list(rows)
    ratios = [r.ratio for r in rows if r.ratio is not None]
    deltas = [r.delta for r in rows]
    return {
        "n_buckets": len(rows),
        "median_ratio": median(ratios) if ratios else None,
        "mean_delta_w": (sum(deltas) / len(deltas)) if deltas else None,
        "median_delta_w": median(deltas) if deltas else None,
    }
