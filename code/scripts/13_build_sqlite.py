#!/usr/bin/env python3
"""Build (or refresh) the SQLite analysis index from the JSONL captures, and
optionally reconcile two meter streams.

The DB is a *rebuildable derived index* — JSONL in ``findings/captures/`` stays the
source of truth (see ``findings/sqlite-analysis-layer.md``). Import is idempotent,
so re-running only ingests new/changed captures.

Examples
--------
    # build / refresh code/captures.sqlite from every committed capture
    python 13_build_sqlite.py

    # rebuild from scratch (re-materialises annotations from the committed sidecars)
    python 13_build_sqlite.py --rebuild

    # derive annotations (erg holds, ...) and append their committed sidecars
    python 13_build_sqlite.py --auto-annotate

    # §D calibration grid: stages vs assioma, per second, in one ANT+ capture
    python 13_build_sqlite.py --reconcile \
        --capture CAL-grid-20260621.jsonl --stream-a stages --stream-b assioma

    # SB20 FTMS vs an independent ANT+ Assioma feed (two files, wall-clock aligned)
    python 13_build_sqlite.py --reconcile --basis iso \
        --capture-a G-sb20-ftms-erg-20260621-0949.jsonl --stream-a ftms \
        --capture-b ASSIOMA-antfeed-20260621.jsonl     --stream-b assioma
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent
_SRC = _SCRIPTS.parent / "src"
if str(_SRC) not in sys.path:
    sys.path.insert(0, str(_SRC))

from sb20proxy.analysis import annotations as anno  # noqa: E402
from sb20proxy.analysis import jsonl_sqlite as js  # noqa: E402

_DEFAULT_CAPTURES = _SCRIPTS.parent / "findings" / "captures"
_DEFAULT_ANNOTATIONS = _SCRIPTS.parent / "findings" / "annotations"
_DEFAULT_DB = _SCRIPTS.parent / "captures.sqlite"

_COUNT_TABLES = (
    "capture", "record", "ble_notification", "ble_control_point",
    "ble_advertisement", "ant_broadcast",
)


def _print_build(db_path: Path, stats: list[js.ImportStats]) -> None:
    imported = [s for s in stats if not s.skipped]
    skipped = [s for s in stats if s.skipped]
    print(f"# SQLite analysis index - {db_path}")
    print(f"captures: {len(stats)} found, {len(imported)} imported/updated, "
          f"{len(skipped)} unchanged\n")
    for s in imported:
        flag = "replaced" if s.replaced else "new"
        extra = f" ({s.n_unparsed} unparsed lines kept)" if s.n_unparsed else ""
        print(f"  + {s.filename:<48} {s.n_records:>5} records [{flag}]{extra}")
    print()


def _print_counts(conn) -> None:
    print("## table row counts")
    for t in _COUNT_TABLES:
        n = conn.execute(f"SELECT COUNT(*) AS n FROM {t}").fetchone()["n"]
        print(f"  {t:<20} {n:>8}")
    streams = conn.execute(
        "SELECT stream, COUNT(*) AS n FROM power_sample GROUP BY stream ORDER BY n DESC"
    ).fetchall()
    if streams:
        print("\n## power_sample streams (rows carrying power)")
        for row in streams:
            print(f"  {row['stream']:<20} {row['n']:>8}")
    print()


def _do_reconcile(conn, args) -> int:
    cap = args.capture
    cap_a, cap_b = args.capture_a or cap, args.capture_b or cap
    if not cap_a or not cap_b:
        print("reconcile needs --capture (both) or --capture-a/--capture-b", file=sys.stderr)
        return 2
    rows = js.reconcile(
        conn, args.stream_a, args.stream_b,
        capture_a=cap_a, capture_b=cap_b, basis=args.basis,
        bucket_s=args.bucket, min_power=args.min_power, min_cadence=args.min_cadence,
    )
    summ = js.reconcile_summary(rows)
    print(f"## reconcile {args.stream_a} vs {args.stream_b} "
          f"(basis={args.basis}, bucket={args.bucket}s)")
    print(f"  capture A: {cap_a}\n  capture B: {cap_b}")
    print(f"  paired buckets: {summ['n_buckets']}")
    if not rows:
        print("  (no overlap - check stream labels, capture names, and time basis)")
        return 0
    mr = summ["median_ratio"]
    mr_str = f"{mr:.3f}" if mr is not None else "n/a"
    print(f"  median {args.stream_a}/{args.stream_b} ratio: {mr_str}")
    print(f"  mean delta (A-B): {summ['mean_delta_w']:.1f} W   "
          f"median delta: {summ['median_delta_w']:.1f} W")
    print("\n  first paired buckets (bucket: A W @ rpm | B W @ rpm | delta):")
    for r in rows[:12]:
        ca = f"{r.cadence_a:.0f}" if r.cadence_a is not None else "  -"
        cb = f"{r.cadence_b:.0f}" if r.cadence_b is not None else "  -"
        print(f"    {r.bucket:>8}: {r.power_a:6.1f} @ {ca:>3} | "
              f"{r.power_b:6.1f} @ {cb:>3} | {r.delta:+6.1f}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", type=Path, default=_DEFAULT_DB, help=f"DB path (default {_DEFAULT_DB})")
    ap.add_argument("--captures-dir", type=Path, default=_DEFAULT_CAPTURES,
                    help="directory of *.jsonl captures")
    ap.add_argument("--rebuild", action="store_true", help="re-import every capture from scratch")
    ap.add_argument("--no-build", action="store_true", help="skip import; just query/reconcile")
    ap.add_argument("--annotations-dir", type=Path, default=_DEFAULT_ANNOTATIONS,
                    help="directory of committed annotation sidecars")
    ap.add_argument("--auto-annotate", action="store_true",
                    help="derive annotations (erg holds, ...) and append their sidecars")

    ap.add_argument("--reconcile", action="store_true", help="run a two-stream reconciliation")
    ap.add_argument("--capture", help="capture filename pinning both streams (basis=mono)")
    ap.add_argument("--capture-a", dest="capture_a", help="capture for stream A")
    ap.add_argument("--capture-b", dest="capture_b", help="capture for stream B")
    ap.add_argument("--stream-a", dest="stream_a", default="stages", help="stream A label")
    ap.add_argument("--stream-b", dest="stream_b", default="assioma", help="stream B label")
    ap.add_argument("--basis", choices=("mono", "iso"), default="mono",
                    help="time basis: mono (same capture) | iso (wall-clock, cross-capture)")
    ap.add_argument("--bucket", type=float, default=1.0, help="bucket width seconds (default 1)")
    ap.add_argument("--min-power", dest="min_power", type=float, default=40.0,
                    help="drop samples below this power (default 40 W)")
    ap.add_argument("--min-cadence", dest="min_cadence", type=float, default=30.0,
                    help="drop samples below this cadence (default 30 rpm)")
    args = ap.parse_args()

    if args.rebuild and args.db != Path(":memory:") and args.db.exists():
        args.db.unlink()

    conn = js.connect(args.db)
    if not args.no_build:
        stats = js.import_dir(conn, args.captures_dir, replace=args.rebuild)
        _print_build(args.db, stats)
        _print_counts(conn)
        _do_annotations(conn, args)

    if args.reconcile:
        return _do_reconcile(conn, args)
    return 0


def _do_annotations(conn, args) -> None:
    """Materialise the annotation table from the committed sidecars, then (optionally)
    derive fresh auto-annotations and append them.

    Re-materialising from the sidecars on every build is what makes the table a
    lossless rebuild of the committed text (the source of truth) — so ``--rebuild``
    reproduces it and nothing is lost. ``--auto-annotate`` additionally derives erg
    holds (etc.) from the captures and appends them to their sidecars.
    """
    if args.auto_annotate:
        derived = anno.auto_annotate(conn, annotations_dir=args.annotations_dir)
        print(f"## auto-annotate: derived {len(derived)} annotation(s)")
        for ann in derived:
            print(f"  + {ann.filename:<40} {ann.label}")
    loaded = anno.rematerialise_annotations(conn, args.annotations_dir)
    n_files = len(sorted(Path(args.annotations_dir).glob("*.jsonl"))) \
        if Path(args.annotations_dir).exists() else 0
    print(f"\n## annotation table: {loaded} row(s) from {n_files} sidecar(s) "
          f"in {args.annotations_dir}\n")


if __name__ == "__main__":
    raise SystemExit(main())
