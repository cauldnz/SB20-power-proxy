"""The bench harness must tap where the firmware draws — `bench_ui.more_pitch` vs `LcdLayout.h`.

Background (2026-09-25 bench UI pass): the More rows were laid out at a fixed `y = 36 + 29*k`, which
overran the IP footer on the CYD's ninth row (#364) and stranded ~40% of the Guition's panel (#358).
Fixing the firmware to derive the pitch from the panel height and the row count moves every row —
and `bench_ui.py` computes its own tap targets from the same rule, so a change on one side and not
the other means a walk that silently taps *between* rows and reports a wrong screen as a finding.

So this test does not re-state the rule (that would drift in a third place). It reads the constants
out of `firmware/lib/proxy/LcdLayout.h` and asserts the Python mirror reproduces the C++ for every
panel and row count we ship or plan. The C++ invariants themselves are tested in `test_proxy`.
"""

from __future__ import annotations

import importlib.util
import re
from pathlib import Path

import pytest

_ROOT = Path(__file__).resolve().parents[2]
_SCRIPT = _ROOT / "code" / "scripts" / "bench_ui.py"
_HEADER = _ROOT / "firmware" / "lib" / "proxy" / "LcdLayout.h"
_UI = _ROOT / "firmware" / "src" / "ui" / "LvglUi.cpp"

PANEL_H = (320, 480)          # CYD/S3, Guition
ROW_COUNTS = range(1, 13)     # 8 shipped, 9 on the CYD, headroom for more


def _load_bench_ui():
    spec = importlib.util.spec_from_file_location("bench_ui", _SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _header_constants() -> dict[str, int]:
    """Pull moreLayout's magic numbers straight out of the header."""
    src = _HEADER.read_text(encoding="utf-8")

    def one(pattern: str, what: str) -> int:
        m = re.search(pattern, src)
        assert m, f"LcdLayout.h no longer states {what} in the expected form — update this mirror"
        return int(m.group(1))

    return {
        "nav_h": one(r"constexpr int kNavH = (\d+);", "kNavH"),
        "row_top": one(r"L\.rowTop = (\d+);", "rowTop"),
        "ip_pad": one(r"L\.ipFromBottom = kNavH \+ (\d+);", "the IP caption's pad above the nav"),
        "ip_h": one(r"const int ipH = (\d+);", "the IP caption height"),
        "footer_gap": one(r"const int footerGap = (\d+);", "the gap above the IP caption"),
        "cap": one(r"const int roomy = std::min\((\d+),", "the pitch cap"),
        "design": one(r"const int roomy = std::min\(\d+, std::max\((\d+),", "the design pitch"),
        "divisor": one(r"std::max\(\d+, h / (\d+)\)\);", "the panel-height divisor"),
        "floor": one(r"L\.pitch = std::max\(L\.pitch, (\d+)\);", "the readable pitch floor"),
        "row_min": one(r"L\.rowH = std::max\((\d+), L\.pitch - \d+\);", "the minimum row height"),
        "row_inset": one(r"L\.rowH = std::max\(\d+, L\.pitch - (\d+)\);", "the row-height inset"),
    }


def _cpp_more_layout(h: int, rows: int, k: dict[str, int]) -> tuple[int, int, int]:
    """moreLayout(h, rows) evaluated from the header's own constants."""
    ip_from_bottom = k["nav_h"] + k["ip_pad"]
    footer_top = h - ip_from_bottom - k["ip_h"]
    n = max(1, rows)
    roomy = min(k["cap"], max(k["design"], h // k["divisor"]))
    pitch = max(k["floor"], min(roomy, (footer_top - k["row_top"] - k["footer_gap"]) // n))
    return k["row_top"], pitch, max(k["row_min"], pitch - k["row_inset"])


@pytest.mark.parametrize("h", PANEL_H)
@pytest.mark.parametrize("rows", ROW_COUNTS)
def test_python_mirror_matches_the_firmware_header(h: int, rows: int) -> None:
    mod = _load_bench_ui()
    assert mod.more_pitch(h, rows) == _cpp_more_layout(h, rows, _header_constants())


def _firmware_row_counts() -> tuple[int, int]:
    """(rows in the table, rows flagged CYD-only) read out of LvglUi.cpp's kMoreRows[]."""
    src = _UI.read_text(encoding="utf-8")
    table = re.search(r"kMoreRows\[\] = \{(.*?)\n\};", src, re.S)
    assert table, "LvglUi.cpp no longer declares kMoreRows[] in the expected form"
    rows = re.findall(r"^\s*\{\s*\"[^\"]+\".*?(true|false)\s*\},?\s*$", table.group(1), re.M)
    assert rows, "no rows parsed out of kMoreRows[]"
    return len(rows), sum(r == "true" for r in rows)


def test_row_counts_match_the_firmware_table() -> None:
    """Adding a More row must not leave the walker tapping the old pitch (or off the last row)."""
    mod = _load_bench_ui()
    assert set(mod.MORE_ROWS) == set(mod.PANELS), "MORE_ROWS and PANELS disagree on the boards"
    total, cyd_only = _firmware_row_counts()
    assert mod.MORE_ROWS["cyd"] == total, "the CYD shows every row (resistive: Touch cal too)"
    for board in ("guition", "s3"):
        assert mod.MORE_ROWS[board] == total - cyd_only, f"{board} shows the non-CYD rows only"
    assert cyd_only == 1, "only Touch cal is CYD-only; a second one needs a look at #364's fix"


def test_tap_targets_land_inside_their_row() -> None:
    """The centre of every derived target must fall within the row the firmware draws."""
    mod = _load_bench_ui()
    for board, (w, h) in mod.PANELS.items():
        rows = mod.MORE_ROWS[board]
        top, pitch, row_h = mod.more_pitch(h, rows)
        t = mod.Targets(w, h, board)
        for k in range(rows):
            x, y = t.more_row(k)
            assert 0 < x < w
            assert top + pitch * k <= y < top + pitch * k + row_h, f"{board} row {k} target off-row"
        # ...and the last row must still clear the IP footer, which is what #364 was.
        last_bottom = top + pitch * (rows - 1) + row_h
        assert last_bottom <= h - 34 - 16, f"{board}: row {rows - 1} reaches the IP footer"
