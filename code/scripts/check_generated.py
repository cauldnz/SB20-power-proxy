#!/usr/bin/env python3
"""check_generated.py — one gate over every generated-artifact mirror in the repo.

Four generators emit committed artifacts, and each has its own CI check. Forgetting to
re-run one is the failure this script exists to prevent: on 2026-07-25/26 `WebSpa.h` went
two web commits stale, so six consecutive `main` runs were red AND the firmware shipped an
embedded SPA missing the iOS/Bluefy `optionalServices` fix and auto-reconnect. CI caught the
drift; nothing acted on it, because the check only ran after the push.

Run it before you push (see `.githooks/pre-push`, installed by `tools/install-hooks.ps1`):

    python code/scripts/check_generated.py            # verify, non-zero exit on drift
    python code/scripts/check_generated.py --fix      # regenerate in place, then re-verify

Keep this list in sync with the `--check` invocations in `.github/workflows/tests.yml`.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# (label, script path relative to repo root, what it generates)
GENERATORS: list[tuple[str, str, str]] = [
    ("SPA header", "web/gen_spa_header.py", "firmware/lib/proxy/WebSpa.h"),
    ("Bridge wire contract", "code/scripts/gen_bridge.py", "bridge-codec.js + golden + Proto mirrors"),
    ("Web JSON contract", "code/scripts/gen_webjson.py", "WebJson.h + web/index.html mirror"),
    ("Design tokens", "design/gen_tokens.py", "web CSS + WebUi.h + LVGL RGB565"),
]


def run(script: str, fix: bool) -> tuple[bool, str]:
    """Return (ok, output). With fix=False the generator only reports drift."""
    args = [sys.executable, str(REPO / script)] + ([] if fix else ["--check"])
    proc = subprocess.run(args, capture_output=True, text=True, cwd=REPO)
    return proc.returncode == 0, (proc.stdout + proc.stderr).strip()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fix", action="store_true", help="regenerate artifacts in place instead of only reporting")
    args = ap.parse_args()

    if args.fix:
        for label, script, _ in GENERATORS:
            ok, out = run(script, fix=True)
            print(f"[regen] {label}: {'ok' if ok else 'FAILED'}")
            if out:
                print(f"        {out}")

    stale: list[str] = []
    for label, script, artifact in GENERATORS:
        ok, out = run(script, fix=False)
        print(f"[{'ok  ' if ok else 'DRIFT'}] {label:<22} -> {artifact}")
        if not ok:
            stale.append(label)
            if out:
                print(f"        {out}")

    if stale:
        print(
            f"\n{len(stale)} generated artifact(s) stale: {', '.join(stale)}\n"
            "Fix with: python code/scripts/check_generated.py --fix   (then commit the result)",
            file=sys.stderr,
        )
        return 1

    print("\nAll generated artifacts in sync.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
