#!/usr/bin/env python3
"""Import an nRF Sniffer ``.pcap`` (``LINKTYPE_NORDIC_BLE``) into the SQLite index.

The capture is the canonical raw record (committed to ``findings/captures/``); this
module derives a queryable index from it — never the other way round.

We shell out to **tshark** (Wireshark's CLI), which natively dissects the Nordic-BLE
encapsulation down to BLE LL / L2CAP / ATT — far more robust than hand-rolling a
Nordic-BLE parser. The ATT operations land in three places:

* ``pcap_att``          — *every* ATT op (raw + lightly decoded): the knowledge-base
  spine. Query any characteristic's traffic, in time order, without re-running tshark.
* ``ble_notification``  — decoded **CPS Measurement** (``0x2A63``) and **FTMS Indoor
  Bike Data** (``0x2AD2``) power, so the existing ``power_sample`` view + ``reconcile``
  pick the pcap streams up for free.
* ``ble_control_point`` — control writes: **FTMS CP** (``0x2AD9``, Set-Target-Power)
  and the **Stages-proprietary** ``0c46be`` erg writes (``02 00 <u16-LE> 00 00``).

Seam (so the logic is hermetically testable — CI has no tshark):
  ``tshark_att_rows`` is the only I/O; ``parse_att_row`` and the ``decode_*`` /
  ``resolve_char`` helpers are pure; ``load_att_rows`` takes already-parsed rows and
  an in-memory connection. The unit tests drive the pure path with synthetic rows.
"""

from __future__ import annotations

import hashlib
import shutil
import sqlite3
import subprocess
from collections.abc import Iterable, Iterator
from datetime import datetime, timezone
from pathlib import Path

from sb20proxy.ble.cps import decode_cps_measurement
from sb20proxy.ble.ftms import decode_indoor_bike_data

# --- ATT opcodes ----------------------------------------------------------- #
ATT_WRITE_REQ = 0x12
ATT_WRITE_CMD = 0x52
ATT_NOTIFY = 0x1B
ATT_INDICATE = 0x1D
ATT_FIND_INFO_RSP = 0x05        # discovery: handle/uuid pairs (descriptors, CCCDs)
ATT_READ_BY_TYPE_RSP = 0x09     # discovery: characteristic declarations
_WRITE_OPS = frozenset({ATT_WRITE_REQ, ATT_WRITE_CMD})
_NOTIFY_OPS = frozenset({ATT_NOTIFY, ATT_INDICATE})

ATT_OPCODE_NAMES = {
    0x01: "error_response", 0x02: "exchange_mtu_req", 0x03: "exchange_mtu_rsp",
    0x04: "find_info_req", 0x05: "find_info_rsp", 0x06: "find_by_type_req",
    0x07: "find_by_type_rsp", 0x08: "read_by_type_req", 0x09: "read_by_type_rsp",
    0x0A: "read_req", 0x0B: "read_rsp", 0x0C: "read_blob_req", 0x0D: "read_blob_rsp",
    0x10: "read_by_group_req", 0x11: "read_by_group_rsp",
    0x12: "write_req", 0x13: "write_rsp", 0x52: "write_cmd",
    0x16: "prepare_write_req", 0x17: "prepare_write_rsp",
    0x18: "execute_write_req", 0x19: "execute_write_rsp",
    0x1B: "notification", 0x1D: "indication", 0x1E: "confirmation",
}

