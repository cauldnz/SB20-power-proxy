# Repository review synthesis

**Document date:** 2026-09-23 (Australia/Sydney).
**Original reviews:** 2026-07-26.
**Initial reconciliation:** 2026-09-14.
**Repository refresh:** 2026-09-23.
**Status:** Review record and recommendations; not approval to implement or merge.
**Reviewed baseline:** `8383ad3`; July remediation checkpoint: `18c3667`.
**Latest main inspected:** `6d5afa25a09b2a71ad32d240d8e04dde4e59bf2a`
(#320, merged 2026-09-21 Australia/Sydney).

## Verdict

The five reviews agree on a strong pure core and weaker orchestration around hardware.
Their useful recommendation was to expose and test concrete behavioral invariants, not to
rewrite the architecture or optimize file sizes. Much of that work landed on July 26-27.
Treating the original reports as a fresh backlog would repeat completed work and revive
recommendations that subsequent measurement rejected.

Preserve the capture-grounded codecs, shared C++ core, golden-vector contracts, separate
transport adapters, and distinction between desk tests and physical validation.

The [source register](2026-09-23-review-sources.md) attributes the five reviews and records
the limitations of this archive. The living implementation checklist remains
[architecture-remediation.md](../../code/findings/architecture-remediation.md); this dated
document does not replace it.

## Findings reconciled with landed work

| Theme | Original feedback | Reconciled disposition |
|---|---|---|
| Enforcement and generated artifacts | A stale embedded SPA reached main while CI was red; branch protection contradicted the documented policy. | SPA regeneration and a unified generated-artifact gate landed in #294. On September 14, live protection required PR reviews and strict checks. The current main CI run is green. |
| nRF radio orchestration | Role selection and the read/correct/relay path lived inside callbacks, bypassing shared `ProxyCore`. | Peer-role dispatch and `SourceRelay` were extracted and tested in #296, #299 and #312. This does **not** mean the nRF now uses `ProxyCore`: connection adapters and some callback ownership remain inline. |
| ESP32 callback ordering | Pure codec tests did not prove reply-before-zero, coalescing, calibration drain order or disconnect-edge behavior. | #315 added `CpApply.h`, `LoopDrain.h` and an end-to-end host test over captured control-point bytes. The tracker explicitly distinguishes these proofs from unmeasured cross-task races. |
| LCD policy in main.cpp | A large LCD region mixed view building, touch calibration and hardware scheduling. | #308 extracted touch calibration; #317 extracted shared view projections. The remaining hardware/delegation code intentionally stays. The recorded CYD screen sweep for #317 remains pending. |
| HTTP routing | `WifiLink` was wide and repetitive, and route invariants depended on convention. | #314 introduced pure request/response dispatch, method-level POST protection and returned reboot intent. The tracker records zero differences across 57 hardware route behaviors. |
| Configuration persistence | Untagged positional fields and caller-dependent delimiter sanitation could corrupt configuration. | #301 added versioning and sanitation at the serializer. Vocabulary convergence remains low-priority maintenance, not a reason to unify wire formats. |
| Capture tooling and imports | Duplicate decoding and import-time exits weakened the desk tooling. | #295 made dependency guards import-safe; #307 removed the real ANT decoder fork and the copied observer callback, while preserving deliberately different BLE decoders. #316 made capture truncation explicit. |
| Documentation | The front door described an unbuilt Python-first product; sniffer instructions contradicted one another. | #309 rewrote the README, marked historical material and corrected operational instructions. Historical files stayed in place to preserve inbound links. |
| CI and reproducibility | Moving dependencies and incomplete build coverage reduced confidence in shipped firmware. | #300 pinned critical toolchain inputs, added Python 3.14 and shipping C3/CYD compilation. This is not a complete Python dependency lock or automatic coverage of every future board. |
| Smaller correctness and clarity fixes | Analysis routing, spoof-specific constants, Arduino macros and flash selection needed attention. | #303 fixed analysis routing; #304 separated captured Stages values and named the macro workaround; #302/#313 hardened flash selection. These are landed work, not an untouched backlog. |

These dispositions are based on the source tree, commit/PR history and recorded evidence.
Historical hardware results are attributed to the remediation tracker, not represented as
experiments rerun for this document.

## September 23 update: what changed after the first synthesis

The September 14 refresh found no main commits after `18c3667` and no open PRs.
That statement is historical, not the current repository state. The September 23 fetch
succeeded and found six additional commits, including these three merged PRs:

| PR | Change | Relevance to this review |
|---|---|---|
| #319 | Bound the MCP dependency below v2 after the FastMCP API change broke fresh installation. | Concrete follow-through on dependency compatibility; not proof that the whole dependency graph is reproducible. |
| #318 | Added the Guition JC3248W535 head-unit port using the AXS15231B QSPI display. | A new hardware adapter and build target outside the July reviewers' evidence. |
| #320 | Fixed Guition panel initialization, asynchronous DMA strip reuse and TE synchronization. | Reinforces the distinction between correct renderer output and a correct physical display path. |

The latest inspected main run,
[GitHub Actions run 35529401423](https://github.com/cauldnz/SB20-power-proxy/actions/runs/35529401423),
completed successfully for `6d5afa2`. The board findings are recorded in
[guition-board.md](../../code/findings/guition-board.md), including residual bright-content
striping and hardware checks that were still open on main.

### Open work is not a landed fix

[PR #322](https://github.com/cauldnz/SB20-power-proxy/pull/322), at head
`f0bacdce7867fa4a7ada7c38201fbc970750ba23`, was **open** on September 23.
Its description reports:

- End-to-end Guition bench validation over real BLE using a simulated power meter.
- An S3 USB-flash fix to preserve NVS while deliberately resetting OTA boot selection.
- Smaller DMA strip buffers to recover internal heap under simultaneous display/radio/web load.
- A distinct Guition mDNS hostname.

These are PR-author reports, not independently reproduced results in this synthesis.
They must not be marked fixed on main before merge. The PR explicitly excludes a real
SB20/pedal ride and full-panel touch accuracy. Its synthetic navigation taps should not
be treated as proof of physical touch accuracy.

## Remaining priorities

1. **Finish specific behavioral and physical validation gaps.** Evaluate #322 on its own
   evidence, preserve the distinction between simulated-meter bench work and a real ride,
   and close the outstanding display/touch checks explicitly.
2. **Resolve the remaining control-point policy question.** R5b records that the
   Stages-specific crank-length response is also used for non-spoof consumers.
   This requires target-specific protocol and head-unit validation, not a mechanical split.
3. **Guard Garmin's wire contract if Connect IQ remains supported.** R2c still identifies
   Monkey-C as the unguarded mirror. Reuse the existing schema and golden vectors rather
   than rebuilding the JavaScript/C++ parity machinery.
4. **Only extract more nRF connection policy for a concrete payoff.** Role classification
   and relay logic are now testable. Remaining adapters should earn their complexity by
   isolating an actual lifecycle invariant or enabling a needed substitution.
5. **Make remaining enforcement choices explicit.** The September 14 protection read showed
   administrator enforcement disabled and required checks for Python 3.10/3.12, firmware
   and bridge parity, but not Python 3.14. That configuration was not re-read on September 23.
   Decide whether those exceptions are intentional before changing repository settings.

## Recommendations not to carry forward unchanged

- **Merge the BLE capture/runtime decoders:** rejected after examining their contracts.
  Capture preserves all optional fields and damaged records; runtime decoding has a narrower,
  stricter job. Only genuinely shared constants were consolidated.
- **Move every LCD or HTTP function behind a new interface:** replaced by smaller
  behavior-focused extractions. File length alone is not sufficient justification.
- **Move historical documents out of the root:** banners were used instead, preserving
  links from the append-only decision record.
- **Split the large test file before correctness work:** useful opportunistic housekeeping,
  not a substitute for tests of previously untested behavior. No generated test aggregator
  is required merely to use multiple PlatformIO test suites.
- **Add missing JavaScript parity or nRF target compilation:** some reports asserted these
  were absent while others correctly described them. Both already existed; do not re-file.
- **Treat licensing-gated S340 CI or physical testing as generic test failures:** retain
  their explicit constraints. The actionable gap is policy that can be exercised at the desk.

## Evidence boundaries and maintenance

This is a synthesis of reviews and subsequent work, not a new exhaustive code or security audit.
Reviewer agreement is qualitative: no vote totals are asserted because the earlier combined
report's counts do not consistently match the individual reports.

The September 14 branch survey distinguished squash-merged patches from commits merely absent
by ancestry. It identified old `docs/ui-architecture-review` material and the closed, unmerged
LCD WiFi-status PR #255; neither should be silently resurrected. September 23 added a new
active branch for #322. No branches were deleted or other sessions' checkouts modified.

For later updates, append a dated addendum or create a new dated review. Do not silently
rewrite this snapshot to make historical findings appear current. Execution status belongs
in the living remediation checklist and the relevant issue/PR.
