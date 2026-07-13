"""The ESP32 <-> web SPA JSON contract must stay single-source.

`ui-schema/web-json.json` is the one definition of the `/scan` `/config` `/curve` field names;
`firmware/lib/proxy/WebJson.h` emits them and `web/index.html` reads them. This runs the checker in
`--check` mode so neither side (nor the generated reference doc) can drift from the schema — the
web-JSON sibling of the Bridge parity guard. See `code/scripts/gen_webjson.py`.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GEN = ROOT / "code" / "scripts" / "gen_webjson.py"


def test_generator_exists() -> None:
    assert GEN.is_file(), f"the web-json checker is missing: {GEN}"


def test_web_json_contract_in_sync() -> None:
    result = subprocess.run(
        [sys.executable, str(GEN), "--check"],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, (
        "Web JSON contract has drifted from ui-schema/web-json.json — "
        "run `python code/scripts/gen_webjson.py` and reconcile WebJson.h / web/index.html.\n"
        + result.stdout
        + result.stderr
    )
