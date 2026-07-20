"""The ESP32 <-> web SPA JSON contract must stay single-source.

`ui-schema/web-json.json` is the one definition of the `/scan` `/config` `/curve` `/compare` field
names; `firmware/lib/proxy/WebJson.h` emits them and `web/index.html` reads them. This runs the
checker in `--check` mode so neither side (nor the generated doc) can drift from the schema — the
web-JSON sibling of the Bridge parity guard. See `code/scripts/gen_webjson.py`.
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GEN = ROOT / "code" / "scripts" / "gen_webjson.py"
SCHEMA = ROOT / "ui-schema" / "web-json.json"
WEBJSON_H = ROOT / "firmware" / "lib" / "proxy" / "WebJson.h"


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


def test_closed_world_catches_an_unregistered_serializer() -> None:
    """The guard must be closed-world over WebJson.h: a serializer defined there but absent from the
    schema is precisely how /compare escaped review. Assert every WebJson.h serializer is registered
    — if this fails, a new endpoint shipped without a contract."""
    cpp = WEBJSON_H.read_text("utf-8")
    defined = set(re.findall(r"inline std::string\s+(render\w+Json)\s*\(", cpp))
    registered = {ep["serializer"] for ep in json.loads(SCHEMA.read_text("utf-8"))["endpoints"]}
    missing = sorted(defined - registered)
    assert not missing, f"WebJson.h serializers missing from ui-schema/web-json.json: {missing}"