# 16-bit UUID -> normalised characteristic name (the names ble_notification expects)
UUID16_CHARS = {
    "2a63": "cycling_power_measurement",
    "2a65": "cycling_power_feature",
    "2a5b": "csc_measurement",
    "2ad2": "indoor_bike_data",
    "2ad9": "fitness_machine_control_point",
    "2ada": "fitness_machine_status",
    "2ad3": "training_status",
    "2acc": "fitness_machine_feature",
    "2ad8": "supported_power_range",
    "2902": "cccd",
}
_CP_CHARS = frozenset({"fitness_machine_control_point", "cycling_power_control_point"})
# the Stages-proprietary 0c46be... base, in both wire orders (tshark prints either)
_STAGES_MARKERS = ("c6eae1a2f4e5", "e5f4a2e1eac6")
# GATT declaration attribute types (service / include / characteristic). In a
# read_by_type_rsp these are the *declaration* attributes interleaved with the real
# char UUIDs — skip them when mapping value handles to characteristics.
_GATT_DECL_UUIDS = frozenset({"2800", "2801", "2802", "2803"})

# tshark -T fields order (parse_att_row depends on it)
TSHARK_FIELDS = (
    "frame.number", "frame.time_epoch", "btle.access_address", "btatt.opcode",
    "btatt.handle", "btatt.uuid16", "btatt.uuid128", "btatt.value", "btsmp.opcode",
)

_PCAP_SCHEMA = """
CREATE TABLE IF NOT EXISTS pcap_att (
    capture_id  INTEGER NOT NULL REFERENCES capture(capture_id) ON DELETE CASCADE,
    rec_index   INTEGER NOT NULL,
    frame_num   INTEGER,
    t_epoch     REAL,
    monotonic_s REAL,
    iso_time    TEXT,
    access_addr TEXT,                 -- BLE access address = the connection (crank / SB20 / app)
    direction   TEXT,                 -- write | notify | response | request
    opcode      INTEGER,
    opcode_name TEXT,
    handle      INTEGER,
    char        TEXT,                 -- resolved characteristic (name or uuid or stages_prop)
    value_hex   TEXT,
    decoded     TEXT,                 -- short human note (power=NNN, target=NNN, ctrl=NNN, ...)
    PRIMARY KEY (capture_id, rec_index)
);
CREATE INDEX IF NOT EXISTS ix_pcap_att_char ON pcap_att(capture_id, char, monotonic_s);
"""


# --------------------------------------------------------------------------- #
# pure helpers (unit-tested)
# --------------------------------------------------------------------------- #
def _clean_hex(v: str | None) -> str:
    """tshark prints byte fields as ``aa:bb:cc``; normalise to plain lowercase hex."""
    if not v:
        return ""
    return v.replace(":", "").replace(" ", "").lower()


def _int(x: str, base: int = 10) -> int | None:
    x = (x or "").strip()
    if not x:
        return None
    x = x.split(",")[0]  # multi-value field -> first occurrence
    try:
        return int(x, base)
    except ValueError:
        return None


def parse_att_row(line: str) -> dict | None:
    """Parse one ``tshark -T fields`` TSV line into a dict (None for blanks)."""
    if not line or not line.strip():
        return None
    parts = line.split("\t")
    parts += [""] * (len(TSHARK_FIELDS) - len(parts))
    frame, t_epoch, access, opcode, handle, uuid16, uuid128, value, smp = parts[:9]
    return {
        "frame": _int(frame),
        "t_epoch": float(t_epoch) if t_epoch.strip() else None,
        "access_addr": (access.strip().split(",")[0].lower() or None),
        "opcode": _int(opcode, 16),
        "handle": _int(handle, 16),
        "uuid16": (uuid16.strip().lower().split(",")[0] or None),
        "uuid128": (_clean_hex(uuid128).split(",")[0] or None),
        "value": _clean_hex(value),
        "smp_opcode": _int(smp, 16),
    }


def _norm_uuid16(uuid16: str | None) -> str | None:
    """Normalise a 16-bit UUID to the bare lowercase form ``UUID16_CHARS`` is keyed by.

    tshark prints ``btatt.uuid16`` as ``0x2ad2`` (and, under ``-E occurrence=a``,
    comma-joins every occurrence in a packet); the lookup table is keyed ``2ad2``.
    Strip the ``0x`` prefix and take the first value so the lookup hits — without
    this the 16-bit FTMS/CPS chars never resolve (they fall through to ``uuid16_…``).
    """
    if not uuid16:
        return None
    u = uuid16.strip().lower().split(",")[0]
    if u.startswith("0x"):
        u = u[2:]
    return u or None


