#!/usr/bin/env python3
"""Validate a Phase 0 JSONL capture — is it well-formed and usable?

This is the Session A checkpoint gate. Run it immediately after your first
capture (Session A) and BEFORE running sessions B–F. The cost of a broken
stick, wrong device ID, or misconfigured channel multiplies if you run all
six sessions before noticing the first one was bad.

It runs a battery of sanity checks against a capture and ends with a single
clear verdict:

  PASS    (exit 0) — capture mechanism is working; proceed.
  REVIEW  (exit 1) — usable, but something looks unusual; read the WARNs.
  FAIL    (exit 2) — not usable; something is broken. Stop and fix it.

The verdict is the worst severity triggered by any check:
  any FAIL  -> FAIL
  else any WARN -> REVIEW
  else      -> PASS

Output is designed to be paste-friendly for a second opinion in chat. Add
--markdown for a nicely-formatted version you can drop straight into a
conversation:

    python 00_validate_capture.py --input '../findings/captures/A-*.jsonl'
    python 00_validate_capture.py --input '../findings/captures/A-*.jsonl' --markdown \\
        > /tmp/session-a-validation.md

(Keep the glob quoted — this script picks the newest match itself; an unquoted
glob that matches several files becomes several args and errors.)

This script reads the JSONL schema produced by 01_capture_stages.py and
consumed by 03/04/05. It does NOT import openant and does NOT touch hardware,
so it is safe to run anywhere (including pasting captures around).
"""

from __future__ import annotations

import argparse
import glob
import json
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

# Severity ordering: higher is worse.
PASS = 0
WARN = 1
FAIL = 2
SEV_NAME = {PASS: "PASS", WARN: "WARN", FAIL: "FAIL"}
SEV_GLYPH = {PASS: "✅", WARN: "⚠️ ", FAIL: "❌"}

# Expected channel parameters for a standard ANT+ Bike Power capture.
EXPECTED_DEVICE_TYPE = 0x0B  # 11
EXPECTED_RF_FREQ = 57        # 2457 MHz
EXPECTED_PERIOD = 8182       # 4 Hz

KNOWN_PAGES = {0x01, 0x10, 0x11, 0x12, 0x13, 0x20, 0x50, 0x51, 0x52}


@dataclass
class Check:
    name: str
    severity: int
    detail: str


@dataclass
class Report:
    path: Path
    checks: list[Check] = field(default_factory=list)

    def add(self, name: str, severity: int, detail: str) -> None:
        self.checks.append(Check(name, severity, detail))

    @property
    def verdict(self) -> int:
        return max((c.severity for c in self.checks), default=PASS)


def load_records(path: Path) -> tuple[list[dict[str, Any]], int]:
    """Return (parsed records, count of unparseable non-empty lines)."""
    records: list[dict[str, Any]] = []
    bad = 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                bad += 1
    return records, bad


