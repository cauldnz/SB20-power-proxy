"""Tolerant JSONL line iteration, shared across the analysis layer.

The capture/sidecar files are canonical text but can contain a torn final line, a
stray non-UTF-8 byte, or (defensively) a non-object JSON value. Both the SQLite
importer and the annotation sidecar reader walk lines the same way, so the policy
lives here once: decode losslessly (`errors="replace"`), keep going on a bad line,
and surface "couldn't parse this to an object" as a `None` the caller stores
verbatim rather than crashing the whole import.
"""

from __future__ import annotations

import json
from collections.abc import Iterator
from pathlib import Path


def iter_jsonl_lines(text: str) -> Iterator[tuple[str, dict | None]]:
    """Yield ``(stripped_line, parsed_or_None)`` for each non-blank line.

    ``parsed`` is ``None`` for a torn/invalid-JSON line *or* a JSON value that isn't
    an object (a bare array/string/number) — so callers can keep the raw line
    losslessly without an ``AttributeError`` on ``.get``.
    """
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rec = json.loads(line)
        except json.JSONDecodeError:
            yield line, None
            continue
        yield line, rec if isinstance(rec, dict) else None


def read_jsonl_text(path: str | Path) -> str:
    """Read a JSONL file as text, lossily decoding any stray bytes (never raises on
    a bad byte — the JSONL is canonical and one torn write shouldn't abort a build)."""
    return Path(path).read_bytes().decode("utf-8", errors="replace")
