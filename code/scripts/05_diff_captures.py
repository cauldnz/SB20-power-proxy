#!/usr/bin/env python3
"""Diff two JSONL captures side-by-side as a markdown report.

The most important Phase 0 question is: what does the Stages crank emit
that the Assioma doesn't, and vice versa? This tool produces the answer
in a form that can be pasted into a chat with Claude, committed to
findings/, or used as the appendix of phase-0-report.md.

Usage:
    python 05_diff_captures.py \\
        --left  ../findings/captures/A-stagesL-steady-NNNN.jsonl \\
        --right ../findings/captures/D-assioma-steady-NNNN.jsonl \\
        --left-label "Stages L crank" --right-label "Assioma DUO" \\
        > ../findings/captures/diff-stages-vs-assioma.md
"""

from __future__ import annotations

import argparse
import json
import statistics
from collections import Counter
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


def session_metadata(records: list[dict[str, Any]]) -> dict[str, Any]:
    s = next((r for r in records if r.get("kind") == "session_start"), {})
    return {
        "device_id": s.get("device_id"),
        "device_type": s.get("device_type"),
        "transmission_type": s.get("transmission_type"),
        "channel_period": s.get("channel_period"),
        "rf_freq": s.get("rf_freq"),
    }


def first_with_page(records: list[dict[str, Any]], pg: int) -> dict[str, Any] | None:
    for r in records:
        if r.get("kind") == "broadcast":
            d = r.get("data") or {}
            if d.get("page") == pg:
                return d
    return None


def page_mix(records: list[dict[str, Any]]) -> Counter:
    c: Counter = Counter()
    for r in records:
        if r.get("kind") in ("broadcast", "acknowledged"):
            d = r.get("data") or {}
            page = d.get("page")
            if isinstance(page, int):
                c[page] += 1
    return c


def power_stats(records: list[dict[str, Any]]) -> dict[str, Any]:
    powers = []
    cadences = []
    for r in records:
        if r.get("kind") == "broadcast":
            d = r.get("data") or {}
            if d.get("page") == 0x10:
                if d.get("instantaneous_power_w") is not None:
                    powers.append(d["instantaneous_power_w"])
                c = d.get("instantaneous_cadence_rpm")
                if c is not None and c > 0:
                    cadences.append(c)
    out: dict[str, Any] = {}
    if powers:
        out["power_count"] = len(powers)
        out["power_min"] = min(powers)
        out["power_max"] = max(powers)
        out["power_mean"] = round(statistics.mean(powers), 1)
    if cadences:
        out["cadence_count"] = len(cadences)
        out["cadence_min"] = min(cadences)
        out["cadence_max"] = max(cadences)
        out["cadence_mean"] = round(statistics.mean(cadences), 1)
    return out


def diff_table(left_label: str, right_label: str,
               left: dict[str, Any], right: dict[str, Any],
               keys: list[str] | None = None) -> str:
    keys = keys or sorted(set(left.keys()) | set(right.keys()))
    lines = [f"| Field | {left_label} | {right_label} | Same? |",
             "|-------|-------------|--------------|-------|"]
    for k in keys:
        lv = left.get(k, "—")
        rv = right.get(k, "—")
        same = "✅" if lv == rv else "❌"
        lines.append(f"| {k} | `{lv}` | `{rv}` | {same} |")
    return "\n".join(lines)


def diff_page_dicts(left_label: str, right_label: str,
                    left: dict[str, Any] | None,
                    right: dict[str, Any] | None) -> str:
    if not left and not right:
        return "_Neither capture saw this page._"
    left = left or {}
    right = right or {}
    skip = {"raw_hex", "page", "page_hex", "page_toggle_bit", "page_no_toggle"}
    keys = sorted((set(left.keys()) | set(right.keys())) - skip)
    return diff_table(left_label, right_label, left, right, keys=keys)


