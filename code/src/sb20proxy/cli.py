"""sb20proxy CLI entry point (stub).

The proxy library itself is not built yet. Phase 0 (diagnostic capture + analysis)
is complete and the build plan is in `code/findings/phase-0-report.md` (§6); the
Phase-0 capture and analysis tools live in `code/scripts/` (run those directly for
now). This stub exists so the declared `sb20proxy` console command resolves with a
helpful message instead of an ImportError, and becomes the real CLI when Phase 1
(static replay) lands.
"""

from __future__ import annotations

import sys


def main() -> int:
    sys.stderr.write(
        "sb20proxy: the proxy is not built yet (Phase 0 complete; Phase 1 = replay is next).\n"
        "  - Current state & next-steps plan: code/findings/phase-0-report.md\n"
        "  - Phase-0 capture/analysis tools:   code/scripts/  (run directly)\n"
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
