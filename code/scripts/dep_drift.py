#!/usr/bin/env python3
"""dep_drift.py — has upstream moved past what we pin?

Pinning makes builds reproducible; it also makes them quietly stale. This is the other half of the
bargain the owner asked for (2026-09-25): pin exactly, and keep a standing check so a pin becomes a
*decision to revisit* rather than a version nobody looks at again.

Why it exists: `lvgl/lvgl@^9.3.0` carried the comment "resolves 9.5.x so pixels match". That was
true only while 9.5.x was newest. A fresh environment resolved **9.6.0** against boards already
built on 9.5.0, which would have had one head unit rendering on a different UI library than the host
harness the layout was developed against — and the bench pass comparing it to the others. A caret
range is not a pin, and a comment is not a check.

    python code/scripts/dep_drift.py                 # table + exit 1 if anything drifted
    python code/scripts/dep_drift.py --json          # machine-readable, for CI
    python code/scripts/dep_drift.py --offline FILE  # compare against a saved registry snapshot

The parsing and comparison are pure and host-tested; the registry lookup is the seam
(`--offline`, or inject `fetch_latest`), so the hermetic suite never touches the network.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
INI_FILES = ("firmware/platformio.ini", "firmware-nrf/platformio.ini")

# `owner/name@spec` inside a lib_deps list. The spec may be exact (9.6.0), a caret/tilde range, or
# absent. Anything that is not a registry package (symlink://, a URL, a bare path) is skipped.
# The lookbehind keeps us out of URLs and paths (https://…/x@y, symlink://…): a package token is
# preceded by start-of-line, whitespace or '='. Matching only at line start would MISS the
# single-line form `lib_deps = lvgl/lvgl@9.6.0`, which is how the host harness declares it — i.e.
# it would have been blind to exactly the host-vs-device split that prompted this script.
_DEP = re.compile(r"(?:^|[\s=])([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)@([^\s;,]+)", re.M)
_SEMVER = re.compile(r"^(\d+)\.(\d+)\.(\d+)")


def parse_deps(text: str) -> dict[str, set[str]]:
    """package -> the set of version specs it is declared with across this file."""
    out: dict[str, set[str]] = {}
    for pkg, spec in _DEP.findall(text):
        out.setdefault(pkg, set()).add(spec.strip())
    return out


def collect_pins(repo: Path = REPO) -> dict[str, set[str]]:
    merged: dict[str, set[str]] = {}
    for rel in INI_FILES:
        p = repo / rel
        if not p.exists():
            continue
        for pkg, specs in parse_deps(p.read_text(encoding="utf-8")).items():
            merged.setdefault(pkg, set()).update(specs)
    return merged


def is_exact(spec: str) -> bool:
    """An exact pin is a bare semver. '^9.3.0' / '~2.2.0' / '>=1' are ranges, i.e. not pinned."""
    return bool(_SEMVER.fullmatch(spec.strip()))


def semver_key(v: str) -> tuple[int, int, int]:
    m = _SEMVER.match(v.strip())
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else (0, 0, 0)


def fetch_latest(pkg: str, timeout: float = 25.0) -> str | None:
    """Latest published version from the PlatformIO registry. None if it cannot be determined."""
    owner, name = pkg.split("/", 1)
    url = f"https://api.registry.platformio.org/v3/packages/{owner}/library/{name}"
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            return (json.load(r).get("version") or {}).get("name")
    except (urllib.error.URLError, TimeoutError, ValueError, KeyError):
        return None


def assess(pins: dict[str, set[str]], latest: dict[str, str | None]) -> list[dict]:
    """One row per package: what we declare, what upstream has, and whether to act."""
    rows = []
    for pkg in sorted(pins):
        specs = sorted(pins[pkg])
        up = latest.get(pkg)
        exact = [s for s in specs if is_exact(s)]
        consistent = len(set(specs)) == 1
        behind = bool(exact and up and semver_key(up) > semver_key(exact[0]))
        rows.append({
            "package": pkg,
            "declared": specs,
            "pinned_exactly": bool(exact) and consistent,
            "consistent": consistent,
            "latest": up,
            "behind": behind,
            # A range is reported too: not "behind" (it floats) but not reproducible either, which
            # is its own finding -- that is exactly how the lvgl surprise happened.
            "floats": not exact,
        })
    return rows


def render(rows: list[dict]) -> str:
    w = max((len(r["package"]) for r in rows), default=10)
    out = [f"{'package'.ljust(w)}  {'declared':<18} {'latest':<10} note"]
    for r in rows:
        decl = ",".join(r["declared"])
        note = []
        if not r["consistent"]:
            note.append("INCONSISTENT across envs")
        if r["floats"]:
            note.append("floats (range, not a pin)")
        if r["behind"]:
            note.append(f"BEHIND -> {r['latest']}")
        if not note:
            note.append("up to date")
        out.append(f"{r['package'].ljust(w)}  {decl:<18} "
                   f"{str(r['latest'] or '?'):<10} {'; '.join(note)}")
    return "\n".join(out)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--offline", metavar="FILE",
                    help="JSON {package: latest_version} instead of querying the registry")
    a = ap.parse_args(argv)

    pins = collect_pins()
    if not pins:
        print("no registry pins found - did the ini layout change?", file=sys.stderr)
        return 2

    if a.offline:
        latest = json.loads(Path(a.offline).read_text(encoding="utf-8"))
    else:
        latest = {pkg: fetch_latest(pkg) for pkg in pins}

    rows = assess(pins, latest)
    print(json.dumps(rows, indent=2) if a.json else render(rows))

    actionable = [r for r in rows if r["behind"] or not r["consistent"]]
    if actionable:
        names = ", ".join(r["package"] for r in actionable)
        print(f"\n{len(actionable)} package(s) need a look: {names}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
