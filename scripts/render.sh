#!/usr/bin/env bash
# POS diagram renderer — Mermaid (.mmd) -> SVG/PNG, 100% LOCAL / OFFLINE.
#
# Boundary #3/#4: diagrams of POS name hosts/topology/infra. Rendering is done on THIS machine
# via mmdc + headless Chromium. DO NOT swap in a cloud renderer (mermaid.ink / mermaid.live / a
# hosted MCP) — that posts the source to a third party. If you're tempted, stop and ask Chris.
#
# Usage:
#   render.sh <file.mmd | dir> [more...]     # render one file, or every *.mmd in a dir
#   render.sh -f png <target> [...]          # output PNG instead of SVG (default: svg)
#
# Requires (one-time per machine):
#   npm install -g @mermaid-js/mermaid-cli
#   npx puppeteer browsers install chrome-headless-shell
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PCONF="$HERE/puppeteer-config.json"
MCONF="$HERE/mermaid-config.json"
FMT="svg"

usage() { sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

# --- args ---
targets=()
while [ $# -gt 0 ]; do
  case "$1" in
    -f|--format) FMT="${2:-}"; shift 2 ;;
    -h|--help)   usage 0 ;;
    -*)          echo "unknown flag: $1" >&2; usage 1 ;;
    *)           targets+=("$1"); shift ;;
  esac
done
[ "${#targets[@]}" -gt 0 ] || usage 1
case "$FMT" in svg|png) ;; *) echo "format must be svg or png (got: $FMT)" >&2; exit 1 ;; esac

# --- locate mmdc (PATH, or the npm global shim on Windows/mac/linux) ---
if command -v mmdc >/dev/null 2>&1; then
  MMDC="mmdc"
else
  shim="$(npm config get prefix 2>/dev/null)/mmdc"
  [ -x "$shim" ] || shim="$shim.cmd"
  if [ -e "$shim" ]; then MMDC="$shim"; else
    echo "mmdc not found. Install: npm install -g @mermaid-js/mermaid-cli" >&2; exit 127
  fi
fi

render_one() {
  local src="$1" out="${1%.mmd}.$FMT"
  "$MMDC" -i "$src" -o "$out" -p "$PCONF" -c "$MCONF" >/dev/null
  echo "  rendered  $src  ->  $out"
}

for t in "${targets[@]}"; do
  if [ -d "$t" ]; then
    shopt -s nullglob
    files=("$t"/*.mmd)
    shopt -u nullglob
    [ "${#files[@]}" -gt 0 ] || { echo "no .mmd files in $t" >&2; continue; }
    for f in "${files[@]}"; do render_one "$f"; done
  elif [ -f "$t" ]; then
    render_one "$t"
  else
    echo "not found: $t" >&2; exit 1
  fi
done
