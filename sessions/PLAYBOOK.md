# Running a physical (on-bike) session — the playbook

On-bike sessions spend the scarcest resource in this project: **the rider's time and patience**. The
agent directs; the human is the hands and eyes. Treat every session as expensive, and aim for each to be
**better-run than the last**. Three rules above all the detail below:

1. **Never send the human to do something you haven't verified is ready.** Everything desk-testable is
   done and green *first*; the bike is only for what genuinely needs the hardware.
2. **One step at a time, with an explicit pass/fail.** The human should never have to improvise or judge —
   you give one instruction, state what they should see, they narrate, you confirm against the data, next.
3. **Every session leaves a record *and* a retro.** Capture the bytes, write down what happened, and turn
   what went wrong (and right) into a concrete change *before* the next session.

This pairs with CLAUDE.md → *Session plans & the session ledger* (the doc/status/ledger mechanics) and
*Git & branch hygiene*. Read this before directing any session.

---

## 1 · Plan (at the desk, before the rider is involved)

- **Desk-derisk everything first.** Compile, host-tests, software loopback, capture-tool smoke test,
  fixtures from real captures — all green before the session. The rider's time is *not* for catching
  something a `pytest`/compile/loopback would have caught.
- **Pre-stage turnkey.** Firmware built **and flashed and verified on the actual board**; tools
  smoke-tested; every command ready to **paste** (no improvising at the bike); restore values written
  down. The rider should arrive to a session that just runs.
- **Front-load the gates.** Order steps so the **highest-information go/no-go comes first** — while the
  rider is fresh, the battery full, the meter awake. Don't spend patience on low-value steps early; a
  go/no-go that fails early saves the rest of the session.
- **Batch by physical setup.** Group everything that needs the same rig state (a battery pulled, a given
  pairing) so the rig changes as few times as possible. Each battery swap / re-pair is a patience tax.
- **Write each step as Plan**: *goal · exact action · expected result (explicit pass/fail) · what to
  capture.* Mark **must-do vs optional/stretch**, and give an **honest time budget up front** ("~45 min,
  3 must-dos").
- **A fallback per gate.** For each likely failure (pairing rejected, OTA drops, meter bounces), the next
  move is already written — the rider never waits while you think.
- **Bring-list + restore-list.** What the rider must have on hand (battery, tools, the right app), and the
  exact state to restore afterwards — written before they start.

## 2 · Execute (live, you driving)

- **Feed one step; wait for the narrated result; confirm against the data** (`curl` / `/log` / the capture
  file) **before advancing.** Never assume a step passed because it "should have."
- **State the expected result *before* they act** — "you should see `source:connected` and ~200 W; tell me
  what you see." Make pass/fail something the human **observes**, not **interprets**.
- **Record actuals inline as you go** — `✅`/`❌`/`⚠️` + the observed bytes/values/`/log` lines, written
  into the session doc, not just chat (the Plan/Actual rule). Memory is lossy; the rider is busy.
- **Timestamp every step (wall-clock, in the doc).** Note the local time (`HH:MM`) when each section and
  key step *starts*, beside its result — so **actual** durations are recorded, not reconstructed from
  memory. The agent has been an unreliable time estimator (bike *and* desk work); logged actuals are the
  only thing that fixes that. One clock-read per step turns every session into calibration data for the
  planned-vs-actual review (§4). Get a timestamp at the start of the session and at each section boundary.
- **Protect the rig.** Write current state down before changing anything; restore it at the end; confirm
  normal operation before they leave.
- **Know when to stop ratholing.** If a step is stuck, don't loop the rider through blind retries — grab
  the diagnostic (`/log`, a capture), have them pause/hold, and either move to the next item or iterate
  off the captured data at the desk. Blind retries burn patience for zero information.
- **Keep the rider oriented** — "2 of 3 gates done; this next one's the big one." They can't see your plan.

## 3 · Document (immediately after, while it's fresh)

- **Mark the session `✅ DONE` with a one-line Outcome** atop its doc + update the ledger row.
- **Promote durable findings** to `code/findings/decisions.md` (append-only) and **commit every capture**
  to `code/findings/captures/` — canonical and lossless. Interpretation can happen later; the bytes can't
  be re-collected without another trip.
- **Leave the next gate explicit:** what this result unblocks, and what **desk work must precede** the next
  visit (so the next session is also pre-staged turnkey).

## 4 · Retro (the part that compounds — never skip it)

End every session with a short **Retro** block in its doc. This is the mechanism that makes each session
better than the last:

- **What went well** — name it so we keep doing it.
- **What went wrong / was slow / was confusing** — and the **root cause**, not just the symptom.
- **The change** — turn each into a concrete fix to the **process, the run-sheet format, or the tooling**,
  *before* the next session: a new pre-flight check, a flash helper, a clearer step, a captured fallback.
  *A retro item without a resulting change is just a complaint.*
- **Did we reduce future trips?** The best outcome is needing **fewer/shorter** sessions next time — batch
  more, derisk more, capture enough to iterate at the desk instead of on the bike.
- **Planned vs actual — tabulate it from the timestamps.** Lay each section's *planned* budget beside its
  *actual* wall-clock (from the per-step timestamps), note the delta and its cause, and carry those deltas
  into the next session's Plan. The estimate only stops being wrong if the miss is written down; an
  unrecorded over/under just repeats. (This project's running weak spot is the agent's time estimates.)

Suggested stub to drop at the bottom of each session doc:
```
## Retro
- Went well:
- Went wrong / slow / confusing (+ root cause):
- Planned vs actual (per section, from the timestamps; + the delta & why):
- Changes to make before next session (process / run-sheet / tooling):
- Next gate + desk work that must precede it:
```

---

## The human-in-the-loop contract

- The rider's **time and patience are the budget.** Optimise for **maximum learning per minute and per
  unit of frustration** — not for covering the most steps.
- A session that discovers "the firmware was wrong" or "the tool doesn't run" **at the bike** is a process
  failure on the **agent's** side — that belongs to step 1, not the rider's time.
- Default to **fewer, denser, better-prepared sessions**, not more frequent ones. Every trip you can avoid
  by derisking or capturing-for-later at the desk is a win.
