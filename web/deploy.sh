#!/usr/bin/env bash
# Deploy web/index.html (the canonical shared SPA) to the public GitHub Pages repo.
#
# web/index.html is the source of truth; cauldnz/bike-bridge-web is a deploy target (its index.html
# is overwritten from here). Run from the repo root or web/.  Needs: git + push access to the Pages
# repo (gh auth or a credential helper).
set -euo pipefail

PAGES_REPO="${PAGES_REPO:-https://github.com/cauldnz/bike-bridge-web.git}"
WEB="$(cd "$(dirname "$0")" && pwd)"
SRC="$WEB/index.html"
[ -f "$SRC" ] || { echo "no index.html next to deploy.sh ($SRC)"; exit 1; }

# index.html is a `<script type="module">` and since the R2a generated-codec change it
# `import`s ./bridge-codec.js. A module whose import 404s does not run AT ALL — no handlers,
# no banner, no log, a dead Connect button — so every ESM dependency MUST ship with it.
# (Deploying index.html alone broke the live site on 2026-07-26; caught on-device.)
ASSETS=(index.html)
while IFS= read -r f; do
  [ -n "$f" ] && ASSETS+=("$f")
done < <(grep -oE 'from "\./[^"]+"' "$SRC" | sed 's/from "\.\///; s/"$//' | sort -u)

for a in "${ASSETS[@]}"; do
  [ -f "$WEB/$a" ] || { echo "deploy aborted: index.html imports '$a' but $WEB/$a is missing"; exit 1; }
done
echo "Deploying: ${ASSETS[*]}"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
git clone --depth 1 -q "$PAGES_REPO" "$TMP/pages"
for a in "${ASSETS[@]}"; do cp "$WEB/$a" "$TMP/pages/$a"; done

cd "$TMP/pages"
if git diff --quiet && [ -z "$(git status --porcelain)" ]; then
  echo "Pages already up to date — nothing to deploy."
  exit 0
fi
git add "${ASSETS[@]}"
git commit -q -m "Deploy web UI from web/index.html ($(git -C "$(dirname "$SRC")/.." rev-parse --short HEAD 2>/dev/null || echo local))"
git push -q origin HEAD
echo "Deployed index.html to $PAGES_REPO"