def resolve_char(uuid16: str | None, uuid128: str | None,
                 handle: int | None, handle_map: dict[int, str]) -> str | None:
    """Map a UUID (or, failing that, a learned handle) to a characteristic label."""
    u16 = _norm_uuid16(uuid16)
    if u16:
        return UUID16_CHARS.get(u16, f"uuid16_{u16}")
    if uuid128:
        if any(m in uuid128 for m in _STAGES_MARKERS):
            # distinguishing byte is the 4th from the 0c46beXX end (wire order varies);
            # label by the whole short form so different prop chars stay distinct.
            return f"stages_prop_{uuid128[:4]}_{uuid128[-4:]}"
        return f"uuid128_{uuid128[:8]}"
    if handle is not None and handle in handle_map:
        return handle_map[handle]
    return f"handle_{handle:#06x}" if handle is not None else None


def direction_of(opcode: int | None) -> str | None:
    if opcode in _WRITE_OPS:
        return "write"
    if opcode in _NOTIFY_OPS:
        return "notify"
    if opcode in (0x0A, 0x08, 0x10, 0x04, 0x06, 0x12, 0x16, 0x18):
        return "request"
    return "response"


def decode_stages_control(value_hex: str) -> int | None:
    """Stages app proprietary erg write: ``02 00 <u16-LE> 00 00`` -> the streamed value."""
    try:
        b = bytes.fromhex(value_hex)
    except ValueError:
        return None
    if len(b) >= 4 and b[0] == 0x02 and b[1] == 0x00:
        return int.from_bytes(b[2:4], "little")
    return None


def _iso(t_epoch: float | None) -> str | None:
    if t_epoch is None:
        return None
    return datetime.fromtimestamp(t_epoch, tz=timezone.utc).isoformat().replace("+00:00", "Z")


# --------------------------------------------------------------------------- #
# tshark I/O seam
# --------------------------------------------------------------------------- #
def tshark_bin(explicit: str | None = None) -> str:
    if explicit and Path(explicit).exists():
        return explicit
    found = shutil.which("tshark")
    if found:
        return found
    for cand in (r"C:\Program Files\Wireshark\tshark.exe",
                 r"C:\Program Files (x86)\Wireshark\tshark.exe"):
        if Path(cand).exists():
            return cand
    raise FileNotFoundError(
        "tshark not found — install Wireshark (winget install WiresharkFoundation.Wireshark) "
        "or pass tshark=<path>."
    )


def tshark_att_rows(pcap_path: str | Path, tshark: str | None = None) -> Iterator[str]:
    """Run tshark over the pcap; yield TSV lines for ATT/SMP frames (the only I/O)."""
    binp = tshark_bin(tshark)
    cmd = [binp, "-r", str(pcap_path), "-Y", "btatt || btsmp",
           "-T", "fields", "-E", "separator=\t", "-E", "occurrence=f"]
    for f in TSHARK_FIELDS:
        cmd += ["-e", f]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=True)
    yield from proc.stdout.splitlines()


def discovery_handle_map(pcap_path: str | Path, tshark: str | None = None) -> dict[int, str]:
    """Second tshark pass: learn value-handle -> char from GATT discovery responses.

    The 128-bit proprietary chars don't carry their UUID on data ops, so we map them
    here (and 16-bit chars too, as a fallback for ops tshark can't back-fill). We
    emit the opcode so ``parse_handle_map`` can tell the two discovery shapes apart:
    read_by_type_rsp (0x09) char declarations vs find_information_rsp (0x05) pairs.
    """
    binp = tshark_bin(tshark)
    cmd = [binp, "-r", str(pcap_path), "-Y", "btatt.opcode==0x09 || btatt.opcode==0x05",
           "-T", "fields", "-E", "separator=\t", "-E", "occurrence=a", "-E", "aggregator=,",
           "-e", "btatt.opcode", "-e", "btatt.handle", "-e", "btatt.uuid16", "-e", "btatt.uuid128"]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return parse_handle_map(proc.stdout.splitlines())


