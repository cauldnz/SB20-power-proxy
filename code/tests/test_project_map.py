"""PROJECT-MAP.md (the top-level repo map) must stay complete + link-valid.

The **freshness mechanism** for the whole-repo map: it must cover the doc areas that *don't* have
their own CI-guarded index — every `beta/*.md` by name, plus the `sessions/` and `findings/` deep
indexes it points into — and every relative link in it must resolve. CI fails the moment a `beta/`
doc is added without mapping it, so the map (and "find it before you build it") can't drift stale.

(This session: planning loops read only the findings index and missed `beta/`, nearly rebuilding the
already-built collaboration loop. This test keeps the map honest where no other index does.)
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAP = ROOT / "PROJECT-MAP.md"


def test_map_exists() -> None:
    assert MAP.is_file(), f"the top-level project map is missing: {MAP}"


def test_every_beta_doc_is_mapped() -> None:
    """`beta/` has no sub-index, so PROJECT-MAP is its index: every beta/*.md is referenced."""
    text = MAP.read_text(encoding="utf-8")
    missing = [
        md.name
        for md in sorted((ROOT / "beta").glob("*.md"))
        if md.name not in text
    ]
    assert not missing, (
        "PROJECT-MAP.md is missing entries for beta/ docs: "
        + ", ".join(missing)
        + " — add them to §B so the next session finds them (an unmapped doc gets re-derived)."
    )


def test_map_points_to_the_deep_indexes() -> None:
    """The per-area indexes PROJECT-MAP delegates to (findings + sessions) must be referenced."""
    text = MAP.read_text(encoding="utf-8")
    for index in ("code/findings/README.md", "sessions/README.md", "sessions/PLAYBOOK.md"):
        assert index in text, f"PROJECT-MAP.md should point to the {index} index"


def test_map_has_no_dead_links() -> None:
    """Every relative *.md link in the map resolves to a real file (repo-root-relative paths)."""
    text = MAP.read_text(encoding="utf-8")
    dead = [
        target
        for target in re.findall(r"\]\(([^)#]+\.md)(?:#[^)]*)?\)", text)
        if not target.startswith("http") and not (ROOT / target).exists()
    ]
    assert not dead, f"PROJECT-MAP.md links to missing files: {sorted(set(dead))}"
