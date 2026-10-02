#!/usr/bin/env python3
"""Generate firmware/lib/proxy/WebSpa.h from web/index.html — the shared SPA the ESP32 serves.

The ESP32 embeds the same index.html the nRF build serves from GitHub Pages, and serves it at
GET /app; over HTTP the page's HttpTransport talks to the board's JSON API (web/HTTP-API.md). This
generator wraps the file in a C++ raw-string accessor. CI (`code/tests/test_spa_sync.py`) runs it in
--check mode so the embedded copy can't drift from web/index.html.

    python web/gen_spa_header.py           # regenerate the header
    python web/gen_spa_header.py --check    # verify it's in sync (CI); exit 1 on drift
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "web" / "index.html"
WEB = ROOT / "web"
OUT = ROOT / "firmware" / "lib" / "proxy" / "WebSpa.h"
DELIM = "SB20SPA"  # raw-string delimiter; index.html must not contain )SB20SPA"

# index.html imports its sibling ES modules (works on GitHub Pages): the GENERATED Bridge codec and the
# ride view's pure model. A board serves the SPA as ONE file from PROGMEM, so there is no sibling to
# fetch — inline each module here. Every `import * as NS from "./x.js";` line is inlined.
IMPORT_RE = re.compile(r'^\s*import \* as (\w+) from "\./([\w-]+\.js)";\s*$', re.MULTILINE)
REQUIRED = "bridge-codec.js"  # the SPA must use the generated codec (architecture-remediation.md R2a)
NOTES = {
    "bridge-codec.js": "(generated from ui-schema/bridge.json)",
    "ride-model.js": "(the ride view's pure model; tested by web/test/ride-model.test.mjs)",
}


def inline_module(ns: str, fname: str) -> str:
    """One module's body, exposed under the same namespace the import gave it."""
    path = WEB / fname
    src = path.read_text(encoding="utf-8").replace("\r\n", "\n")
    names = re.findall(r"^export (?:function|const|class) (\w+)", src, re.MULTILINE)
    if not names:
        raise SystemExit(f"no exports found in {path} — refusing to inline an empty namespace")
    body = re.sub(r"^export ", "", src, flags=re.MULTILINE)
    return (
        f"// --- INLINED web/{fname} {NOTES.get(fname, '')} ---\n"
        "// The Pages copy imports this as a module; a board serves one file, so it is inlined here\n"
        "// by web/gen_spa_header.py. Same source on both hosts — never hand-edit the inlined copy.\n"
        f"const {ns} = (() => {{\n" + body + "\nreturn { " + ", ".join(names) + " };\n})();"
    )


def inline_codec(html: str) -> str:
    """Replace each `import * as NS from "./x.js"` line with x.js's body under the same `NS`.

    Keeps the served page self-contained (and identical in behaviour to the Pages copy) without a
    second HTTP route. Deterministic, so --check still detects drift in ANY input file.
    """
    found = [m.group(2) for m in IMPORT_RE.finditer(html)]
    if REQUIRED not in found:
        raise SystemExit(
            "index.html no longer imports ./bridge-codec.js — the SPA must use the generated codec "
            "(architecture-remediation.md R2a). Update IMPORT_RE here if the import form changed."
        )
    return IMPORT_RE.sub(lambda m: inline_module(m.group(1), m.group(2)), html)


def build() -> str:
    html = SRC.read_text(encoding="utf-8").replace("\r\n", "\n")  # normalize so the header is stable
    html = inline_codec(html)
    if f"){DELIM}\"" in html:
        raise SystemExit(f"delimiter collision: index.html contains ){DELIM}\"")
    return (
        "#pragma once\n"
        "// GENERATED from web/index.html by web/gen_spa_header.py — DO NOT EDIT BY HAND.\n"
        "// The shared Bike Bridge SPA the ESP32 serves at GET /app. Same source as the copy served\n"
        "// from GitHub Pages, except its sibling modules (web/bridge-codec.js, generated from\n"
        "// ui-schema/bridge.json, and web/ride-model.js) are INLINED here, since a board serves a\n"
        "// single file with no sibling module to fetch.\n"
        "// Regenerate: python web/gen_spa_header.py\n"
        "namespace sb20proxy {\n"
        f"inline const char* webSpaHtml() {{ return R\"{DELIM}(\n"
        + html
        + f"){DELIM}\"; }}\n"
        "}  // namespace sb20proxy\n"
    )


def main() -> int:
    check = "--check" in sys.argv
    want = build()
    have = OUT.read_text(encoding="utf-8") if OUT.exists() else ""  # text mode normalizes CRLF
    if check:
        if want != have:
            print("WebSpa.h is stale vs web/index.html — run: python web/gen_spa_header.py")
            return 1
        print("WebSpa.h in sync with web/index.html")
        return 0
    OUT.write_text(want, encoding="utf-8", newline="\n")
    print(f"wrote {OUT} ({len(want)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