def _parse_handle_list(handles_s: str) -> list[int] | None:
    """Parse a comma-joined ``0x…`` handle list; None if any entry is malformed."""
    out: list[int] = []
    for h in handles_s.split(","):
        h = h.strip()
        if not h:
            continue
        try:
            out.append(int(h, 16))
        except ValueError:
            return None
    return out


def _record(hmap: dict[int, str], handle: int, char: str | None) -> None:
    """Key ``handle -> char`` unless the char is unknown (a bare ``handle_…`` label)."""
    if char and not char.startswith("handle_"):
        hmap[handle] = char


def _map_char_declarations(hmap: dict[int, str], handles: list[int],
                           uuid16s: list[str], uuid128s: list[str]) -> None:
    """Map the value handles of a read_by_type_rsp's characteristic declarations.

    A single packet can carry several chars (16-bit UUIDs pack tight — the SB20's
    whole FTMS service arrives in one frame). tshark emits the handles as
    ``decl, value, decl, value, …`` so the value handles (what notifications/writes
    use) are ``handles[1::2]``. A Read-By-Type response holds only same-length
    elements, so a packet is either all-16-bit-UUID chars (UUID in ``uuid16``, the
    ``0x2803`` declaration type interleaved and filtered) or all-128-bit (UUID in
    ``uuid128``, one per char).
    """
    # strict=False: a malformed/truncated discovery packet drops its extra entries
    # rather than aborting the whole import.
    value_handles = handles[1::2]
    if uuid128s:
        for vh, u128 in zip(value_handles, uuid128s, strict=False):
            _record(hmap, vh, resolve_char(None, u128, None, {}))
    else:
        char_uuids = [u for u in uuid16s if u not in _GATT_DECL_UUIDS]
        for vh, u16 in zip(value_handles, char_uuids, strict=False):
            _record(hmap, vh, resolve_char(u16, None, None, {}))


def _map_find_info(hmap: dict[int, str], handles: list[int],
                   uuid16s: list[str], uuid128s: list[str]) -> None:
    """Map find_information_rsp ``handle, uuid`` pairs (1:1; descriptors / CCCDs)."""
    if uuid128s:
        for h, u128 in zip(handles, uuid128s, strict=False):
            _record(hmap, h, resolve_char(None, u128, None, {}))
    else:
        for h, u16 in zip(handles, uuid16s, strict=False):
            _record(hmap, h, resolve_char(u16, None, None, {}))


def parse_handle_map(lines: Iterable[str]) -> dict[int, str]:
    """Pure: build ``{value_handle -> char}`` from discovery-response TSV lines.

    Columns (``-E occurrence=a -E aggregator=,``): ``opcode \\t handles \\t uuid16
    \\t uuid128``, each field the comma-joined list of every occurrence in that
    packet. read_by_type_rsp (0x09) packets carry one *or more* characteristic
    declarations; find_information_rsp (0x05) carries ``handle, uuid`` pairs.
    """
    hmap: dict[int, str] = {}
    for line in lines:
        if not line.strip():
            continue
        opcode_s, handles_s, uuid16_s, uuid128_s = (line.split("\t") + ["", "", "", ""])[:4]
        opcode = _int(opcode_s.split(",")[0], 16)
        handles = _parse_handle_list(handles_s)
        if not handles:
            continue
        uuid16s = [u for u in (_norm_uuid16(x) for x in uuid16_s.split(",")) if u]
        uuid128s = [h for h in (_clean_hex(x) for x in uuid128_s.split(",")) if h]
        if opcode == ATT_FIND_INFO_RSP:
            _map_find_info(hmap, handles, uuid16s, uuid128s)
        elif opcode == ATT_READ_BY_TYPE_RSP:
            _map_char_declarations(hmap, handles, uuid16s, uuid128s)
    return hmap


