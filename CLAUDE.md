# CLAUDE.md

Project metadata for [Claude Code](https://claude.ai/code) and similar tools.

## What this project is

An ANT+ man-in-the-middle proxy that lets a Stages SB20 smart bike consume power data from a third-party power meter (primary target: Favero Assioma) as if it came from the bike's native Stages crank power meters. See `README.md` and `01-project-brief.md`.

## Where to start

**Read in this order before doing anything:**

1. `HANDOFF.md` — your role, the first task, and what's not in this package
2. `01-project-brief.md` — goals and success criteria
3. `03-central-hypothesis-and-phase-zero.md` — **most important** — why we don't write proxy code yet
4. `02-technical-context.md` — background on SB20, ANT+ Bike Power, BLE Cycling Power
5. The parent `Research_Content` document in the project files (broader fitness-sensor research)

Then as needed: `04-architecture.md`, `05-implementation-phases.md`, `06-prior-art-and-references.md`, `07-hardware-and-environment.md`, `08-risks-and-gotchas.md`, `09-exploring-captures.md`, `10-relationship-to-QZ.md`.

## Build / install / run

This is a Python project. Layout:

```
code/
├── pyproject.toml               # openant + optional [dev], [ble], [analysis]
├── scripts/                     # one-off scripts (capture, ingest, summarise, diff)
├── src/sb20proxy/               # the actual proxy library — sources/ targets/
├── findings/                    # committed history of captures + analyses
├── docker/                      # InfluxDB + Grafana stack for visualisation
└── grafana/                     # Grafana provisioning + dashboards
```

Setup (Linux / WSL Ubuntu):

```bash
cd code
python3 -m venv .venv && source .venv/bin/activate
pip install -e ".[dev,analysis]"
# udev rule for ANT+ sticks — write directly; openant's `udev_rules` helper
# copies from a relative resources/ path that pip doesn't ship, so it errors.
sudo tee /etc/udev/rules.d/42-ant-usb-sticks.rules >/dev/null <<'RULE'
SUBSYSTEM=="usb", ATTRS{idVendor}=="0fcf", MODE="0666"
RULE
sudo udevadm control --reload-rules && sudo udevadm trigger   # then unplug+replug stick
```

For Windows users, USB passthrough to WSL is required — see `START-HERE.md` and `07-hardware-and-environment.md` §"Windows + WSL".

## Engineering disciplines

A few invariants that this project's success depends on:

- **Capture before code.** Phase 0 (diagnostic capture) is mandatory; the proxy's architecture depends on what it reveals. See `03-central-hypothesis-and-phase-zero.md`.
- **JSONL is the canonical lossless record.** Summaries, diffs, and InfluxDB rows are derived. Never edit a capture; produce a new analysis.
- **Document protocol bytes, not Python idioms.** When findings resolve into a "what we need to spoof" specification, write it as a protocol doc (page formats, byte layouts, calibration response shape) — not as code. The doc ports across languages; the code may not.
- **`findings/decisions.md` is append-only.** Record every numeric value chosen, every hypothesis refuted, every "it works now" moment. Future debugging will rely on it.
- **Test the desk-testable, in the same change.** Any logic that runs without the ANT+ stick or the SB20 — page encode/decode, capture parsing, file replay, `ProxyCore` wiring, calibration-byte construction — ships with `pytest` unit tests in the **same commit** as the code (no "tests later"). Hardware-bound behaviour (radio TX, real pairing) is isolated behind a seam — e.g. an injectable radio — so its logic is still unit-tested with a fake; only the final on-air / pairing check is left to the bench. Build fixtures from the **real committed captures**, never invented bytes (see *Capture before code* and real-data-first). The suite stays hermetic (no hardware, no network) and green; CI runs it on every push. Details in §Validation.
- **MIT-licensed.** Don't copy code from GPL-3.0 prior art (qdomyos-zwift especially). Read, understand, reimplement clean-room.

## Git & branch hygiene (one human, several concurrent Claude sessions)

There is a **single developer** here, but often **multiple Claude sessions sharing this repo at the same time**, with work landing on `main` via PRs between sessions. That combination once produced a painful divergence — a long-lived branch drifted from `main` while other sessions advanced it, then had to be reconciled commit-by-commit, and a capture-grounded fix was almost lost. These rules keep `main` the single source of truth and branches cheap and short-lived:

- **`main` is the source of truth; treat every branch as disposable.** Long-lived feature branches are *the* hazard — they silently diverge from whatever other sessions merged. A branch lives for **one task**, not one "topic."
- **Sync before you touch anything.** At the start of a session/task, `git fetch origin` and check whether `origin/main` (and your current branch) moved. If `origin/main` advanced, recut or rebase onto it **before** working. Never assume the working tree or your branch is current — another session may have merged since.
- **Cut branches fresh from an up-to-date `origin/main`; never resurrect a previous session's branch** — assume it has diverged. If you start on an existing `claude/*` (or any older) branch, `git fetch` and diff it against `origin/main` before adding to it; if it's behind, branch off `origin/main` instead.
- **One task → one short-lived branch → PR → merge → delete, within the session where possible.** Don't leave branches lying around for the next session to trip over. Direct pushes to `main` are blocked by design — PR + green CI is the path — but merge promptly and don't let the branch outlive the task.
- **Concurrent sessions coordinate only through `origin/main`.** Don't assume changes already in the tree are yours (`git status` / `git fetch` first). Avoid two sessions editing the same files; if unavoidable, rebase on `main` often so conflicts stay small.
- **Reconciling divergent work: real data wins, and never silently drop the other line's finding.** When two branches disagree on a captured or numeric value (e.g. the BLE cal-offset `0` vs the ANT+ `903`), **verify against the actual capture in `findings/captures/` before choosing** — a capture-grounded value beats "but it was bike-tested." If both look defensible, ask rather than drop. This is *capture before code* / real-data-first applied to merges. When porting one branch's fix onto another, port the **specific delta**; never take a pre-PR branch wholesale (it will delete newer work).
- **Flash/build only from firmware you've confirmed is current with `origin/main`.** Before flashing the bike, check the source isn't a superseded branch — flashing the wrong build wastes a session and can ship a wrong value (e.g. the ANT+ offset on a BLE crank). The local ESP32 compile is the pre-flash gate (see §Validation).

## Session plans & the session ledger (bike / physical-interaction work)

Any session that touches hardware we can't drive from the desk — a bike session, a flash/pair run, an on-air test — is **planned and recorded in the repo**, with the plan and what actually happened in the **same doc**, and every session tracked in **one ledger**. History stays valuable without leaving a scatter of stale files.

- **One ledger: `sessions/README.md`.** Every physical session is a row there — number, date, status, one-line outcome, link to its doc. Check the ledger to know what's current, what's done, and what each session concluded; nothing else needs scanning to see the state of play.
- **Each session doc is Plan *and* Actual.** It starts as the plan; while guiding the session live, **annotate each step in place** with what actually happened — `✅` pass / `❌` fail / `⚠️` partial, plus the observed bytes / values / UI / `/log` lines. Don't leave the result only in chat; write it back. The plan text stays — Actual is added next to it, not swapped in.
- **Status header, always:** `Status: PLANNED → IN PROGRESS → ✅ DONE (YYYY-MM-DD)` (or `⛔ SUPERSEDED → <successor>`). At the end, flip to DONE and add a one-line **Outcome** at the top.
- **Promote durable findings.** The session doc is the blow-by-blow narrative; lasting facts (a confirmed byte value, "the SB20 ergs off our crank", a refuted hypothesis) are also appended to `findings/decisions.md` (append-only) and captured bytes committed to `findings/captures/`. Those are the canonical record.
- **No detritus.** New session docs live in `sessions/`; completed ones stay there marked DONE (history is valuable — don't delete) and are linked from the ledger. Don't spawn ad-hoc `NEXT-`/`READY-` variants per session — one ledger, one doc per session. (Legacy `BIKE-SESSION-*.md` / `NEXT-BIKE-SESSION.md` stay at the repo root because the append-only `decisions.md` links them; the ledger tracks them in place.)
- **One open session at a time.** Don't draft session N+1 until N is DONE — keeps "what's current" unambiguous.

## Validation

**Unit tests are the standing rule.** They live in `code/tests/`, run with `pytest` from
`code/` (after `pip install -e ".[dev]"`), and are **hermetic** — no ANT+ stick, no SB20, no
network — so they run in CI on every push (`.github/workflows/tests.yml`) and on any laptop.
The conventions:

- **Cover the desk-testable surface in the same commit as the code** — codecs, parsers, sources
  that read files, `ProxyCore` wiring, byte construction (see the *Test the desk-testable*
  discipline above). A change that adds such logic without tests is incomplete.
- **Fixtures come from the real committed captures** in `findings/captures/` (round-trip /
  golden-vector style), not invented bytes. `tests/conftest.py` exposes a `capture_pages` helper
  that iterates the real pages of a capture; reserved/edge byte values were confirmed against the
  captures, not guessed.
- **Isolate hardware behind a seam.** The radio / BLE I/O is the only thing that needs the stick;
  the page-scheduling, calibration, and encode/decode logic must be unit-testable with a fake
  (e.g. a `FakeRadio`). Only the final on-air check is manual.
- **Keep it green and lint-clean.** `pytest` and `ruff check src tests` both pass before a commit.

For changes to capture / analysis scripts: smoke-test against a synthetic JSONL fixture (the diff and summarize tools have been tested this way; see `code/findings/decisions.md` for the manufacturer-ID-69-vs-263 reference fixture).

For changes touching openant API usage: verify against the installed openant version (`pip show openant`) and the actual openant source on disk before assuming an API exists.

For changes to the proxy itself: nothing replaces hardware testing against a real SB20. The bench loopback and SB20 pairing are **manual** steps (documented in `NEXT-BIKE-SESSION.md` and `code/findings/forward-plan.md`); CI covers only the replay-against-fixtures / codec / wiring layer.
