#!/usr/bin/env bash
# Deploy web/index.html (the canonical shared SPA) to the public GitHub Pages repo.
#
# web/index.html is the source of truth; cauldnz/bike-bridge-web is a deploy target (its index.html
# is overwritten from here). Run from the repo root or web/.  Needs: git + push access to the Pages
# repo (gh auth or a credential helper).
set -euo pipefail

PAGES_REPO="${PAGES_REPO:-https://github.com/cauldnz/bike-bridge-web.git}"
SRC="$(cd "$(dirname "$0")" && pwd)/index.html"
[ -f "$SRC" ] || { echo "no index.html next to deploy.sh ($SRC)"; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
git clone --depth 1 -q "$PAGES_REPO" "$TMP/pages"
cp "$SRC" "$TMP/pages/index.html"

cd "$TMP/pages"
if git diff --quiet; then
  echo "Pages already up to date — nothing to deploy."
  exit 0
fi
git add index.html
git commit -q -m "Deploy web UI from web/index.html ($(git -C "$(dirname "$SRC")/.." rev-parse --short HEAD 2>/dev/null || echo local))"
git push -q origin HEAD
echo "Deployed index.html to $PAGES_REPO"