def run_checks(path: Path) -> Report:
    rep = Report(path=path)

    # --- File-level ---------------------------------------------------------
    size = path.stat().st_size
    if size == 0:
        rep.add("file-nonempty", FAIL, "Capture file is empty (0 bytes). Nothing was recorded.")
        return rep  # No point continuing.
    rep.add("file-nonempty", PASS, f"File is {size:,} bytes.")

    records, bad_lines = load_records(path)
    if bad_lines:
        sev = WARN if bad_lines < max(1, len(records) // 20) else FAIL
        rep.add("json-parse", sev,
                f"{bad_lines} line(s) failed to parse as JSON. "
                f"({len(records)} parsed OK.)")
    else:
        rep.add("json-parse", PASS, f"All {len(records)} non-empty lines parsed as JSON.")

    if not records:
        rep.add("records-present", FAIL, "No parseable records at all.")
        return rep

    kinds = Counter(r.get("kind") for r in records)

    # --- Session framing ----------------------------------------------------
    start = next((r for r in records if r.get("kind") == "session_start"), None)
    end = next((r for r in records if r.get("kind") == "session_end"), None)
    if start is None:
        rep.add("session-start", FAIL,
                "No 'session_start' record. The capture script may have crashed "
                "before opening the channel.")
    else:
        rep.add("session-start", PASS, "session_start present.")
    if end is None:
        rep.add("session-end", WARN,
                "No 'session_end' record. Capture may have been killed (e.g. terminal "
                "closed) rather than stopping cleanly. Data so far is still usable.")
    else:
        rep.add("session-end", PASS, "session_end present (clean stop).")

    # --- Channel parameters -------------------------------------------------
    if start is not None:
        dt = start.get("device_type")
        if dt == EXPECTED_DEVICE_TYPE:
            rep.add("device-type", PASS, f"device_type = {dt} (Bike Power, as expected).")
        else:
            rep.add("device-type", WARN,
                    f"device_type = {dt}, expected {EXPECTED_DEVICE_TYPE} (Bike Power). "
                    "Fine if you meant to capture a different profile.")

        rf = start.get("rf_freq")
        if rf == EXPECTED_RF_FREQ:
            rep.add("rf-freq", PASS, f"rf_freq = {rf} (2457 MHz, ANT+).")
        else:
            rep.add("rf-freq", WARN, f"rf_freq = {rf}, expected {EXPECTED_RF_FREQ} for ANT+.")

        period = start.get("channel_period")
        if period == EXPECTED_PERIOD:
            rep.add("channel-period", PASS, f"channel_period = {period} (~4 Hz).")
        elif isinstance(period, int) and period > 0:
            rep.add("channel-period", WARN,
                    f"channel_period = {period} (~{32768/period:.2f} Hz), "
                    f"expected {EXPECTED_PERIOD} (~4 Hz).")
        else:
            rep.add("channel-period", WARN, f"channel_period = {period!r} (unexpected).")

        dev_id = start.get("device_id")
        if dev_id in (0, None):
            rep.add("device-id", WARN,
                    f"device_id = {dev_id!r} (wildcard or missing). You'll have paired "
                    "with whatever was found first — fine for a smoke test, but for a "
                    "real session pass the specific 5-digit ID.")
        else:
            rep.add("device-id", PASS, f"device_id = {dev_id}.")

    # --- Extended messages (source-ID tails) --------------------------------
    ext = next((r for r in records if r.get("kind") == "ext_messages"), None)
    if ext is None:
        rep.add("ext-messages", WARN,
                "No 'ext_messages' record. The capture script writes one as of "
                "package Rev 7 — you may be running an older copy of "
                "01_capture_stages.py. Capture is still usable, but packets "
                "won't carry ext_device_number source-ID tails.")
    elif ext.get("enabled"):
        rep.add("ext-messages", PASS, "Extended RX messages enabled (0x66 accepted).")
    else:
        rep.add("ext-messages", WARN,
                f"Extended RX messages could NOT be enabled "
                f"(error: {ext.get('error', 'unknown')}). Capture works, but "
                "packets lack source-ID tails — fine for Session A, weaker for "
                "the multi-device disambiguation in Sessions C/F.")

    # --- Did we actually receive anything? ---------------------------------
    broadcasts = [r for r in records if r.get("kind") == "broadcast"]
    acks = [r for r in records if r.get("kind") == "acknowledged"]
    n_b = len(broadcasts)

    if n_b == 0:
        rep.add("broadcasts-received", FAIL,
                "ZERO broadcast records. The script ran but the stick never received "
                "anything from the target device. Most likely: wrong device_id, the "
                "meter was asleep (rotate cranks to wake it), the stick isn't attached "
                "to WSL, or RF interference. Re-running with the same setup will just "
                "produce another empty capture — fix the cause first.")
        return rep
    rep.add("broadcasts-received", PASS, f"{n_b} broadcast records received.")

    # --- Rate sanity --------------------------------------------------------
    span = 0.0
    if n_b >= 2:
        t0 = broadcasts[0].get("monotonic_s", 0.0)
        t1 = broadcasts[-1].get("monotonic_s", 0.0)
        span = max(t1 - t0, 0.0)
    if span > 0:
        rate = n_b / span
        if rate < 1.0:
            rep.add("broadcast-rate", WARN,
                    f"Aggregate broadcast rate is {rate:.2f} Hz over {span:.0f}s — low. "
                    "Expected ~4 Hz from a Bike Power meter. Could be dropouts (Wi-Fi "
                    "interference on 2.4 GHz), a sleepy meter, or a low battery.")
        elif rate > 12.0:
            rep.add("broadcast-rate", WARN,
                    f"Aggregate broadcast rate is {rate:.2f} Hz — higher than the ~4 Hz "
                    "baseline. May indicate multiple meters in range bleeding into the "
                    "capture, or a fast-mode meter. Check the device_id scoping.")
        else:
            rep.add("broadcast-rate", PASS,
                    f"Aggregate broadcast rate {rate:.2f} Hz over {span:.0f}s (sane).")
    else:
        rep.add("broadcast-rate", WARN,
                "Could not compute a broadcast rate (too few records or no timestamps).")

    # --- Capture length -----------------------------------------------------
    if span and span < 60 and end is not None:
        rep.add("capture-length", WARN,
                f"Capture span is only {span:.0f}s. Fine for a smoke test, but Session A "
                "should be ~15 min of varied pedalling to be representative.")
    elif span:
        rep.add("capture-length", PASS, f"Capture spans {span:.0f}s.")

    # --- Page decoding ------------------------------------------------------
    page_counts: Counter = Counter()
    short_payloads = 0
    for r in broadcasts:
        d = r.get("data") or {}
        if d.get("error") == "short payload":
            short_payloads += 1
            continue
        pg = d.get("page")
        if isinstance(pg, int):
            page_counts[pg] += 1

    if not page_counts:
        rep.add("page-decode", FAIL,
                "Broadcasts were received but none decoded to a recognisable page. "
                "The decode logic or the payload framing is off — check decode_page().")
    else:
        top = ", ".join(f"0x{p:02X}×{c}" for p, c in page_counts.most_common(6))
        rep.add("page-decode", PASS, f"Pages decoded: {top}.")

    if short_payloads:
        sev = WARN if short_payloads < n_b // 10 else FAIL
        rep.add("payload-length", sev,
                f"{short_payloads}/{n_b} broadcasts had short (<8 byte) payloads. "
                "ANT+ data pages should be 8 bytes — framing may be wrong.")
    else:
        rep.add("payload-length", PASS, "All broadcast payloads are full 8-byte pages.")

    # --- Power-Only page presence (the data we actually care about) --------
    if 0x10 not in page_counts:
        rep.add("power-page", WARN,
                "No Power-Only pages (0x10) seen. Almost every modern meter sends these. "
                "If you only saw torque pages (0x12/0x20), that's unusual but not fatal — "
                "note it. If you saw nothing but common pages, the meter may not have been "
                "pedalled.")
    else:
        # Sanity-check the power values themselves.
        powers = [
            (r.get("data") or {}).get("instantaneous_power_w")
            for r in broadcasts
            if (r.get("data") or {}).get("page") == 0x10
        ]
        powers = [p for p in powers if isinstance(p, int)]
        if powers:
            pmax = max(powers)
            nonzero = sum(1 for p in powers if p > 0)
            if pmax == 0:
                rep.add("power-values", WARN,
                        "All instantaneous_power_w values are 0. Either you didn't pedal, "
                        "or the meter wasn't loaded. Session A wants varied power.")
            elif pmax > 2000:
                rep.add("power-values", WARN,
                        f"Max instantaneous_power_w = {pmax} W — implausibly high. Possible "
                        "decode/byte-order issue; spot-check raw_hex for a high-power record.")
            else:
                rep.add("power-values", PASS,
                        f"Power values look sane (max {pmax} W, {nonzero}/{len(powers)} "
                        "non-zero samples).")
        else:
            rep.add("power-values", WARN, "0x10 pages present but no decoded power values.")

    # --- Unknown pages (informational) -------------------------------------
    unknown = sorted(p for p in page_counts if p not in KNOWN_PAGES)
    if unknown:
        rep.add("unknown-pages", WARN,
                "Saw page(s) not in the known set: "
                + ", ".join(f"0x{p:02X}" for p in unknown)
                + ". Not necessarily wrong — could be manufacturer-specific pages worth "
                "investigating. Note them for the Phase 0 report.")

    # --- ACK / inbound traffic (informational for Session A) ---------------
    if acks:
        rep.add("inbound-acks", PASS,
                f"{len(acks)} acknowledged (inbound) message(s) captured — good sign the "
                "ACK-capture path works, which Session C depends on.")
    else:
        rep.add("inbound-acks", PASS,
                "No acknowledged/inbound messages — expected for a plain steady-state "
                "Session A. (Session C is where inbound ACKs matter; de-risk that with "
                "the Session C-0 dry run.)")

    return rep


def render_plain(rep: Report) -> str:
    lines = [
        f"Capture validation: {rep.path.name}",
        "=" * 60,
    ]
    for c in rep.checks:
        lines.append(f"[{SEV_NAME[c.severity]}] {c.name}: {c.detail}")
    lines.append("=" * 60)
    v = rep.verdict
    verdict = {PASS: "PASS", WARN: "REVIEW", FAIL: "FAIL"}[v]
    lines.append(f"VERDICT: {verdict}")
    lines.append(_verdict_advice(v))
    return "\n".join(lines)


def render_markdown(rep: Report) -> str:
    v = rep.verdict
    verdict = {PASS: "PASS", WARN: "REVIEW", FAIL: "FAIL"}[v]
    glyph = {PASS: "✅", WARN: "⚠️", FAIL: "❌"}[v]
    lines = [
        f"# Capture validation — `{rep.path.name}`",
        "",
        f"**Verdict: {glyph} {verdict}**",
        "",
        _verdict_advice(v),
        "",
        "| Check | Severity | Detail |",
        "|-------|----------|--------|",
    ]
    for c in rep.checks:
        detail = c.detail.replace("|", "\\|")
        lines.append(f"| {c.name} | {SEV_NAME[c.severity]} | {detail} |")
    lines.append("")
    counts = Counter(c.severity for c in rep.checks)
    lines.append(
        f"_Checks: {counts[PASS]} PASS, {counts[WARN]} WARN, {counts[FAIL]} FAIL._"
    )
    return "\n".join(lines)


def _verdict_advice(v: int) -> str:
    if v == PASS:
        return ("Capture mechanism is working. Proceed with the next session.")
    if v == WARN:
        return ("Capture is usable but something looks unusual. Read the WARN rows; "
                "if you're unsure whether to re-record, paste this output into a chat "
                "for a second opinion before continuing.")
    return ("Capture is NOT usable — something is broken. Stop. Re-running with the "
            "same setup will just produce another bad capture. Paste this output into "
            "a chat to work out what to change first.")


def main() -> int:
    p = argparse.ArgumentParser(description="Validate a Phase 0 JSONL capture.")
    p.add_argument("--input", required=True,
                   help="Path to the capture JSONL (globs allowed, e.g. 'A-*.jsonl').")
    p.add_argument("--markdown", action="store_true",
                   help="Emit markdown (paste-friendly for chat) instead of plain text.")
    args = p.parse_args()

    matches = sorted(glob.glob(args.input))
    if not matches:
        print(f"No file matched: {args.input}", file=sys.stderr)
        return FAIL
    if len(matches) > 1:
        print(f"Pattern matched {len(matches)} files; validating the newest: {matches[-1]}",
              file=sys.stderr)
    path = Path(matches[-1])

    rep = run_checks(path)
    out = render_markdown(rep) if args.markdown else render_plain(rep)
    print(out)
    return rep.verdict


if __name__ == "__main__":
    raise SystemExit(main())
