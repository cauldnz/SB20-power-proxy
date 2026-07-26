"""Every relative Markdown link in the repo must resolve to a real file.

The **freshness mechanism** for cross-doc navigation. `test_findings_index.py` keeps the findings
index honest and `test_project_map.py` keeps the capability map honest; this closes the remaining
gap — a link *anywhere* that points at a moved, renamed or never-created file.

A dead link is worse than a missing one: it reads as a promise that the answer exists somewhere,
so the next session goes looking instead of asking. This test makes that class of drift impossible
to merge.

(2026-07-27: the front-door README was rewritten after five reviews found it described a product
that no longer existed — "proxy not yet built" while two firmware targets shipped. The rewrite added
~20 new cross-links, and three session-04 links were already silently dead. Hence a guard.)
"""

from __future__ import annotations

import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# Directories that are build output, vendored code, or virtualenvs - not ours to police.
SKIP_DIRS = {
    ".git",
    ".pio",
    ".ruff_cache",
    ".venv",
    "__pycache__",
    "node_modules",
}

_LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")


def _markdown_files() -> list[Path]:
    return sorted(
        md
        for md in REPO.rglob("*.md")
        if not any(part in SKIP_DIRS for part in md.relative_to(REPO).parts)
    )


def _is_checkable(target: str) -> bool:
    """Skip external URLs, pure anchors, and `<placeholder>` paths inside doc examples."""
    if target.startswith(("http://", "https://", "mailto:", "#")):
        return False
    # Templates like `docs/diagrams/<name>.svg` are instructions, not links.
    return "<" not in target and ">" not in target


def test_no_dead_relative_links() -> None:
    dead: list[str] = []
    checked = 0

    for md in _markdown_files():
        try:
            text = md.read_text(encoding="utf-8")
        except UnicodeDecodeError:  # pragma: no cover - defensive
            continue
        for target in _LINK.findall(text):
            if not _is_checkable(target):
                continue
            path = target.split("#", 1)[0].strip()
            if not path:
                continue
            checked += 1
            if not (md.parent / path).exists():
                dead.append(f"{md.relative_to(REPO).as_posix()}  ->  {target}")

    assert checked > 100, (
        f"only {checked} relative links were checked - the scan is probably broken, "
        "which would make this guard silently useless"
    )
    assert not dead, (
        "dead relative Markdown link(s) - each one sends the next session hunting for a file "
        "that isn't there:\n  " + "\n  ".join(dead)
    )
