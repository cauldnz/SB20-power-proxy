#!/usr/bin/env python3
"""Convert a Zwift ``.zwo`` / Garmin ``.fit`` workout to the on-device workout JSON, and optionally
load it onto a running proxy.

    python scripts/import_workout.py my.zwo --ftp 285
    python scripts/import_workout.py my.zwo --ftp 285 --post http://sb20proxy.local

Prints the canonical JSON to stdout (pipe it, or paste it into the Workout screen's "Paste a
workout" box). With ``--post`` it POSTs to ``<base>/workout/load`` so the workout shows up live.
FIT needs the ``[analysis]`` extra (``pip install -e '.[analysis]'``); FIT power decode is
best-effort until validated against a real exported file.
"""

from __future__ import annotations

import argparse
import sys
import urllib.request
from pathlib import Path

from sb20proxy.workout import WorkoutImportError, from_fit, from_zwo, to_device_json


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Import a .zwo/.fit workout to device JSON.")
    ap.add_argument("file", help="path to a .zwo or .fit workout")
    ap.add_argument("--ftp", type=int, default=250, help="FTP (W) for %%FTP/zone targets (default 250)")
    ap.add_argument("--name", default=None, help="override the workout name")
    ap.add_argument("--post", default=None, metavar="BASE_URL",
                    help="also POST the JSON to <BASE_URL>/workout/load (e.g. http://sb20proxy.local)")
    args = ap.parse_args(argv)

    path = Path(args.file)
    suffix = path.suffix.lower()
    try:
        if suffix == ".zwo":
            workout = from_zwo(path.read_text(encoding="utf-8"), name=args.name)
        elif suffix == ".fit":
            workout = from_fit(str(path), name=args.name)
        else:
            print(f"unsupported file type: {suffix} (want .zwo or .fit)", file=sys.stderr)
            return 2
    except (WorkoutImportError, OSError) as e:
        print(f"import failed: {e}", file=sys.stderr)
        return 1

    body = to_device_json(workout, args.ftp)
    print(body)

    if args.post:
        url = args.post.rstrip("/") + "/workout/load"
        req = urllib.request.Request(url, data=body.encode("utf-8"), method="POST")
        try:
            with urllib.request.urlopen(req, timeout=10) as resp:  # noqa: S310 (user-supplied device URL)
                print(f"POST {url} -> {resp.status} {resp.read().decode(errors='replace').strip()}",
                      file=sys.stderr)
        except OSError as e:
            print(f"POST failed: {e}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
