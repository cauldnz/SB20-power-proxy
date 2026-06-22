#!/usr/bin/env python3
"""parse_diag.py — decode a tester's on-device /diag report (the desk half of the collaboration loop).

A beta tester whose meter isn't recognised saves the board's ``GET /diag`` page and sends it. This
decodes the raw CPS frames in it with the same codec the firmware mirrors and tells us, offline,
whether we handle the meter — and emits a golden-vector test stub to add it (real-data-first).

    python scripts/parse_diag.py <diag.txt>            # decode + summarise
    python scripts/parse_diag.py <diag.txt> --fixture  # also print a golden-vector test stub
    cat diag.txt | python scripts/parse_diag.py -       # from stdin
"""

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
from sb20proxy.analysis.diag import parse_diag_report  # noqa: E402
from sb20proxy.ble import cps  # noqa: E402


def _decode(frame: str):
    try:
        return cps.decode_cps_measurement(bytes.fromhex(frame)), None
    except Exception as exc:  # noqa: BLE001 — report the failure, don't crash the tool
        return None, str(exc)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", help="the saved /diag report (a file, or - for stdin)")
    ap.add_argument("--fixture", action="store_true",
                    help="also print a golden-vector test stub for the first frame")
    args = ap.parse_args()

    text = sys.stdin.read() if args.path == "-" else open(args.path, encoding="utf-8",
                                                           errors="replace").read()
    rep = parse_diag_report(text)

    print(f"fw: {rep.fw or '?'}")
    print(f"config: source_addr={rep.config.get('source_addr', '')!r} "
          f"name_filter={rep.config.get('source_name_filter', '')!r} "
          f"spoof={rep.config.get('spoof_name', '')!r}")
    print(f"status: source={rep.status.get('source', '?')} "
          f"name={rep.status.get('source_connected_name', '')!r} "
          f"power={rep.status.get('src_power_w', '?')}W")
    print(f"\n{len(rep.frames)} meter frame(s):")

    flags_seen: set[int] = set()
    ok = 0
    for i, f in enumerate(rep.frames):
        m, err = _decode(f)
        if err:
            print(f"  [{i}] {f}  -> DECODE FAILED: {err}")
            continue
        ok += 1
        flags_seen.add(m.flags)
        bits = []
        if m.pedal_balance is not None:
            bits.append(f"bal={m.balance_pct:.0f}%L")
        if m.cumulative_crank_revs is not None:
            bits.append(f"crank={m.cumulative_crank_revs}")
        if m.accumulated_torque is not None:
            bits.append(f"torque={m.accumulated_torque}")
        print(f"  [{i}] {f}  -> flags=0x{m.flags:04x} power={m.power_w}W "
              + " ".join(bits))

    print(f"\nverdict: {ok}/{len(rep.frames)} frames decode cleanly; "
          f"flag layouts seen: {sorted(f'0x{x:04x}' for x in flags_seen)}")
    if rep.frames and ok == len(rep.frames):
        print("  -> our CPS codec already handles this meter. Add the frames as golden vectors to lock it.")
    elif rep.frames:
        print("  -> some frames don't decode — extend cps.decode_cps_measurement for this meter's flags.")

    if args.fixture and rep.frames:
        f0 = rep.frames[0]
        m, _ = _decode(f0)
        meter = (rep.status.get("source_connected_name") or rep.config.get("spoof_name") or "METER")
        print("\n# --- golden-vector stub for code/tests/test_ble_cps.py (verify the values!) ---")
        print(f'def test_decode_real_{meter.split()[0].lower()}_frame():')
        print(f'    m = cps.decode_cps_measurement(bytes.fromhex("{f0}"))')
        if m is not None:
            print(f"    assert m.flags == 0x{m.flags:04x}")
            print(f"    assert m.power_w == {m.power_w}")
            if m.pedal_balance is not None:
                print(f"    assert m.pedal_balance == {m.pedal_balance}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
