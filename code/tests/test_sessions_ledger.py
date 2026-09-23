"""The session ledger (`sessions/README.md`) must list every session document.

The **freshness mechanism** for the physical-session record. PROJECT-MAP delegates `sessions/` to the
ledger the way it delegates `findings/` to the findings index, so the ledger has to be complete: a
session doc that exists but has no ledger row is a plan or a result nobody will find.

(2026-09-23 review: four session docs — the nRF S340 bring-up, both `CAPTURE-*` runs and the
laptop hand-off — had no ledger row while the map claimed CI enforced `sessions/` coverage.)
"""

from __future__ import annotations

from pathlib import Path

SESSIONS = Path(__file__).resolve().parents[2] / "sessions"
LEDGER = SESSIONS / "README.md"
NOT_SESSIONS = {"README.md", "PLAYBOOK.md"}


def test_ledger_exists() -> None:
    assert LEDGER.is_file(), f"the session ledger is missing: {LEDGER}"


def test_every_session_doc_is_in_the_ledger() -> None:
    """Every `sessions/*.md` except the ledger and the playbook is linked from a ledger row."""
    text = LEDGER.read_text(encoding="utf-8")
    missing = [
        md.name
        for md in sorted(SESSIONS.glob("*.md"))
        if md.name not in NOT_SESSIONS and f"]({md.name}" not in text
    ]
    assert not missing, (
        "sessions/README.md (the ledger) has no linked row for: "
        + ", ".join(missing)
        + " - add a row (number or '-', date, status, link, outcome) so the session is findable."
    )
