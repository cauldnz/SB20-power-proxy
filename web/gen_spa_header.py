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

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "web" / "index.html"
OUT = ROOT / "firmware" / "lib" / "proxy" / "WebSpa.h"
DELIM = "SB20SPA"  # raw-string delimiter; index.html must not contain )SB20SPA"


def build() -> str:
    html = SRC.read_text(encoding="utf-8").replace("\r\n", "\n")  # normalize so the header is stable
    if f"){DELIM}\"" in html:
        raise SystemExit(f"delimiter collision: index.html contains ){DELIM}\"")
    return (
        "#pragma once\n"
        "// GENERATED from web/index.html by web/gen_spa_header.py — DO NOT EDIT BY HAND.\n"
        "// The shared Bike Bridge SPA the ESP32 serves at GET /app (byte-identical to the file the\n"
        "// nRF build serves from GitHub Pages). Regenerate: python web/gen_spa_header.py\n"
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
