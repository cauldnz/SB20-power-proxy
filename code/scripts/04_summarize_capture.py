#!/usr/bin/env python3
"""Summarise a JSONL capture as a human-readable markdown report.

The output is designed to be (a) easy for the owner to scan and (b) easy to
paste into chat with Claude for analysis. It surfaces the things that matter
most for Phase 0 protocol diff:

- Capture metadata (device, duration, message count)
- Channel parameters
- Page mix — count and rate per page type
- Common Pages content (manufacturer ID, product info, battery)
- Power/cadence statistics from page 0x10
- Calibration events with their full payloads
- Channel events and incoming ACKs (the bike-to-crank traffic)

Usage:
    python 04_summarize_capture.py --input ../findings/captures/A-stagesL-steady-NNNN.jsonl
    python 04_summarize_capture.py --input ... > ../findings/captures/A-stagesL-summary.md
"""

from __future__ import annotations

import argparse
import json
import statistics
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


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


def load_records(path: Path) -> list[dict[str, Any]]:
    records = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    records.append(json.loads(line))
                except json.JSONDecodeError:
                    pass
    return records


def fmt_dict(d: dict[str, Any], indent: int = 0) -> str:
    pad = "  " * indent
    return "\n".join(f"{pad}- **{k}**: {v}" for k, v in d.items())


