# Diagrams

Mermaid diagram **sources** (`.mmd`, authoritative) and their **rendered** `.svg` live here, side by
side — commit both. See the **Diagrams** section in [`CLAUDE.md`](../../CLAUDE.md) / `AGENTS.md` for the
full convention.

Render locally (never via a cloud service — diagram source can name hosts/topology):

```bash
scripts/render.sh docs/diagrams/<name>.mmd     # -> docs/diagrams/<name>.svg
```

Canonical convention & shared helper: `cauldnz/cauldnz-pos` → `skills/diagrams/`.
