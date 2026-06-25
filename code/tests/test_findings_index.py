"""The findings/ doc index (`code/findings/README.md`) must stay complete + link-valid.

The **freshness mechanism** for the doc map: every top-level `code/findings/*.md` must be linked
from the index, and every link in the index must resolve. CI fails the moment a doc is added or
renamed without updating the index — so the map can't silently drift stale.

(Session 9: `nrf-sniffer.md` existed but was in no index, so a session rebuilt tooling on wrong
assumptions instead of finding it. This test keeps the index honest.)
"""
from __future__ import annotations

import re
from pathlib import Path

FINDINGS = Path(__file__).resolve().parents[1] / "findings"
INDEX = FINDINGS / "README.md"


def test_index_exists() -> None:
    assert INDEX.is_file(), f"the findings doc index is missing: {INDEX}"


def test_every_findings_doc_is_indexed() -> None:
    """Every top-level findings doc (except the index itself) is linked from README.md."""
    text = INDEX.read_text(encoding="utf-8")
    missing = [
        md.name
        for md in sorted(FINDINGS.glob("*.md"))
        if md.name != "README.md" and f"({md.name})" not in text and f"]({md.name}" not in text
    ]
    assert not missing, (
        "findings/README.md (the doc index) is missing entries for: "
        + ", ".join(missing)
        + " — add a one-line entry (grouped by subsystem) so the next session can find them. "
        "An undiscoverable doc is one we re-derive."
    )


def test_index_has_no_dead_links() -> None:
    """Every relative *.md link in the index resolves to a real file under findings/."""
    text = INDEX.read_text(encoding="utf-8")
    dead = [
        target
        for target in re.findall(r"\]\(([^)#]+\.md)(?:#[^)]*)?\)", text)
        if not target.startswith(("../", "http")) and not (FINDINGS / target).exists()
    ]
    assert not dead, f"findings/README.md links to missing files: {sorted(set(dead))}"
