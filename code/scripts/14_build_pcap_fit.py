#!/usr/bin/env python3
"""Build / refresh the SQLite index from the committed ``.pcap`` (nRF BLE sniffs) and
``.fit`` (Garmin) captures — the BLE/FIT companion to ``13_build_sqlite.py`` (JSONL).

Everything lands in the *same* index (``captures.sqlite``), so a sniffed BLE power
stream, a JSONL capture, and a Garmin FIT all reconcile with a timestamp JOIN. The
captures stay the source of truth; this index is a rebuildable derivative.

Requires **tshark** (Wireshark CLI) for the pcaps — see ``pcap_sqlite.tshark_bin``.

Examples
--------
    python 14_build_pcap_fit.py                       # import every *.pcap + *.fit
    python 14_build_pcap_fit.py --rebuild             # from scratch
    python 14_build_pcap_fit.py --tshark "C:\\Program Files\\Wireshark\\tshark.exe"
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent
_SRC = _SCRIPTS.parent / "src"
if str(_SRC) not in sys.path:
    sys.path.insert(0, str(_SRC))

from sb20proxy.analysis import fit_sqlite as fs  # noqa: E402
from sb20proxy.analysis import jsonl_sqlite as js  # noqa: E402
from sb20proxy.analysis import pcap_sqlite as ps  # noqa: E402

_DEFAULT_CAPTURES = _SCRIPTS.parent / "findings" / "captures"
_DEFAULT_DB = _SCRIPTS.parent / "captures.sqlite"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", type=Path, default=_DEFAULT_DB)
    ap.add_argument("--captures-dir", type=Path, default=_DEFAULT_CAPTURES)
    ap.add_argument("--tshark", default=None, help="path to tshark.exe (else auto-detect)")
    ap.add_argument("--rebuild", action="store_true", help="delete the DB and re-import")
    args = ap.parse_args()

    if args.rebuild and args.db != Path(":memory:") and args.db.exists():
        args.db.unlink()

    conn = js.connect(args.db)
    pcaps = sorted(args.captures_dir.glob("*.pcap"))
    fits = sorted(args.captures_dir.glob("*.fit"))
    print(f"# pcap + fit SQLite index -> {args.db}")
    print(f"  {len(pcaps)} pcap, {len(fits)} fit in {args.captures_dir}\n")

    for p in pcaps:
        try:
            st = ps.import_pcap(conn, p, tshark=args.tshark)
            print(f"  + {p.name:<44} att={st['att']:<5} cps={st['cps']} ibd={st['ibd']} "
                  f"ftms_cp={st['ftms_cp']} stages_ctrl={st['stages_ctrl']}")
        except Exception as exc:  # noqa: BLE001 — report + keep going over the batch
            print(f"  ! {p.name:<44} FAILED: {exc}")
    for f in fits:
        try:
            st = fs.import_fit(conn, f)
            print(f"  + {f.name:<44} records={st['records']:<5} laps={st['laps']} "
                  f"power_records={st['power_records']}")
        except Exception as exc:  # noqa: BLE001
            print(f"  ! {f.name:<44} FAILED: {exc}")

    streams = conn.execute(
        "SELECT stream, protocol, COUNT(*) n FROM power_sample GROUP BY stream, protocol "
        "ORDER BY n DESC"
    ).fetchall()
    if streams:
        print("\n## power_sample streams (reconcilable):")
        for r in streams:
            print(f"  {r['stream']:<22} {r['protocol']:<6} {r['n']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