def summarise(records: list[dict[str, Any]], path: Path) -> str:
    lines: list[str] = [f"# Capture summary — `{path.name}`", ""]

    # Session metadata
    session_start = next((r for r in records if r.get("kind") == "session_start"), None)
    session_end = next((r for r in records if r.get("kind") == "session_end"), None)
    duration_record = next((r for r in records if r.get("kind") == "duration_reached"), None)

    lines.append("## Session metadata")
    lines.append("")
    if session_start:
        lines.append(f"- **Start time**: {session_start.get('iso_time', 'unknown')}")
        lines.append(f"- **Device ID**: {session_start.get('device_id')}")
        lines.append(f"- **Device type**: {session_start.get('device_type')} (0x{int(session_start.get('device_type', 0)):02X})")
        lines.append(f"- **Transmission type**: {session_start.get('transmission_type')}")
        period = session_start.get('channel_period', 1)
        lines.append(f"- **Channel period**: {period} (= {32768 / period:.2f} Hz)")
        lines.append(f"- **RF frequency**: {session_start.get('rf_freq')} (2400 + {session_start.get('rf_freq')} = {2400 + session_start.get('rf_freq', 0)} MHz)")
    if session_end:
        lines.append(f"- **Total messages logged**: {session_end.get('messages_logged')}")
    if duration_record:
        lines.append(f"- **Duration ran**: {duration_record.get('duration_s')} s")
    lines.append("")

    # Compute monotonic span for rate calculations
    broadcasts = [r for r in records if r.get("kind") == "broadcast"]
    acks = [r for r in records if r.get("kind") == "acknowledged"]
    events = [r for r in records if r.get("kind") == "channel_event"]
    span_s = 1.0
    if broadcasts:
        first_t = broadcasts[0].get("monotonic_s", 0.0)
        last_t = broadcasts[-1].get("monotonic_s", 0.0)
        span_s = max(last_t - first_t, 0.001)

    # Page mix
    page_counts: Counter = Counter()
    page_kinds: dict[int, set] = defaultdict(set)
    for r in records:
        if r.get("kind") in ("broadcast", "acknowledged"):
            data = r.get("data") or {}
            page = data.get("page")
            if isinstance(page, int):
                page_counts[page] += 1
                page_kinds[page].add(r["kind"])

    lines.append("## Page mix")
    lines.append("")
    lines.append(f"Total broadcasts: **{len(broadcasts)}** over **{span_s:.1f}s** = "
                 f"**{len(broadcasts)/span_s:.2f} Hz** aggregate.")
    lines.append(f"Total acknowledged messages (incoming, head unit→meter): **{len(acks)}**.")
    lines.append("")
    lines.append("| Page | Name | Count | Rate (Hz) | Kinds |")
    lines.append("|------|------|-------|-----------|-------|")
    for page in sorted(page_counts.keys()):
        name = PAGE_NAMES.get(page, "unknown")
        kinds = "+".join(sorted(page_kinds[page]))
        lines.append(f"| 0x{page:02X} | {name} | {page_counts[page]} | "
                     f"{page_counts[page]/span_s:.2f} | {kinds} |")
    lines.append("")

    # Common Pages content
    def first_with_page(pg: int) -> dict[str, Any] | None:
        for r in records:
            if r.get("kind") == "broadcast":
                d = r.get("data") or {}
                if d.get("page") == pg:
                    return d
        return None

    lines.append("## Common Pages")
    lines.append("")
    for page, label in [(0x50, "Manufacturer Information (page 0x50)"),
                        (0x51, "Product Information (page 0x51)"),
                        (0x52, "Battery Status (page 0x52)")]:
        d = first_with_page(page)
        lines.append(f"### {label}")
        if d:
            shown = {k: v for k, v in d.items()
                     if k not in ("raw_hex", "page", "page_hex",
                                  "page_toggle_bit", "page_no_toggle")}
            lines.append(fmt_dict(shown))
            lines.append(f"  - raw_hex: `{d.get('raw_hex')}`")
        else:
            lines.append("- *not seen in capture*")
        lines.append("")

    # Power statistics
    powers = []
    cadences = []
    for r in broadcasts:
        d = r.get("data") or {}
        if d.get("page") == 0x10:
            p = d.get("instantaneous_power_w")
            c = d.get("instantaneous_cadence_rpm")
            if p is not None:
                powers.append(p)
            if c is not None:
                cadences.append(c)

    if powers or cadences:
        lines.append("## Power-Only (page 0x10) statistics")
        lines.append("")
        if powers:
            lines.append("**Power (W)**:")
            lines.append(f"- count: {len(powers)}, min: {min(powers)}, "
                         f"max: {max(powers)}, mean: {statistics.mean(powers):.1f}, "
                         f"median: {statistics.median(powers):.0f}, "
                         f"stdev: {statistics.stdev(powers) if len(powers) > 1 else 0:.1f}")
            lines.append("")
        if cadences:
            valid_cad = [c for c in cadences if c > 0]
            if valid_cad:
                lines.append("**Cadence (rpm, non-zero only)**:")
                lines.append(f"- count: {len(valid_cad)}, min: {min(valid_cad)}, "
                             f"max: {max(valid_cad)}, mean: {statistics.mean(valid_cad):.1f}")
                lines.append("")

    # Calibration events
    cal_events = [r for r in records
                  if (r.get("data") or {}).get("page") == 0x01]
    if cal_events:
        lines.append("## Calibration events (page 0x01)")
        lines.append("")
        lines.append(f"Found **{len(cal_events)}** calibration messages.")
        lines.append("")
        for i, r in enumerate(cal_events[:10]):  # cap to first 10
            d = r.get("data") or {}
            lines.append(f"### Event {i+1} — t={r.get('monotonic_s'):.3f}s, kind={r.get('kind')}")
            lines.append(f"- calibration_id: 0x{d.get('calibration_id', 0):02X} ({_cal_id_name(d.get('calibration_id'))})")
            lines.append(f"- auto_zero_status: {d.get('auto_zero_status')}")
            lines.append(f"- calibration_data: {d.get('calibration_data')}")
            lines.append(f"- raw_hex: `{d.get('raw_hex')}`")
            lines.append("")
        if len(cal_events) > 10:
            lines.append(f"_(... {len(cal_events) - 10} more not shown)_")
            lines.append("")

    # Acknowledged messages
    if acks:
        lines.append("## Acknowledged messages (incoming from head unit)")
        lines.append("")
        lines.append("These are messages the head unit (e.g. SB20) sent **to** the meter — "
                     "essential for understanding pairing/calibration handshakes.")
        lines.append("")
        for r in acks[:20]:
            d = r.get("data") or {}
            lines.append(f"- t={r.get('monotonic_s'):.3f}s page=0x{d.get('page', 0):02X} "
                         f"({PAGE_NAMES.get(d.get('page'), 'unknown')}) raw=`{d.get('raw_hex')}`")
        if len(acks) > 20:
            lines.append(f"- _(... {len(acks) - 20} more)_")
        lines.append("")

    # Channel events
    if events:
        lines.append("## Channel events")
        lines.append("")
        event_counts = Counter(r.get("event", "") for r in events)
        for event, count in event_counts.most_common():
            lines.append(f"- `{event}`: {count}")
        lines.append("")

    # First and last broadcast — sanity check the capture isn't truncated
    lines.append("## Sanity check — first 3 broadcasts")
    lines.append("")
    for r in broadcasts[:3]:
        d = r.get("data") or {}
        lines.append(f"- t={r.get('monotonic_s'):.3f}s "
                     f"page={d.get('page_hex')} "
                     f"raw=`{d.get('raw_hex')}`")
    lines.append("")
    lines.append("## Sanity check — last 3 broadcasts")
    lines.append("")
    for r in broadcasts[-3:]:
        d = r.get("data") or {}
        lines.append(f"- t={r.get('monotonic_s'):.3f}s "
                     f"page={d.get('page_hex')} "
                     f"raw=`{d.get('raw_hex')}`")
    lines.append("")

    return "\n".join(lines)


def _cal_id_name(cal_id: int | None) -> str:
    if cal_id is None:
        return "missing"
    return {
        0xAA: "manual zero request",
        0xAB: "auto zero configuration",
        0xAC: "manual zero success response",
        0xAF: "manual zero failed response",
        0xBA: "custom calibration parameters request",
        0xBB: "custom calibration parameters response",
    }.get(cal_id, "unknown")


def main() -> int:
    p = argparse.ArgumentParser(description="Summarise a JSONL capture as markdown")
    p.add_argument("--input", type=Path, required=True)
    args = p.parse_args()

    records = load_records(args.input)
    print(summarise(records, args.input))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