# --------------------------------------------------------------------------- #
# DB load (testable with synthetic rows + an in-memory connection)
# --------------------------------------------------------------------------- #
def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def load_att_rows(conn: sqlite3.Connection, filename: str, rows: Iterable[dict], *,
                  sha256: str = "", device_prefix: str | None = None,
                  handle_map: dict[int, str] | None = None) -> dict:
    """Insert parsed ATT rows under a new ``capture`` row. Returns a small stats dict.

    ``handle_map`` (value-handle -> char, from ``discovery_handle_map``) resolves the
    proprietary 128-bit chars, whose UUID isn't carried on data ops. ``device_prefix``
    is prepended to the access-address stream label in ``ble_notification.device``.
    """
    conn.executescript(_PCAP_SCHEMA)
    handle_map = dict(handle_map or {})
    rows = [r for r in rows if r]
    epochs = [r["t_epoch"] for r in rows if r.get("t_epoch") is not None]
    t0 = min(epochs) if epochs else None
    started_iso = _iso(t0)

    cur = conn.execute("SELECT capture_id FROM capture WHERE filename = ?", (filename,))
    existing = cur.fetchone()
    if existing:
        conn.execute("DELETE FROM capture WHERE filename = ?", (filename,))  # cascades

    cur = conn.execute(
        """INSERT INTO capture (filename, sha256, protocol, n_records, started_iso, imported_at)
           VALUES (?, ?, 'ble-pcap', ?, ?, ?)""",
        (filename, sha256, len(rows), started_iso,
         datetime.now(tz=timezone.utc).isoformat().replace("+00:00", "Z")),
    )
    cid = cur.lastrowid

    # augment the discovery map with any inline UUIDs the data ops happen to carry
    for r in rows:
        ch = resolve_char(r.get("uuid16"), r.get("uuid128"), None, {})
        if (ch and r.get("handle") is not None
                and not ch.startswith("handle_") and not ch.startswith("uuid")):
            handle_map.setdefault(r["handle"], ch)

    stats = {"att": 0, "smp": 0, "cps": 0, "ibd": 0, "ftms_cp": 0, "stages_ctrl": 0}
    for idx, r in enumerate(rows):
        if r.get("smp_opcode") is not None:
            stats["smp"] += 1
        if r.get("opcode") is None:
            continue
        stats["att"] += 1
        char = resolve_char(r.get("uuid16"), r.get("uuid128"), r.get("handle"), handle_map)
        opcode = r["opcode"]
        direction = direction_of(opcode)
        mono = (r["t_epoch"] - t0) if (r.get("t_epoch") is not None and t0 is not None) else None
        iso_time = _iso(r.get("t_epoch"))
        value = r.get("value") or ""
        device = None
        if r.get("access_addr"):
            aa = r["access_addr"]
            device = aa if not device_prefix else f"{device_prefix}:{aa}"
        note = None

        # decode power notifications into ble_notification (feeds power_sample)
        if opcode in _NOTIFY_OPS and char == "cycling_power_measurement" and value:
            try:
                cpm = decode_cps_measurement(bytes.fromhex(value))
            except ValueError:
                pass
            else:
                note = f"power={cpm.power_w}"
                stats["cps"] += 1
                conn.execute(
                    """INSERT INTO ble_notification
                       (capture_id, rec_index, monotonic_s, iso_time, device, char, raw_hex,
                        flags, power_w, cadence_rpm, speed_kmh, avg_power_w,
                        total_distance_m, decoded)
                       VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,1)""",
                    (cid, idx, mono, iso_time, device or "cps", "cycling_power_measurement",
                     value, cpm.flags, cpm.power_w, None, None, None, None),
                )
        elif opcode in _NOTIFY_OPS and char == "indoor_bike_data" and value:
            try:
                ibd = decode_indoor_bike_data(bytes.fromhex(value))
            except ValueError:
                pass
            else:
                note = f"power={ibd.power_w} cad={ibd.cadence_rpm}"
                stats["ibd"] += 1
                conn.execute(
                    """INSERT INTO ble_notification
                       (capture_id, rec_index, monotonic_s, iso_time, device, char, raw_hex,
                        flags, power_w, cadence_rpm, speed_kmh, avg_power_w,
                        total_distance_m, decoded)
                       VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,1)""",
                    (cid, idx, mono, iso_time, device or "ftms", "indoor_bike_data",
                     value, ibd.flags, ibd.power_w, ibd.cadence_rpm, ibd.speed_kmh,
                     ibd.average_power, ibd.total_distance),
                )
        # control writes -> ble_control_point
        elif direction == "write" and char == "fitness_machine_control_point" and value:
            b = bytes.fromhex(value) if value else b""
            target = None
            if b and b[0] == 0x05 and len(b) >= 3:
                target = int.from_bytes(b[1:3], "little", signed=True)
                note = f"set_target_power={target}"
            stats["ftms_cp"] += 1
            conn.execute(
                """INSERT INTO ble_control_point
                   (capture_id, rec_index, monotonic_s, iso_time, direction, char, raw_hex,
                    note, opcode, is_response, result, result_name, target_power_w)
                   VALUES (?,?,?,?,?,?,?,?,?,0,?,?,?)""",
                (cid, idx, mono, iso_time, "write", "fitness_machine_control_point",
                 value, note, b[0] if b else None, None, None, target),
            )
        elif direction == "write" and value and (
                (char and char.startswith("stages_prop"))
                or decode_stages_control(value) is not None):
            ctrl = decode_stages_control(value)
            if ctrl is not None:
                note = f"stages_ctrl={ctrl}"
                stats["stages_ctrl"] += 1
            if not (char and char.startswith("stages_prop")):
                char = "stages_prop_ctrl"  # matched by value pattern, not the handle map
            conn.execute(
                """INSERT INTO ble_control_point
                   (capture_id, rec_index, monotonic_s, iso_time, direction, char, raw_hex,
                    note, opcode, is_response, result, result_name, target_power_w)
                   VALUES (?,?,?,?,?,?,?,?,?,0,?,?,?)""",
                (cid, idx, mono, iso_time, "write", char, value, note,
                 ctrl if ctrl is not None else None, None, None, None),
            )

        conn.execute(
            """INSERT INTO pcap_att
               (capture_id, rec_index, frame_num, t_epoch, monotonic_s, iso_time, access_addr,
                direction, opcode, opcode_name, handle, char, value_hex, decoded)
               VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
            (cid, idx, r.get("frame"), r.get("t_epoch"), mono, iso_time, r.get("access_addr"),
             direction, opcode, ATT_OPCODE_NAMES.get(opcode, f"op_{opcode:#04x}"),
             r.get("handle"), char, value, note),
        )

    conn.commit()
    stats["capture_id"] = cid
    return stats


def import_pcap(conn: sqlite3.Connection, pcap_path: str | Path, *,
                tshark: str | None = None, device_prefix: str | None = None) -> dict:
    """Top-level: tshark -> parse -> load. ``filename`` is the basename (the capture key)."""
    pcap_path = Path(pcap_path)
    hmap = discovery_handle_map(pcap_path, tshark)
    rows = [parse_att_row(line) for line in tshark_att_rows(pcap_path, tshark)]
    return load_att_rows(conn, pcap_path.name, rows, sha256=_sha256(pcap_path),
                         device_prefix=device_prefix, handle_map=hmap)
