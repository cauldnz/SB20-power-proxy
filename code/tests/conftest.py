"""Shared pytest scaffolding for the sb20proxy suite.

The suite is HERMETIC: no ANT+ stick, no SB20, no network. Fixtures replay the
real committed captures in `findings/captures/` (real-data-first — fixtures are
built from bytes the actual hardware produced, never invented).
"""

from __future__ import annotations

import json
import sys
from collections.abc import Iterator
from pathlib import Path

import pytest

_CODE = Path(__file__).resolve().parents[1]
_SRC = _CODE / "src"
# Make the package importable when running `pytest` from a fresh checkout that
# hasn't been `pip install -e`'d yet (CI installs it; this is belt-and-braces).
if str(_SRC) not in sys.path:
    sys.path.insert(0, str(_SRC))

CAPTURES_DIR = _CODE / "findings" / "captures"


def iter_capture_pages(filename: str) -> Iterator[tuple[dict, bytes]]:
    """Yield (decoded_dict, raw_8_bytes) for every page in a committed capture.

    decoded_dict is exactly what the capture script's decoder wrote; raw_8_bytes
    is the wire payload (the ext-message tail, if any, is stripped to 8 bytes).
    """
    path = CAPTURES_DIR / filename
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            if rec.get("kind") not in ("broadcast", "acknowledged"):
                continue
            data = rec.get("data") or {}
            raw_hex = data.get("raw_hex")
            if not raw_hex:
                continue
            raw = bytes.fromhex(raw_hex)[:8]
            if len(raw) == 8:
                yield data, raw


@pytest.fixture
def captures_dir() -> Path:
    return CAPTURES_DIR


@pytest.fixture
def capture_pages():
    """Return the iter_capture_pages helper (call it with a capture filename)."""
    return iter_capture_pages
