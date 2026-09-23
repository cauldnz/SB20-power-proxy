# Repository reviews — dated snapshots

Each file in this folder is a **dated review**: what the repository looked like at a named commit
and what was concluded from it. A review is never rewritten to look current. Later findings go in
a dated addendum at the end of the file, or in a new dated review. Execution status does not live
here: priorities are in `ROADMAP.md` (repo root), the capability inventory is
[`PROJECT-MAP.md`](../../PROJECT-MAP.md), and every durable decision is in
[`code/findings/decisions.md`](../../code/findings/decisions.md).

| Date | Document | Scope | Baseline |
|---|---|---|---|
| 2026-09-23 | [Review synthesis](2026-09-23-review-synthesis.md) | Reconciles the July 2026 five-model review with the remediation that landed on 07-26/27 and the September work | `8383ad3` → `6d5afa2` |
| 2026-09-23 | [Review source register](2026-09-23-review-sources.md) | Provenance of the five original July reviews and the limits of the archive | — |
| 2026-09-23 | [State of the repo](2026-09-23-state-of-the-repo.md) | Whole-repo audit (docs, code, CI, branches, issues, open work), the north-star reset, and the hygiene performed | `8a64161` audited · `be32624` acted on |
| 2026-09-23 | Appendices: [doc inventory](2026-09-23-state-of-the-repo-doc-inventory.md) · [code inventory](2026-09-23-state-of-the-repo-code-inventory.md) · [open-work register](2026-09-23-state-of-the-repo-open-work-register.md) · [branch triage](2026-09-23-state-of-the-repo-branch-triage.md) | The evidence tables behind the state-of-the-repo review | `8a64161` |

Convention: `YYYY-MM-DD-<topic>.md`, the baseline commit stated in the header, append-only after
merge. A review that proposes work does not become a second backlog: its proposals are copied into
`ROADMAP.md` or an issue, and the review records where they went.
