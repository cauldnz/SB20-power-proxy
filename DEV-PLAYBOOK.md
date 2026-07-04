# The dev loop — how we build here

The desk-engineering companion to [`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md) (which is for *on-bike*
sessions). This is the **software working agreement**: the patterns that have made sessions productive,
written down so the next one starts where this one left off instead of re-deriving them. It pairs with
CLAUDE.md → *Engineering disciplines* (the invariants) and *Git & branch hygiene* (the mechanics).

**Keep it living.** When a way of working pays off — or a sharp edge costs you time — fold it in here
*before* the session ends. A lesson that isn't written down gets re-learned. (Same ethos as the
on-bike playbook's retro step.)

Four rules above the detail:

1. **Slice big work into small, independently-mergeable PRs — pure core first, hardware seam last.**
2. **Prove it with the cheapest sufficient test, then say exactly what's proven vs still pending.**
3. **At a real fork, ask or plan — don't guess.**
4. **Harden what you shipped before you build on it** — green CI doesn't cover the untested live seam (§6).

---

## 1 · Slice the work — pure core first, seam last

A big feature is a *sequence* of small PRs, each one coherent, host-tested in the same commit, green on
CI, and merged the same session. Don't open a 10-file branch and hope.

- **Pure-core-first / seam-isolation.** Put the logic in a header-only / pure module that compiles and
  host-tests with **no hardware**; keep the radio / HTTP / NVS behind a thin seam. Land the pure core
  CI-covered, *then* wire the seam in a later PR. *(The meter-to-meter corrector went `CalibrationFit`
  → `CalibrationSession` → `CalibrationPage` — each a pure, host-tested PR — before any `main.cpp`
  wiring. The wiring PR was then small and low-risk.)*
- **Each PR is one reviewable step.** M1 fit-core, M2 identity, M3 dual-central refactor, M4 wizard, M5
  docs. If a PR is getting large or mixes "pure logic" with "hardware seam," split it (M4 split into
  the pure page `M4b-i` and the live wiring `M4b-ii`).
- **The real-target compile is a gate, not an afterthought.** Before flashing *and* before merging a
  firmware change, compile the actual env (`pio run -e esp32c3-oled-live`) — `pio test -e native` does
  **not** build `main.cpp` or the BLE seam. The host tests catch logic; the target compile catches the
  wiring.

## 2 · Prove it — cheapest sufficient test, honest about scope

- **Host-test every pure bit in the same commit** (`pio test -e native` / `pytest`). This is non-
  negotiable and already a CLAUDE.md invariant — the slicing above exists *so that* most of a feature
  is host-testable.
- **Parity-lock cross-language twins.** When the same logic lives in two languages (C++ firmware ⇄
  Python desk), pin them to **one shared golden dataset asserted on both sides**, so neither drifts
  silently. *(The on-device fit and `calibration.fit_grid` share the same pairs + expected breakpoints,
  asserted in both `test_main.cpp` and `test_calibration_parity.py`.)*
- **Bench-prove integration with a tracer.** Drive the device with a known, distinctive value so the
  read-back unambiguously identifies the path — and so two identically-named boards/sources stay
  distinguishable. *(A steady `fake_meter` 200 W → read 220 W back confirmed the 1.10× corrector end to
  end; the steady value also told us which of two "Stages 62144" boards was the one under test.)*
- **Regression-verify a path you refactor.** A change that touches a *proven* path isn't done until you
  re-confirm the old behaviour. *(M3 generalised the BLE central from a singleton to N instances — we
  re-flashed COM10 and confirmed the spoof still relayed 175 W before adding the second meter.)*
- **"Built" and "validated" are different claims — never conflate them.** "Compiles + host-tested" is
  not "works on hardware." State which you have, and when something is hardware-gated, say so plainly
  *and* say what would prove it (e.g. "the live two-meter walk-through + coex check needs the board on
  WiFi + both real meters — that's the calibration ride"). Defer honestly; don't imply done.
- **Host-testing a parser ≠ testing the route that feeds it.** A whole class of bugs lives in the thin
  glue between the framework and your pure code — green CI sails right past it. The form-POST bug (the
  routes read `arg("plain")`, empty for a browser's urlencoded body) is the canonical example: the
  parser had passing tests; the *live route* was never hit. When you add an HTTP route / CLI / file
  handler, exercise it end-to-end against a real board at least once — `code/scripts/route_smoke.py
  --ip <board>` does this for the device's routes (incl. POSTing a real urlencoded form and confirming
  `/diag` reflects it), so that regression can't recur silently.
- **Test the seam the code will actually run through — not just its pure core.** The async / IO / hardware
  wiring is its own bug habitat that a pure-core test sails right past (the async sibling of the form-POST bug
  above). *(Session 9: the FTMS workout driver shipped with passing **segment-builder** tests but bombed on the
  bike — it called the **synchronous** `ftms_erg.drive()` with an **async** bleak transport, so the transport
  coroutine was never awaited. The fix shipped with `test_async_pump_converges_against_the_twin`, which drives
  `_pump` against the in-process FTMS twin over an async transport — exactly the path that broke. For an
  agent-built or seam-crossing module, the test MUST exercise the seam against the twin, not just pure functions.)*

## 3 · Decide at the right altitude — ask or plan, don't guess

- **A big or multi-approach feature → `EnterPlanMode`** first: research, then a phased plan the human
  approves before any code. *(The corrector was planned into M1–M5 and approved up front.)*
- **A genuine architecture fork → `AskUserQuestion`.** Where two designs both work and the choice is the
  owner's (mode toggle vs separate build; on-device vs desk fit; reboot-into-calibration vs live
  re-pointing; coverage-guided vs auto-finish; fixed vs editable device name) — ask, with a
  recommendation, rather than picking blind. The answers shape the build; guessing wastes a PR.
- **Otherwise pick the obvious default and proceed**, and *say* you did. Don't stop to ask about choices
  with a conventional answer or facts you can check in the code.
- **Checkpoint after a long autonomous run** at a natural seam (a feature's core proven, the rest
  hardware-gated). Summarise proven-vs-pending and let the human steer the next, riskier or
  validation-gated phase — especially when the next step needs hardware they don't have right now.

## 4 · Git rhythm (the mechanics that kept ~15 PRs collision-free)

One task → **fresh branch off `origin/main`** → PR → **wait for green CI** → `git fetch` + **survey all
open PRs** + confirm `origin/main` hasn't moved under the PR → `gh pr merge --merge --delete-branch` →
`git pull` → prune the local branch. Never resurrect an old branch; never merge on a stale base. (This
is CLAUDE.md → *Git & branch hygiene*; it works — follow it every time, including for docs PRs.)

- **A spawned task or concurrent session can leave work in the *shared working tree* — survey before you commit,
  and reconcile rather than stomp or duplicate.** `git status` + `git branch -r` + `gh pr list` before each
  commit/merge. *(Session 9: a spawned doctor-hardening task's changes turned up **uncommitted** in the tree
  with no branch/PR — validated + adopted them in one PR; meanwhile a concurrent session independently improved
  the same `nrf-sniffer.md` on its own branch (#159). The survey caught both, so each landed once — not lost,
  not doubled. Stage explicit file lists, never `git add -A`, when the tree may hold another session's work.)*
- **Spawn a desk session into its OWN git worktree, not the shared checkout** (`git worktree add
  ../sb20-<task> -b <branch>`, or the harness's worktree isolation). One session → one worktree → one branch
  → one PR. This is the **proactive** fix for the shared-tree collisions the bullet above cleans up
  *reactively*: a separate worktree gives each session its own HEAD + index + working files, so a concurrent
  session can't switch your branch under you, change a file mid-edit, or commit your work under a name you
  didn't pick. **Two caveats it does *not* solve:** (a) the dev venvs are repo-local + gitignored
  (`code/.venv`, `firmware/.venv`), so a fresh worktree needs `tools\provision-dev-env.ps1` before
  build/test/`doctor.ps1` build-checks pass — the heavy ESP32 toolchain in `~/.platformio` is shared, so it's
  only the two pip venvs to rebuild; (b) worktrees isolate **git + files only** — the one ESP32 board, the
  ANT+ stick / `usbipd` WSL attach, the nRF dongle, and Infisical creds are machine-global, so
  on-bike/hardware sessions still **serialize on the one rig** regardless. So: worktree-per-session for
  *desk* concurrency; the survey-and-reconcile bullet above stays the backstop. *(Session 9: the spawned
  doctor-hardening task ran in the shared checkout — its branch was switched under it, a file changed beneath
  a mid-edit, and its work was committed under a branch it didn't choose (#158); an isolated worktree
  prevents all three.)*

## 5 · Environment gotchas (so the next session doesn't re-find them)

- **`pio test -e native` needs gcc/g++ on PATH.** The WinLibs install is at
  `~/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_*/mingw64/bin` — prepend
  it for the test command.
- **Flash the spare board only.** COM10 = spare (no-OLED); **COM9 = the OLED bike board — don't flash
  it.** `code/scripts/flash_c3.py` is the hang-resistant flasher (esptool 4.11, time-bounded + retried);
  a wedge needs a physical BOOT/RESET tap. Match the env to the board (don't flash an `-oled` build to
  the no-OLED spare). Run BLE scripts under the venv: `code/.venv/Scripts/python.exe`.
- **One PC BLE radio can be a `fake_meter` peripheral *and* a `qa_board`/bleak central at once** — the
  earlier "the radio is blind to its own adverts" confusion was a different issue. Two *notifying*
  sources, though, need two real meters (or two advertisers) — the desk has one `fake_meter`.
- **Keep printed/curled text ASCII.** The Windows console is cp1252; emoji in anything that gets
  `print`ed or served raises `UnicodeEncodeError`. *(The QA acceptance card render was made ASCII-only
  after exactly this.)*
- **Background a long `fake_meter`/soak with a `--duration`** so it self-terminates and frees the radio;
  poll its log for `subs=1` to confirm the board connected before reading back.
- **Leave the hardware in a known-good state after bench work.** Reset config to defaults and flash the
  *current* build on every board you touched, so a run-sheet's "both boards on current firmware, clean
  config" stays *true*. A board left mid-experiment (a renamed spoof, a stale build) silently breaks the
  next session's assumptions. *(This session left a board one build behind; reflashing it kept the
  session-5 run-sheet honest.)*
- **Before building tooling — or making an "X isn't ready" call — for a subsystem, read its canonical findings
  doc first.** `code/findings/` is the source of truth (`nrf-sniffer.md`, `ftms-protocol.md`, the `*-protocol.md`
  set). *(Session 9: both I and a spawned doctor-hardening task built the nRF capture-rig gate — plus a wrong
  "install Npcap" remediation — on assumptions, never reading `nrf-sniffer.md`, which says the path is the
  headless `sniff_ble.py` + Nordic `SnifferAPI` over serial, NOT Npcap/tshark. The gate checked the wrong thing
  and shipped (#158); #160 corrected it. The doc would have saved the whole detour — the owner had to ask
  "did you read it?" to surface it.)*


- **Never edit the tree while a `pio` build runs** (incl. `git checkout`): the compiler reads files
  mid-swap and the build fails weirdly — twice in one day (2026-07-04/05). Kill background builds
  before editing; treat "build in flight" as a lock on the tree.
- **Compile the CI link-guard env (`esp32c3-supermini`) locally before pushing firmware changes** —
  it has no WiFi/LCD/OLED, so guards differ; PR #212 merged red because only the richer envs were
  compiled locally (and the `firmware` job isn't a required check yet).
- **The WinRT `fake_meter` stops advertising after any central disconnects and the process dies on
  radio hiccups** (WLAN/BT combo card shares the antenna — joining a board's setup AP can kill it).
  Just restart the script; treat it as disposable.
- **espota/OTA debugging order:** confirm the board logs `[ota] ... enabled` via `/log`, then watch
  `/log` while sending one invitation — "no new lines" = the UDP never reached the app (S3/pioarduino
  is currently deaf to OTA invites; USB-flash it and retest on each new build).

## 6 · Hunt bugs adversarially in code you just shipped — before you build on it

Green CI means *the tests you wrote* pass; it says nothing about the bugs you didn't think to test —
and a just-built feature's **untested live seam** (HTTP routes, NVS round-trips, cross-task state,
hardware wiring) is where they hide. So when a feature lands, do a hardening pass *before* extending
it. **Harden before you extend** — when the ask is "do all four," sequence the bug-hunt first.

- **Fan out parallel review agents, one per focused slice** (the wiring · the protocol/serialization ·
  the concurrency · the pure logic) — not one agent over everything. Give each the feature context, the
  file list, and a known example of the bug class ("the form-POST routes read `arg(\"plain\")`, empty for
  a browser body — find more like it"). Tell them to be **skeptical, assume bugs exist**, and report
  `file:line · severity · concrete trigger · fix`, most-serious first.
- **A good review retracts its own false alarms.** An agent that chases a hypothesis, finds it's
  actually fine, and *says so* is doing it right — that's signal its other findings are weighed, not
  padded. (This pass flagged a `clientCb_` "leak" and a millis() "wraparound bug" that were both benign
  on inspection — alongside 11 that were real.)
- **You triage; the agents don't fix.** Dedup overlapping findings, rank by severity × ride-relevance,
  **verify each before fixing** (some are false positives), and *defer* the ones pre-existing in a proven
  path or merely cosmetic — say so explicitly rather than destabilising working code right before a ride.
- **Close the loop:** fix the real ones in one cohesive PR (host-test the pure fixes); and where a bug
  was a whole *class* (the form-POST body), build the guard that stops it recurring (→ §2, `route_smoke`)
  so the next regression fails loudly instead of silently.

*(This session: a 4-agent review of the just-shipped corrector surfaced 11 real bugs — a silently
"Saved" 1.0× passthrough, a name that corrupted the NVS line, a data race on the calibration buffer —
none of which CI saw, all fixed + hardware-verified before the ride.)*
