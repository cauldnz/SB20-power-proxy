"""The captures index (`code/findings/captures/README.md`) must name every committed capture.

The **freshness mechanism** for the canonical lossless record. Captures are load-bearing history
(every codec fixture and every calibration traces back to one), so a capture file with no index row
is evidence nobody will find, and an index row naming a file that is not there is a promise that
sends the next session hunting.

(2026-09-23 review: the index listed 15 of 54 files; the session-13 sniffs, the ride captures and
the session-8/9 evidence logs were all unindexed.)
"""

from __future__ import annotations

import re
from pathlib import Path

CAPTURES = Path(__file__).resolve().parents[1] / "findings" / "captures"
INDEX = CAPTURES / "README.md"
NOT_CAPTURES = {"README.md", ".gitkeep"}
_NAMED = re.compile(r"`([A-Za-z0-9][^`\s]*\.(?:jsonl|pcap|fit|txt|json|gz))`")


def test_index_exists() -> None:
    assert INDEX.is_file(), f"the captures index is missing: {INDEX}"


def test_every_capture_file_is_indexed() -> None:
    """Every file in captures/ (except the index itself) appears in the index as `name`."""
    text = INDEX.read_text(encoding="utf-8")
    missing = [
        f.name
        for f in sorted(CAPTURES.iterdir())
        if f.is_file() and f.name not in NOT_CAPTURES and f"`{f.name}`" not in text
    ]
    assert not missing, (
        "captures/README.md is missing rows for: "
        + ", ".join(missing)
        + " - add a row (file, device, what it is, which session) in the same change as the capture."
    )


def test_index_names_only_real_files() -> None:
    """Every capture filename the index names exists on disk (no stale or misspelt rows)."""
    text = INDEX.read_text(encoding="utf-8")
    dead = sorted({name for name in _NAMED.findall(text) if not (CAPTURES / name).exists()})
    assert not dead, f"captures/README.md names files that do not exist: {dead}"