def render_diff(left_path: Path, right_path: Path,
                left_label: str, right_label: str) -> str:
    left = load_records(left_path)
    right = load_records(right_path)

    out = [f"# Capture diff — {left_label} vs {right_label}",
           "",
           f"- Left: `{left_path.name}`",
           f"- Right: `{right_path.name}`",
           ""]

    # Channel parameters
    out += ["## Channel parameters",
            "",
            "What the slave configured to subscribe — these are the channel",
            "params we *asked for*, not necessarily what the device actually emits",
            "(transmission type 0 means wildcard).",
            "",
            diff_table(left_label, right_label,
                       session_metadata(left), session_metadata(right)),
            ""]

    # Page mix
    out += ["## Page mix",
            "",
            "Counts per page across the whole capture. Different rates can",
            "matter (channel period mismatch) but more often we care about",
            "*which* pages each meter emits.",
            ""]
    lm = page_mix(left)
    rm = page_mix(right)
    pages = sorted(set(lm.keys()) | set(rm.keys()))
    out.append(f"| Page | Name | {left_label} | {right_label} |")
    out.append("|------|------|" + "-" * len(left_label) + "|" + "-" * len(right_label) + "|")
    for page in pages:
        name = PAGE_NAMES.get(page, "unknown")
        out.append(f"| 0x{page:02X} | {name} | {lm.get(page, 0)} | {rm.get(page, 0)} |")
    out.append("")

    # Common Pages content
    out += ["## Manufacturer Information (page 0x50)",
            "",
            "**This is the most important diff for our project.** If manufacturer_id",
            "differs, the SB20 may be using it for whitelisting; we'd need to spoof",
            "Stages's value when we broadcast.",
            "",
            diff_page_dicts(left_label, right_label,
                            first_with_page(left, 0x50),
                            first_with_page(right, 0x50)),
            "",
            "## Product Information (page 0x51)",
            "",
            diff_page_dicts(left_label, right_label,
                            first_with_page(left, 0x51),
                            first_with_page(right, 0x51)),
            "",
            "## Battery Status (page 0x52)",
            "",
            diff_page_dicts(left_label, right_label,
                            first_with_page(left, 0x52),
                            first_with_page(right, 0x52)),
            ""]

    # Power statistics — sanity check
    out += ["## Power statistics",
            "",
            "Sanity check that captures actually contain pedalled data; values",
            "should be in roughly the same range if both captures were on the",
            "same bike during pedalling.",
            "",
            diff_table(left_label, right_label,
                       power_stats(left), power_stats(right)),
            ""]

    # Acknowledged messages
    out += ["## Incoming acknowledged messages",
            "",
            "Messages the head unit sent **to** the meter. The Stages capture",
            "during pairing/zero-reset should have these; the Assioma steady-state",
            "capture probably won't unless triggered from a paired head unit.",
            ""]
    l_acks = [r for r in left if r.get("kind") == "acknowledged"]
    r_acks = [r for r in right if r.get("kind") == "acknowledged"]
    out.append(f"- {left_label}: **{len(l_acks)}** acks")
    out.append(f"- {right_label}: **{len(r_acks)}** acks")
    out.append("")

    if l_acks:
        out.append(f"### {left_label} — first 5 acks")
        for r in l_acks[:5]:
            d = r.get("data") or {}
            out.append(f"- t={r.get('monotonic_s'):.3f}s "
                       f"page=0x{d.get('page', 0):02X} "
                       f"raw=`{d.get('raw_hex')}`")
        out.append("")
    if r_acks:
        out.append(f"### {right_label} — first 5 acks")
        for r in r_acks[:5]:
            d = r.get("data") or {}
            out.append(f"- t={r.get('monotonic_s'):.3f}s "
                       f"page=0x{d.get('page', 0):02X} "
                       f"raw=`{d.get('raw_hex')}`")
        out.append("")

    # Calibration messages
    l_cal = [r for r in left if (r.get("data") or {}).get("page") == 0x01]
    r_cal = [r for r in right if (r.get("data") or {}).get("page") == 0x01]
    out += ["## Calibration page (0x01) traffic",
            "",
            f"- {left_label}: **{len(l_cal)}** calibration messages",
            f"- {right_label}: **{len(r_cal)}** calibration messages",
            ""]
    if l_cal:
        out.append(f"### {left_label} — first 3 calibration messages")
        for r in l_cal[:3]:
            d = r.get("data") or {}
            out.append(f"- t={r.get('monotonic_s'):.3f}s kind={r.get('kind')} "
                       f"cal_id=0x{d.get('calibration_id', 0):02X} "
                       f"raw=`{d.get('raw_hex')}`")
        out.append("")
    if r_cal:
        out.append(f"### {right_label} — first 3 calibration messages")
        for r in r_cal[:3]:
            d = r.get("data") or {}
            out.append(f"- t={r.get('monotonic_s'):.3f}s kind={r.get('kind')} "
                       f"cal_id=0x{d.get('calibration_id', 0):02X} "
                       f"raw=`{d.get('raw_hex')}`")
        out.append("")

    out += ["## Headline questions to discuss",
            "",
            "Look at the diff above and answer these for the Phase 0 report:",
            "",
            "1. Is the **manufacturer_id** different? If yes, this is likely",
            "   the most important thing to spoof.",
            "2. Does the Stages capture have **acknowledged messages** the",
            "   Assioma capture doesn't? If yes, the SB20 is doing something",
            "   beyond passive listening.",
            "3. Is there any **page** that one emits and the other doesn't?",
            "4. Do the **channel parameters** (transmission type, period) match?",
            "5. What does the Stages calibration response payload look like",
            "   that we'd need to mimic?",
            ""]

    return "\n".join(out)


def main() -> int:
    p = argparse.ArgumentParser(description="Diff two JSONL captures")
    p.add_argument("--left", type=Path, required=True)
    p.add_argument("--right", type=Path, required=True)
    p.add_argument("--left-label", default="LEFT")
    p.add_argument("--right-label", default="RIGHT")
    args = p.parse_args()

    print(render_diff(args.left, args.right, args.left_label, args.right_label))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
