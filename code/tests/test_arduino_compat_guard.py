"""Guard: the Arduino macro workaround stays in `arduino_compat.h`, with its reasoning.

The Adafruit nRF core defines `abs`/`round`/`min`/`max`/`constrain` as macros, which break the
`std::` calls inside the shared pure headers. The fix is five `#undef`s — trivial to re-copy into
the next source file, and worthless without the paragraph explaining *why* it is needed and why
"just stop using std:: in the pure header" is the wrong fix.

So: the `#undef`s live in exactly one named header. If they reappear loose in a .cpp, this fails
and points at the header instead. Pure text check — no toolchain, no hardware.
"""

from __future__ import annotations

from pathlib import Path

import pytest

# The macros the Arduino core defines that collide with the pure headers' std:: usage.
_GUARDED = ("abs", "round", "min", "max", "constrain")

_REPO = Path(__file__).resolve().parents[2]
_COMPAT = _REPO / "firmware-nrf" / "src" / "arduino_compat.h"

# The one file allowed to contain them, relative to the repo root.
_ALLOWED = {_COMPAT}


def _firmware_sources() -> list[Path]:
    roots = [_REPO / "firmware" / "src", _REPO / "firmware-nrf" / "src",
             _REPO / "firmware" / "lib", _REPO / "firmware-nrf" / "lib"]
    out: list[Path] = []
    for root in roots:
        if root.is_dir():
            out += [p for ext in ("*.h", "*.cpp") for p in root.rglob(ext)]
    return out


def _undef_lines(path: Path) -> list[str]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:  # pragma: no cover - unreadable file is a real failure elsewhere
        return []
    hits = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line.startswith("#undef"):
            continue
        name = line[len("#undef"):].strip().split()[0] if line[len("#undef"):].strip() else ""
        if name in _GUARDED:
            hits.append(line)
    return hits


def test_compat_header_exists_and_undefs_every_guarded_macro():
    assert _COMPAT.is_file(), f"{_COMPAT} is missing — the workaround must have one named home"
    body = _COMPAT.read_text(encoding="utf-8")
    for macro in _GUARDED:
        assert f"#undef {macro}" in body, f"arduino_compat.h no longer undefines `{macro}`"


def test_compat_header_still_explains_itself():
    """A bare list of #undefs is the thing this guard exists to prevent."""
    body = _COMPAT.read_text(encoding="utf-8")
    assert "WHY THIS EXISTS" in body, "arduino_compat.h lost its rationale — that was the point"


@pytest.mark.parametrize("path", _firmware_sources(), ids=lambda p: str(p.name))
def test_no_loose_arduino_undefs_outside_the_compat_header(path):
    if path.resolve() in _ALLOWED:
        pytest.skip("the compat header is the one place these belong")
    hits = _undef_lines(path)
    assert not hits, (
        f"{path.relative_to(_REPO)} undefines an Arduino macro directly ({', '.join(hits)}).\n"
        f"Include \"arduino_compat.h\" before the pure headers instead - it does this and "
        f"explains why."
    )
