"""The UI palette must stay single-source.

`design/tokens.json` is the one definition of the colour palette; `design/gen_tokens.py` propagates
it into every frontend (the shared web SPA's CSS `:root`, the ESP32 web CSS in `WebUi.h`, and the
LVGL RGB565 constants in `LcdCanvas.h`). This test runs the generator in `--check` mode so the
generated blocks can never drift from the source — edit `tokens.json`, run the generator, done.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GEN = ROOT / "design" / "gen_tokens.py"


def test_generator_exists() -> None:
    assert GEN.is_file(), f"the token generator is missing: {GEN}"


def test_palette_in_sync() -> None:
    result = subprocess.run(
        [sys.executable, str(GEN), "--check"],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, (
        "UI palette has drifted from design/tokens.json — run `python design/gen_tokens.py`.\n"
        + result.stdout + result.stderr
    )
