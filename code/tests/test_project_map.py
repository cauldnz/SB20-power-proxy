"""PROJECT-MAP.md (the top-level repo map) must stay complete + link-valid.

The **freshness mechanism** for the whole-repo map: it must cover the doc areas that *don't* have
their own CI-guarded index — every `beta/*.md`, `docs/**/*.md` and `design/**/*.md` by path (the
`docs/reviews/` folder delegates to its own README) — plus the `sessions/` and `findings/` deep
indexes it points into; every relative link in it must resolve; and its §F lifecycle table must not
call a doc "living" while that doc opens with a ⛔ superseded banner. CI fails the moment any of
those drifts, so the map (and "find it before you build it") can't go stale silently.

(This session: planning loops read only the findings index and missed `beta/`, nearly rebuilding the
already-built collaboration loop. 2026-09-23 review: the map listed two ⛔-banner docs as "living",
claimed a `sessions/` guard that did not exist, and 41 docs were in no index at all.)
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAP = ROOT / "PROJECT-MAP.md"
REVIEWS_INDEX = ROOT / "docs" / "reviews" / "README.md"

CLASSES = ("living", "index", "plan", "session", "historical", "tester", "generated")
_ROW = re.compile(
    r"^\|\s*\[[^\]]*\]\(([^)#]+\.md)\)\s*\|\s*(" + "|".join(CLASSES) + r")\s*\|", re.M
)
SKIP_DIRS = {".git", ".pio", ".venv", "__pycache__", "node_modules", ".ruff_cache", ".pytest_cache"}


def _map_text() -> str:
    return MAP.read_text(encoding="utf-8")


def _lifecycle() -> dict[str, str]:
    """{repo-relative path: class} from the §F table."""
    return dict(_ROW.findall(_map_text()))


def _head(path: Path, lines: int = 8) -> str:
    return "\n".join(path.read_text(encoding="utf-8").splitlines()[:lines])


def test_map_exists() -> None:
    assert MAP.is_file(), f"the top-level project map is missing: {MAP}"


def test_every_beta_doc_is_mapped() -> None:
    """`beta/` has no sub-index, so PROJECT-MAP is its index: every beta/*.md is referenced."""
    text = _map_text()
    missing = [md.name for md in sorted((ROOT / "beta").glob("*.md")) if md.name not in text]
    assert not missing, (
        "PROJECT-MAP.md is missing entries for beta/ docs: "
        + ", ".join(missing)
        + " — add them to §B so the next session finds them (an unmapped doc gets re-derived)."
    )


def test_map_points_to_the_deep_indexes() -> None:
    """The per-area indexes PROJECT-MAP delegates to must be referenced."""
    text = _map_text()
    for index in (
        "code/findings/README.md",
        "sessions/README.md",
        "sessions/PLAYBOOK.md",
        "docs/reviews/README.md",
        "ROADMAP.md",
    ):
        assert index in text, f"PROJECT-MAP.md should point to {index}"


def test_map_has_no_dead_links() -> None:
    """Every relative *.md link in the map resolves to a real file (repo-root-relative paths)."""
    text = _map_text()
    dead = [
        target
        for target in re.findall(r"\]\(([^)#]+\.md)(?:#[^)]*)?\)", text)
        if not target.startswith("http") and not (ROOT / target).exists()
    ]
    assert not dead, f"PROJECT-MAP.md links to missing files: {sorted(set(dead))}"


def test_lifecycle_table_is_present_and_resolves() -> None:
    """§F is machine-readable: enough rows, every one a real file."""
    table = _lifecycle()
    assert len(table) >= 40, f"§F doc-lifecycle table has only {len(table)} rows — was it removed?"
    dead = [p for p in table if not (ROOT / p).exists()]
    assert not dead, f"§F lifecycle rows name files that do not exist: {dead}"


def test_living_docs_are_not_banner_superseded() -> None:
    """A doc classed 'living' must not open with the ⛔ superseded banner."""
    bad = [p for p, cls in _lifecycle().items() if cls == "living" and "⛔" in _head(ROOT / p)]
    assert not bad, (
        "classed 'living' in PROJECT-MAP §F but banner-superseded at the top of the file: "
        + ", ".join(bad)
        + " — reclass it as historical in §F, or remove the banner."
    )


def test_docs_and_design_are_mapped() -> None:
    """Every docs/**/*.md and design/**/*.md is named in PROJECT-MAP (or, for docs/reviews/, in
    the reviews folder's own README), so no doc under those trees is an orphan."""
    map_text = _map_text()
    reviews_text = REVIEWS_INDEX.read_text(encoding="utf-8") if REVIEWS_INDEX.is_file() else ""
    missing: list[str] = []
    for area in ("docs", "design"):
        for md in sorted((ROOT / area).rglob("*.md")):
            rel = md.relative_to(ROOT).as_posix()
            if any(part in SKIP_DIRS for part in md.relative_to(ROOT).parts):
                continue
            if rel.startswith("docs/reviews/"):
                if md.name not in reviews_text and rel not in map_text:
                    missing.append(rel)
            elif rel not in map_text:
                missing.append(rel)
    assert not missing, (
        "docs/ or design/ docs in no index (PROJECT-MAP §B/§F, or docs/reviews/README.md): "
        + ", ".join(missing)
    )
