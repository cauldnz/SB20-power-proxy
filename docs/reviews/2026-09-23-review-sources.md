# Repository review source register

**Document date:** 2026-09-23 (Australia/Sydney).
**Review cohort date:** 2026-07-26.
**Reports retrieved and read:** 2026-09-14.
**Archive status:** Attributed summaries, not verbatim original reports.

Companion: [dated synthesis and current dispositions](2026-09-23-review-synthesis.md).

## Provenance

Five project sessions independently reviewed architecture, testing, documentation, CI,
developer tooling and repository hygiene. Their shared brief requested concrete evidence,
8-15 findings, a ranked top three, strengths and deliberately unflagged areas.

The July reports and an earlier combined report were read from the coordinating session
`40316992-cd2e-4eed-a671-61ed1a6613c4`, under `files/reviews/`, during the September 14
conversation. When preparing this commit on September 23, that directory was no longer
available at its recorded path. The previously saved temporary transcript export was also
unavailable. Consequently, this register preserves attributed summaries from the material
already retrieved in this conversation; it does not reconstruct files and claim they are
originals.

The session IDs below are provenance identifiers, not repository dependencies. No user's
absolute filesystem paths are needed to read this review.

## Individual reviews

### GLM 5.2

**Review date:** 2026-07-26.
**Session:** `93a682ba-c129-46e4-b0e1-4afe1cb8b0b6`.
**Original report name:** `01-glm-5.2.md`.

Headline: strong pure core and an already-useful remediation plan, but a gap between
written engineering rules and operational enforcement.

Top priorities were stale `WebSpa.h`/red CI, import-time optional-dependency failures,
and the nRF radio seam with host tests. Additional findings included LCD policy,
generator quality gates, config versioning, historical-document signaling and toolchain
repeatability.

Important qualification: its JavaScript parity finding conflicted with already-shipped
schema/codegen work described elsewhere in the same review cohort. It explicitly judged
`WifiLink` largely defensible as thin route wiring, unlike Gemini.

### Kimi k2.7

**Review date:** 2026-07-26.
**Session:** `949bdcd6-a382-40ff-b905-a24bfb52bd67`.
**Original report name:** `02-kimi-k2.7.md`.

Headline: strong at the core, fragile in the hardware orchestration.

Top priorities were nRF radio extraction, zero-reset/calibration callback-chain tests,
and stale SPA/documentation fixes. Other findings covered LCD policy, a wide HTTP hook
interface, config migration, diagnostic commands, dependencies and Monkey-C parity.

The final report acknowledged existing JavaScript parity, real-board CI compilation,
and the explicit S340 licensing constraint. Earlier exploratory session material made
broader unsupported readiness claims; those are not carried forward as conclusions.

### Gemini 3.1 Pro

**Review date:** 2026-07-26.
**Session:** `3569c26e-c6e0-4d42-9a58-3e856d13ea78`.
**Original report name:** `03-gemini-3.1-pro.md`.

Headline: mature core and useful safeguards, with too much responsibility attributed to
hardware integration modules.

Top priorities were extracting HTTP routing, decomposing nRF main.cpp and separating
captive-portal state. It proposed dispatcher and credential-store interfaces, deferred
reboot handling, a unified development environment and a watchdog wrapper.

Several premises were later corrected: much route logic was already pure and tested,
the reboot behavior was intentional, and the report both criticized absent nRF
compilation and praised nRF link guards. Keep the concrete route-invariant opportunity,
not the blanket recommendation for new interfaces.

### GPT-5.6 Sol

**Review date:** 2026-07-26.
**Session:** `46f393a8-4e90-47f1-b4fd-089d7c2f04d8`.
**Original report name:** `04-gpt-5.6-sol.md`.

Headline: unusually good capture-grounded engineering, but process-by-memory no longer
adequate for the repository's scope.

Top priorities were protecting main/restoring green CI, completing nRF radio-policy
extraction, and repairing the documentation front door. It reported live protection
configuration, distinguished adapter tests from pure-core tests, and identified
contradictory sniffer instructions. Other findings included dependency/build coverage,
script logic, configuration vocabulary, Monkey-C parity and release reproducibility.

Its blanket decoder-consolidation recommendation was subsequently narrowed: the ANT
fork was real, but the BLE capture/runtime decoders had intentionally different contracts.

### Minimax M3

**Review date:** 2026-07-26.
**Session:** `26231472-c6e0-44b9-b8de-18c6025de758`.
**Original report name:** `05-minimax-m3.md`.

Headline: strong architecture and evidence discipline, with concentrated organization
and testability problems.

Top priorities were splitting the large firmware test file, decomposing orchestration
files and separating Stages-specific constants from the general CPS codec. It also
identified the ANT decoder fork, Arduino macro workaround, analysis test gaps,
generated-SPA integrity, script organization and decision-log discoverability.

Qualification: its premise that Unity required an aggregate test source was incorrect
for this repository's PlatformIO suite layout. Organizational changes were also described
too confidently as behavior-neutral; extraction still needs evidence.

## Earlier combined report

**Original cohort:** July 2026; exact combined-report creation date not independently verified.
**Retrieved:** 2026-09-14.
**Original report name:** `00-SYNTHESIS.md`.
**Attributed synthesizer:** Claude Opus 5, according to the report itself.

It proposed a 14-item plan: repair enforcement first, then nRF orchestration plus tests,
LCD/HTTP changes, config and Monkey-C parity, script cleanup, documentation,
reproducibility, test organization and firmware safeguards.

It usefully rejected speculative toolchain unification and corrected the test-runner
premise. However, its agreement counts overstated support for some findings, and later
measurement invalidated portions of its proposed decoder and module restructuring.
The companion September synthesis therefore uses named evidence and dispositions,
not the original vote counts or effort estimates.

## Repository evidence used for reconciliation

- [Architecture remediation tracker](../../code/findings/architecture-remediation.md):
  completed slices, intentional deferrals, corrected premises and attributed hardware proof.
- [Project map](../../PROJECT-MAP.md): capability inventory and canonical documentation.
- [Decision log](../../code/findings/decisions.md): append-only measurement history.
- [CI workflow](../../.github/workflows/tests.yml): generated checks, runtime matrix and
  target compilation.
- Commit history from `8383ad3` through `18c3667`, with corresponding merged PR records.
- September 23 fetch through `6d5afa2`, green main CI, merged #318/#319/#320 and open #322.

No security assurance, production-readiness percentage or unmeasured time saving from
the original discussions is adopted as a verified result.
