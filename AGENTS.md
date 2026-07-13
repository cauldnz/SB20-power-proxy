# AGENTS.md

This repo's primary agent guidance is in [CLAUDE.md](CLAUDE.md); the shared
conventions below are mirrored here for tools that read `AGENTS.md`.

## Diagrams

Diagrams are **Mermaid text, rendered locally.** Source is authoritative; the image is derived.

- **Source + output live together:** `docs/diagrams/<name>.mmd` → `docs/diagrams/<name>.svg`
  (commit both; reference as `![...](diagrams/<name>.svg)`). Author `.mmd` from the templates in
  the POS `diagrams` skill (flowchart / sequenceDiagram / stateDiagram-v2 / erDiagram).
- **Render offline, never via a cloud service:**
  ```bash
  scripts/render.sh docs/diagrams/<name>.mmd        # -> .svg   (add -f png for PNG)
  ```
  Requires once: `npm i -g @mermaid-js/mermaid-cli && npx puppeteer browsers install chrome-headless-shell`.
- **Privacy:** do **not** send diagram source naming hosts/topology/secrets to `mermaid.ink`,
  `mermaid.live`, or a hosted render MCP. Those are for generic sketches only. When unsure, render locally.
- **GitHub ` ```mermaid ` fenced blocks** are fine inline in Markdown (rendered by GitHub, in-repo).

Canonical convention & shared helper: `cauldnz/cauldnz-pos` → `skills/diagrams/`.
