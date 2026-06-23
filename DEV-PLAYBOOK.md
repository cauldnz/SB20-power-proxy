# The dev loop — how we build here

The desk-engineering companion to [`sessions/PLAYBOOK.md`](sessions/PLAYBOOK.md) (which is for *on-bike*
sessions). This is the **software working agreement**: the patterns that have made sessions productive,
written down so the next one starts where this one left off instead of re-deriving them. It pairs with
CLAUDE.md → *Engineering disciplines* (the invariants) and *Git & branch hygiene* (the mechanics).

**Keep it living.** When a way of working pays off — or a sharp edge costs you time — fold it in here
*before* the session ends. A lesson that isn't written down gets re-learned. (Same ethos as the
on-bike playbook's retro step.)

Three rules above the detail:

1. **Slice big work into small, independently-mergeable PRs — pure core first, hardware seam last.**
2. **Prove it with the cheapest sufficient test, then say exactly what's proven vs still pending.**
3. **At a real fork, ask or plan — don't guess.**

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
