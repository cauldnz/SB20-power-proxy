#!/usr/bin/env python3
"""Propagate the single-source UI palette (design/tokens.json) into every frontend.

The same colours are consumed three ways — the shared web SPA (CSS `:root`), the ESP32 web CSS
(a `:root` string literal in C++), and the LVGL on-device UI (RGB565 constants). Hand-maintaining
three copies drifts; this generator makes `design/tokens.json` the one source and rewrites the
`TOKENS-GEN` block in each consumer to match.

    python design/gen_tokens.py           # rewrite the blocks in place
    python design/gen_tokens.py --check    # verify they're in sync (CI); exit 1 on drift

Add a consumer by appending to CONSUMERS below. Each block is delimited by marker lines containing
`TOKENS-GEN:START` / `TOKENS-GEN:END`; everything between them is regenerated, the markers and the
rest of the file are left untouched.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOKENS_JSON = ROOT / "design" / "tokens.json"

START = "TOKENS-GEN:START"
END = "TOKENS-GEN:END"

# token name -> LVGL constant suffix (LcdCanvas.h uses LCD_<SUFFIX>)
LCD_NAMES = {
    "bg": "BG", "card": "CARD", "fg": "FG", "mut": "MUT", "ok": "OK",
    "accent": "ACCENT", "bad": "BAD", "line": "LINE", "chip2": "CHIP", "title": "TITLE",
}


def load_tokens() -> dict[str, str]:
    return json.loads(TOKENS_JSON.read_text(encoding="utf-8"))["tokens"]


def _hex_bytes(hexcolor: str) -> tuple[int, int, int]:
    h = hexcolor.lstrip("#")
    return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


# --- per-consumer renderers: return the inner block (between the marker lines), no markers ----

def render_css_root(tokens: dict[str, str], indent: str = "  ") -> str:
    """A CSS :root{} block for an HTML file (the shared SPA)."""
    decls = " ".join(f"--{k}:{v};" for k, v in tokens.items())
    return f"{indent}:root {{ {decls} }}"


def render_cpp_css_root(tokens: dict[str, str]) -> str:
    """The same :root as a C++ string literal (ESP32 WebUi.h webuiCss())."""
    decls = "".join(f"--{k}:{v};" for k, v in tokens.items())
    return f'        ":root{{{decls}}}"'


def render_lcd_rgb565(tokens: dict[str, str]) -> str:
    lines = []
    for k, suffix in LCD_NAMES.items():
        r, g, b = _hex_bytes(tokens[k])
        name = f"LCD_{suffix}".ljust(10)
        lines.append(f"constexpr uint16_t {name} = lcdRgb(0x{r:02x}, 0x{g:02x}, 0x{b:02x});  // --{k}")
    return "\n".join(lines)


# path relative to ROOT -> renderer
CONSUMERS = [
    ("web/index.html", render_css_root),
    ("firmware/lib/proxy/WebUi.h", render_cpp_css_root),
    ("firmware/lib/proxy/LcdCanvas.h", render_lcd_rgb565),
]


def _splice(text: str, inner: str, path: str) -> str:
    """Replace the content between the START and END marker lines with `inner`."""
    pat = re.compile(
        r"(?P<start>[^\n]*" + re.escape(START) + r"[^\n]*\n)"
        r"(?P<body>.*?)"
        r"(?P<end>[^\n]*" + re.escape(END) + r"[^\n]*)",
        re.DOTALL,
    )
    m = pat.search(text)
    if not m:
        raise SystemExit(f"{path}: no {START}/{END} markers found")
    return text[: m.start("body")] + inner + "\n" + text[m.start("end"):]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true", help="verify sync only; exit 1 on drift")
    args = ap.parse_args()

    tokens = load_tokens()
    drifted = []
    for rel, render in CONSUMERS:
        path = ROOT / rel
        text = path.read_text(encoding="utf-8")
        want = _splice(text, render(tokens), rel)
        if want != text:
            drifted.append(rel)
            if not args.check:
                path.write_text(want, encoding="utf-8", newline="\n")

    if args.check:
        if drifted:
            print("Palette out of sync with design/tokens.json:", ", ".join(drifted))
            print("Run: python design/gen_tokens.py")
            return 1
        print("Palette in sync across", len(CONSUMERS), "consumers.")
        return 0

    print("Wrote palette to", len(CONSUMERS), "consumers"
          + (f" (updated: {', '.join(drifted)})" if drifted else " (already in sync)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
