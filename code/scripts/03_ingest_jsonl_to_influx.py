#!/usr/bin/env python3
"""Ingest a capture JSONL file into InfluxDB.

The JSONL format produced by 01_capture_stages.py is the canonical lossless
record. This script reads one such file and writes records into InfluxDB so
that Grafana (or ad-hoc Flux queries) can explore them.

Schema:
- measurement `ant_power_only`           — fields from page 0x10
- measurement `ant_crank_torque`         — fields from page 0x12
- measurement `ant_torque_effectiveness` — fields from page 0x13
- measurement `ant_manufacturer`         — fields from page 0x50 (Common Page)
- measurement `ant_product_info`         — fields from page 0x51 (Common Page)
- measurement `ant_battery`              — fields from page 0x52 (Common Page)
- measurement `ant_calibration`          — fields from page 0x01
- measurement `ant_channel_event`        — channel events (RX_FAIL, etc.)
- measurement `ant_acknowledged`         — incoming ACK messages (any page)
- measurement `ant_page_count`           — synthetic 1-per-message for page-mix charting
- measurement `ant_raw`                  — every message with raw_hex field, for forensic search

Common tags:
- capture_id    : derived from filename, e.g. "A-stagesL-steady-20260510-1830"
- source_role   : provided on the CLI: stagesL, stagesR, assioma, sb20fec, other
- device_id     : the ANT+ device number from session_start record
- page_hex      : "0x10", "0x12", etc.
- page_name     : "power_only", "crank_torque", etc.
- message_kind  : "broadcast" or "acknowledged"

Usage:
    python 03_ingest_jsonl_to_influx.py \\
        --input ../findings/captures/A-stagesL-steady-20260510-1830.jsonl \\
        --source-role stagesL \\
        --influx-url http://localhost:8086 \\
        --influx-org sb20proxy \\
        --influx-bucket captures \\
        --influx-token "$INFLUX_TOKEN"

If --capture-id is omitted, it's derived from the filename (basename without
extension). If --influx-* args are omitted, environment variables are used:
INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Iterable

try:
    from influxdb_client import InfluxDBClient, Point, WritePrecision
    from influxdb_client.client.write_api import SYNCHRONOUS
except ImportError as e:  # pragma: no cover - dependency guard
    _msg = f"influxdb-client not installed: {e}\nRun: pip install -e '.[analysis]'"
    if __name__ == "__main__":
        print(_msg, file=sys.stderr)
        sys.exit(1)
    raise ImportError(_msg) from e  # importable: let callers skip, don't kill them


PAGE_NAMES = {
    0x01: "calibration",
    0x10: "power_only",
    0x11: "wheel_torque",
    0x12: "crank_torque",
    0x13: "torque_effectiveness",
    0x20: "crank_torque_frequency",
    0x50: "manufacturer",
    0x51: "product_info",
    0x52: "battery",
}


@dataclass
class IngestContext:
    capture_id: str
    source_role: str
    device_id: str = ""  # filled from session_start record


def page_to_measurement(page: int | None) -> str | None:
    """Map a page number to its dedicated measurement name, if any."""
    if page is None:
        return None
    return {
        0x10: "ant_power_only",
        0x12: "ant_crank_torque",
        0x13: "ant_torque_effectiveness",
        0x50: "ant_manufacturer",
        0x51: "ant_product_info",
        0x52: "ant_battery",
        0x01: "ant_calibration",
    }.get(page)


def make_record_time(record: dict[str, Any], session_start_iso: datetime | None) -> datetime:
    """Best-effort timestamp for an InfluxDB point.

    JSONL records have an iso_time (wall clock, 1s precision) and monotonic_s
    (since session start). We use iso_time as the base and add the monotonic
    fractional part for sub-second precision.
    """
    iso = record.get("iso_time")
    mono = record.get("monotonic_s", 0.0)
    if iso:
        try:
            t0 = datetime.fromisoformat(iso).replace(tzinfo=timezone.utc)
        except ValueError:
            t0 = datetime.now(timezone.utc)
    elif session_start_iso is not None:
        t0 = session_start_iso
    else:
        t0 = datetime.now(timezone.utc)
    return t0 + timedelta(seconds=mono % 1.0)


def make_points_for_message(ctx: IngestContext,
                             record: dict[str, Any],
                             ts: datetime) -> Iterable[Point]:
    """Convert a single JSONL message record into one or more Points."""
    kind = record.get("kind", "")
    data = record.get("data") or {}

    # Channel events — text-only, log as a single measurement
    if kind == "channel_event":
        yield (Point("ant_channel_event")
               .tag("capture_id", ctx.capture_id)
               .tag("source_role", ctx.source_role)
               .field("event", str(record.get("event", "")))
               .time(ts, WritePrecision.MS))
        return

    page = data.get("page")
    page_hex = data.get("page_hex", f"0x{page:02X}" if isinstance(page, int) else "")
    page_name = PAGE_NAMES.get(page, "unknown") if isinstance(page, int) else "unknown"

    # Always emit a raw record for forensic search
    yield (Point("ant_raw")
           .tag("capture_id", ctx.capture_id)
           .tag("source_role", ctx.source_role)
           .tag("page_hex", page_hex)
           .tag("page_name", page_name)
           .tag("message_kind", kind)
           .field("raw_hex", data.get("raw_hex", ""))
           .time(ts, WritePrecision.MS))

    # Always emit a counter record for page-mix charting
    yield (Point("ant_page_count")
           .tag("capture_id", ctx.capture_id)
           .tag("source_role", ctx.source_role)
           .tag("page_hex", page_hex)
           .tag("page_name", page_name)
           .field("count", 1)
           .time(ts, WritePrecision.MS))

    # Page-specific measurements
    measurement = page_to_measurement(page)
    if measurement is None:
        return

    p = (Point(measurement)
         .tag("capture_id", ctx.capture_id)
         .tag("source_role", ctx.source_role)
         .tag("device_id", ctx.device_id)
         .tag("message_kind", kind)
         .time(ts, WritePrecision.MS))

    # Add fields by page type. Skip None values.
    if page == 0x10:
        for k in ("instantaneous_power_w", "accumulated_power", "event_count",
                  "instantaneous_cadence_rpm", "pedal_power_balance"):
            v = data.get(k)
            if v is not None:
                # Rename for friendlier tag in Grafana
                field = "power_w" if k == "instantaneous_power_w" else (
                    "cadence_rpm" if k == "instantaneous_cadence_rpm" else
                    "pedal_balance" if k == "pedal_power_balance" else k)
                p = p.field(field, int(v))
    elif page == 0x12:
        for k in ("event_count", "crank_ticks", "instantaneous_cadence_rpm",
                  "accumulated_crank_period", "accumulated_torque"):
            v = data.get(k)
            if v is not None:
                field = "cadence_rpm" if k == "instantaneous_cadence_rpm" else k
                p = p.field(field, int(v))
    elif page == 0x13:
        for k in ("event_count", "left_te_raw", "right_te_raw",
                  "left_ps_raw", "right_ps_raw"):
            v = data.get(k)
            if v is not None:
                p = p.field(k, int(v))
    elif page == 0x50:
        for k in ("manufacturer_id", "model_number", "hw_revision"):
            v = data.get(k)
            if v is not None:
                p = p.field(k, int(v))
    elif page == 0x51:
        for k in ("sw_revision_main", "sw_revision_supp", "serial_number"):
            v = data.get(k)
            if v is not None:
                p = p.field(k, int(v))
    elif page == 0x52:
        for k in ("battery_id", "operating_time_lsb", "battery_voltage_frac",
                  "battery_status_byte"):
            v = data.get(k)
            if v is not None:
                p = p.field(k, int(v))
    elif page == 0x01:
        for k in ("calibration_id", "auto_zero_status", "calibration_data"):
            v = data.get(k)
            if v is not None:
                p = p.field(k, int(v))
        if "calibration_id" in data:
            p = p.tag("calibration_id_hex", f"0x{data['calibration_id']:02X}")

    yield p


def ingest(input_path: Path, ctx: IngestContext, *,
           influx_url: str, influx_org: str,
           influx_bucket: str, influx_token: str,
           batch_size: int = 1000) -> int:
    """Read JSONL, write to InfluxDB. Returns number of points written."""
    client = InfluxDBClient(url=influx_url, token=influx_token, org=influx_org)
    write_api = client.write_api(write_options=SYNCHRONOUS)
    points: list[Point] = []
    total = 0
    session_start_iso: datetime | None = None

    with open(input_path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as e:
                print(f"warning: skipping bad line: {e}", file=sys.stderr)
                continue

            if record.get("kind") == "session_start":
                ctx.device_id = str(record.get("device_id", ""))
                if record.get("iso_time"):
                    try:
                        session_start_iso = datetime.fromisoformat(
                            record["iso_time"]).replace(tzinfo=timezone.utc)
                    except ValueError:
                        pass
                continue
            if record.get("kind") in ("session_end", "duration_reached",
                                      "interrupted", "channel_open",
                                      "close_error", "node_stop_error"):
                continue

            ts = make_record_time(record, session_start_iso)
            for point in make_points_for_message(ctx, record, ts):
                points.append(point)
                if len(points) >= batch_size:
                    write_api.write(bucket=influx_bucket, org=influx_org,
                                    record=points)
                    total += len(points)
                    points = []

    if points:
        write_api.write(bucket=influx_bucket, org=influx_org, record=points)
        total += len(points)

    write_api.close()
    client.close()
    return total


def main() -> int:
    p = argparse.ArgumentParser(description="Ingest a JSONL capture into InfluxDB")
    p.add_argument("--input", type=Path, required=True)
    p.add_argument("--source-role", required=True,
                   choices=["stagesL", "stagesR", "assioma", "sb20fec", "other"])
    p.add_argument("--capture-id", default=None,
                   help="Tag for this capture; defaults to filename stem")
    p.add_argument("--influx-url", default=os.environ.get("INFLUXDB_URL",
                                                          "http://localhost:8086"))
    p.add_argument("--influx-org", default=os.environ.get("INFLUXDB_ORG", "sb20proxy"))
    p.add_argument("--influx-bucket", default=os.environ.get("INFLUXDB_BUCKET", "captures"))
    p.add_argument("--influx-token", default=os.environ.get("INFLUXDB_TOKEN"))
    args = p.parse_args()

    if not args.influx_token:
        print("InfluxDB token required (--influx-token or $INFLUXDB_TOKEN)",
              file=sys.stderr)
        return 1

    capture_id = args.capture_id or args.input.stem
    ctx = IngestContext(capture_id=capture_id, source_role=args.source_role)

    print(f"Ingesting {args.input} as capture_id={capture_id} role={args.source_role}")
    n = ingest(args.input, ctx,
               influx_url=args.influx_url,
               influx_org=args.influx_org,
               influx_bucket=args.influx_bucket,
               influx_token=args.influx_token)
    print(f"Wrote {n} points to {args.influx_url} bucket={args.influx_bucket}")
    print(f"View in Grafana with capture_id filter: {capture_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
