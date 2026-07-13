#!/usr/bin/env python3
"""Keep the ESP32 <-> web SPA JSON contract single-sourced (ui-schema/web-json.json).

The board's status/config endpoints (/scan, /config, /curve) are serialized by hand in
firmware/lib/proxy/WebJson.h and parsed by hand in web/index.html. Two hand-maintained copies of the
same field names drift silently: rename a field on one side and the SPA quietly reads `undefined`.

This mirrors the R2 Bridge-parity idea (ui-schema/bridge.json -> gen_bridge.py) but for the web-JSON
mirror. `ui-schema/web-json.json` is the ONE source of the field names; this tool:

    python code/scripts/gen_webjson.py           # regenerate the human reference table (web-json.md)
    python code/scripts/gen_webjson.py --check    # CI: fail if C++, JS, or the doc drifted from the schema

--check verifies three things stay in lock-step with the schema:
  1. C++: each serializer in WebJson.h emits EXACTLY the schema's field names (+ its wrapper key).
  2. JS:  web/index.html references every schema field name (so a rename can't silently orphan a read).
  3. Doc: ui-schema/web-json.md matches the schema (regenerate it after editing the schema).
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCHEMA = ROOT / "ui-schema" / "web-json.json"
WEBJSON_H = ROOT / "firmware" / "lib" / "proxy" / "WebJson.h"
INDEX_HTML = ROOT / "web" / "index.html"
DOC = ROOT / "ui-schema" / "web-json.md"


def load_schema() -> list[dict]:
    return json.loads(SCHEMA.read_text(encoding="utf-8"))["endpoints"]


def cpp_serializer_keys(text: str, serializer: str) -> set[str]:
    """The JSON keys a given `inline std::string <serializer>(...)` emits (from its \"key\": literals)."""
    m = re.search(r"inline std::string\s+" + re.escape(serializer) + r"\s*\(", text)
    if not m:
        raise SystemExit(f"WebJson.h: serializer {serializer}() not found")
    tail = text[m.end():]
    nxt = re.search(r"\ninline std::string\s+\w+\s*\(", tail)
    body = tail[: nxt.start()] if nxt else tail
    return set(re.findall(r'\\"(\w+)\\":', body))


def expected_keys(ep: dict) -> set[str]:
    keys = {f["name"] for f in ep["fields"]}
    if ep.get("wrapper"):
        keys.add(ep["wrapper"])
    return keys


def render_doc(endpoints: list[dict]) -> str:
    lines = [
        "<!-- GENERATED from ui-schema/web-json.json by code/scripts/gen_webjson.py — do not edit by hand -->",
        "# Web JSON contract (ESP32 `/scan` `/config` `/curve` <-> the web SPA)",
        "",
        "The single source is [`web-json.json`](web-json.json); `firmware/lib/proxy/WebJson.h` emits",
        "these and `web/index.html` reads them. CI (`gen_webjson.py --check`) fails if either side drifts.",
        "",
    ]
    for ep in endpoints:
        wrap = f" — items under `{ep['wrapper']}[]`" if ep.get("wrapper") else ""
        lines.append(f"## `{ep['method']} {ep['path']}` ({ep['serializer']}){wrap}")
        lines.append("")
        lines.append(ep["doc"])
        lines.append("")
        lines.append("| field | type | meaning |")
        lines.append("|---|---|---|")
        for f in ep["fields"]:
            lines.append(f"| `{f['name']}` | {f['type']} | {f['doc']} |")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="verify sync only; exit 1 on drift")
    args = ap.parse_args()

    endpoints = load_schema()
    cpp_text = WEBJSON_H.read_text(encoding="utf-8")
    html = INDEX_HTML.read_text(encoding="utf-8")
    problems: list[str] = []

    for ep in endpoints:
        want = expected_keys(ep)
        got = cpp_serializer_keys(cpp_text, ep["serializer"])
        if got != want:
            missing = want - got
            extra = got - want
            problems.append(
                f"C++ {ep['serializer']}(): keys differ from schema"
                + (f"; missing {sorted(missing)}" if missing else "")
                + (f"; extra {sorted(extra)}" if extra else "")
            )
        for name in want:
            if name not in html:
                problems.append(f"JS web/index.html: field '{name}' ({ep['path']}) is never referenced")

    doc_want = render_doc(endpoints)
    doc_have = DOC.read_text(encoding="utf-8") if DOC.exists() else ""

    if args.check:
        if doc_have != doc_want:
            problems.append("ui-schema/web-json.md is stale — run: python code/scripts/gen_webjson.py")
        if problems:
            print("Web JSON contract out of sync with ui-schema/web-json.json:")
            for p in problems:
                print("  -", p)
            return 1
        print(f"Web JSON contract in sync across {len(endpoints)} endpoints (C++, JS, doc).")
        return 0

    # regenerate mode: fix the doc; still report any C++/JS drift (can't auto-fix those)
    DOC.write_text(doc_want, encoding="utf-8", newline="\n")
    print("Wrote", DOC.relative_to(ROOT))
    if problems:
        print("NOTE — C++/JS drift the schema can't auto-fix:")
        for p in problems:
            print("  -", p)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
