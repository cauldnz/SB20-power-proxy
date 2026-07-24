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
CODEC = ROOT / "web" / "bridge-codec.js"
OUT = ROOT / "firmware" / "lib" / "proxy" / "WebSpa.h"
DELIM = "SB20SPA"  # raw-string delimiter; index.html must not contain )SB20SPA"

# index.html imports the GENERATED Bridge codec as a sibling ES module (works on GitHub Pages). A board
# serves the SPA as ONE file from PROGMEM, so there is no sibling to fetch — inline the module here.
IMPORT_RE = re.compile(r'^\s*import \* as BC from "\./bridge-codec\.js";\s*$', re.MULTILINE)


def inline_codec(html: str) -> str:
    """Replace the `import * as BC` line with the codec's body, exposed as the same `BC` namespace.

    Keeps the served page self-contained (and identical in behaviour to the Pages copy) without a
    second HTTP route. Deterministic, so --check still detects drift in EITHER input file.
    """
    if not IMPORT_RE.search(html):
        raise SystemExit(
            "index.html no longer imports ./bridge-codec.js — the SPA must use the generated codec "
            "(architecture-remediation.md R2a). Update IMPORT_RE here if the import form changed."
        )
    src = CODEC.read_text(encoding="utf-8").replace("\r\n", "\n")
    names = re.findall(r"^export (?:function|const) (\w+)", src, re.MULTILINE)
    if not names:
        raise SystemExit(f"no exports found in {CODEC} — refusing to inline an empty namespace")
    body = re.sub(r"^export ", "", src, flags=re.MULTILINE)
    inlined = (
        "// --- INLINED web/bridge-codec.js (generated from ui-schema/bridge.json) ---\n"
        "// The Pages copy imports this as a module; a board serves one file, so it is inlined here\n"
        "// by web/gen_spa_header.py. Same source, same golden vectors — never hand-edit either.\n"
        "const BC = (() => {\n" + body + "\nreturn { " + ", ".join(names) + " };\n})();"
    )
    return IMPORT_RE.sub(lambda _: inlined, html, count=1)


def build() -> str:
    html = SRC.read_text(encoding="utf-8").replace("\r\n", "\n")  # normalize so the header is stable
    html = inline_codec(html)
    if f"){DELIM}\"" in html:
        raise SystemExit(f"delimiter collision: index.html contains ){DELIM}\"")
    return (
        "#pragma once\n"
        "// GENERATED from web/index.html by web/gen_spa_header.py — DO NOT EDIT BY HAND.\n"
        "// The shared Bike Bridge SPA the ESP32 serves at GET /app. Same source as the copy served\n"
        "// from GitHub Pages, except web/bridge-codec.js (itself generated from ui-schema/bridge.json)\n"
        "// is INLINED here, since a board serves a single file with no sibling module to fetch.\n"
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
