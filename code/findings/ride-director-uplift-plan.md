# Ride Director Uplift — overnight autonomous build plan

**Status: PLANNED** · authored 2026-06-20 · turnkey brief for an autonomous overnight session.

## Goal

Turn the Ride Director from a static-workout dashboard into a **steerable session engine**:

- The rider is driven by the **phone web app** — it becomes the human interface during a bike
  session, *replacing direct Claude-Code chat interaction*.
- An **agent** (a Claude Code session with network reach to the ride server) **monitors live state
  and steers the plan in real time** through an HTTP **control API** — push the next block, change
  the target, send a coaching message, skip/extend — and it shows up on the phone within ~1 s.
- The plan model gains **power-zone / %FTP** structure so the director can run Power-Zone workouts.

Today's `ride/` is a clean, pure, **host-tested** core (`RideDirector.state_at(elapsed)` over a
frozen `Workout`, a thread-safe `LiveState`, a stdlib HTTP server + a polling phone dashboard). The
live meter feed is the only hardware seam and it already has a replay twin (`ride_web.py --replay`).
**So this whole uplift is desk-buildable and fully testable with no bike — the ideal autonomous run.**

## Decisions (resolved with the owner — do not re-litigate)

- **Scope tonight:** (1) Agent control API, (2) Dynamic plan engine, (3) Phone UI uplift. The
  **power-zone / %FTP _model_** is built as the substrate (segments can be `%FTP`/zone, resolved via
  an FTP); a full authored **zone-workout _library_** is **stretch** (ship the model + 1–2 examples).
- **Zone model:** **Coggan 7-zone %FTP.** FTP is **configurable** (launch flag + control API), never
  baked in. Primary scale = **SB20 / Stages watts** (the active work); the profile carries `scale`
  so the track-bike/Assioma context reuses it later.
- **Steering model:** **Both** — live in-the-loop (agent edits reflect on the phone within the ~1 s
  poll) **and** author-ahead (agent builds a plan the director then runs autonomously with no live
  agent attached). The API must serve both.
- **Erg-ready:** expose the resolved active target as **`erg_setpoint_w`** in the live + control
  JSON, so the (bike-gated) FTMS *Set Target Power* path can later auto-set the SB20. Display-and-
  chase now, auto-erg later. **No bike dependency tonight.**

## Guardrails (this is an autonomous overnight run — follow exactly)

- **100 % hermetic / desk-only.** No bike, no ANT+ stick, no BLE. Validate over **replay + unit
  tests** only. Never invent hardware or fabricate bytes; the Ride Director needs none.
- **Git hygiene (CLAUDE.md):** `git fetch origin` and cut **each phase's branch fresh from
  up-to-date `origin/main`**. **One phase → one short-lived branch → tests in the _same_ commit →
  push → green CI → _regular_-merge (not squash, so each phase stays independently `git
  revert`-able) → delete the branch.** Never commit to `main` directly. Sync before each new branch
  (other sessions may have merged).
- **Tests in the same commit; `pytest -q` green; `ruff check src tests` clean.** CI is the gate on
  every PR — **never merge red.** (Firmware is untouched here, but CI must still be green.)
- **Behaviour-preserving migration.** Keep the existing contract working at every step — `GET /`,
  `GET /api/live`, `GET /api/workout`, `POST /api/start|stop` keep their shape (the phone keeps
  polling exactly as it does now); **add, don't break.** Existing `test_ride_*` stay green (update
  them deliberately in the same commit when a contract intentionally evolves).
- **Stop conditions.** If a phase can't be made green, **leave it on its branch (do not merge)**,
  record why in the PR, and stop — don't merge broken work, don't push past a red gate, don't start
  a stretch item before the core phases are merged green.
- Keep `decisions.md` (append-only) + `forward-plan.md` updated as work lands; write the short
  feature doc (Phase 6). A finished feature whose decisions never reach `decisions.md` is half-done.

## Architecture

### Dynamic plan model (replaces the frozen `Workout` as the live object)

- `Workout` stays as an **immutable template**; you *load* it into a live, mutable `RidePlan`.
- `RidePlan`: `name: str`, `segments: list[Segment]`, `version: int`. Every mutation **bumps
  `version`** (the phone re-fetches the timeline when the version changes).
- `Segment` gains target-type fields (one-of, resolution order absolute → %FTP → zone):
  `power_w: int | None` (absolute Stages watts, as today) · `pct_ftp: float | None` ·
  `zone: str | None` (`"Z1".."Z7"`). A pure `resolved_power_w(seg, profile) -> int | None`.
- **Cursor + clock** live in `LiveState` (it owns the clock): `active_index: int`,
  `segment_started_at: float | None`. The advance + display logic are **pure functions** so they
  stay host-testable with an injected `now`:
  - `advance_cursor(plan, cursor, now) -> cursor` — when `now - segment_started_at >= active
    duration`, step `active_index += 1` and carry `segment_started_at += duration` (handles
    multi-segment catch-up); finished when `active_index >= len(segments)`.
  - `derive_state(plan, cursor, profile, now) -> DirectorState` — current/next segment, resolved
    target watts + zone label, seg/total elapsed+remaining, `finished`.
  - This cursor model makes "where am I" **robust to live edits of future segments** (editing or
    inserting blocks *ahead* of the cursor never retro-shifts the active one — the flaw a pure
    `sum(durations)` model would have under mid-ride edits).
- Plan mutations (pure, version-bumping): `append`, `insert(i)`, `replace(i)`, `delete(i)`,
  `set_segments(list)`. Cursor controls (in `LiveState`, under its lock): `skip()` (advance now),
  `extend(±s)` (adjust active duration), `goto(i)`, `restart()`.

### Agent control API (`/api/control/*` on the same stdlib server)

Curl-able (so a Claude Code session can drive it with Bash), JSON in/out. Optional auth via
`--control-token` → require header `X-Control-Token` (or `?token=`); **default open on the LAN.**
Every control POST bumps the plan/state version so the phone's existing 1 s poll reflects it.

| Method + path | Body | Effect |
|---|---|---|
| `GET  /api/control/state` | — | Rich snapshot for the agent: plan (version + every segment with **resolved** watts + zone), cursor, live meters, hold-override, latest messages, profile (FTP/scale), `erg_setpoint_w`. |
| `POST /api/control/plan` | `{name, segments[]}` | Replace the whole plan (live swap **or** author-ahead). Segments accept absolute `power_w` or `pct_ftp`/`zone`. |
| `POST /api/control/segments` | `{op:append\|insert\|replace\|delete, index?, segment?}` | Single-segment edit. |
| `POST /api/control/skip` · `/extend` · `/goto` | `{seconds?}` / `{index}` | Cursor controls. |
| `POST /api/control/message` | `{text, level?, ttl_s?}` | Push a coaching message → phone banner. |
| `POST /api/control/target` | `{power_w?\|pct_ftp?, cadence_rpm?, duration_s?}` or `{clear:true}` | Ad-hoc **hold-this** override that supersedes the segment target on the phone (the "hold 250 W until I say stop" path). |
| `POST /api/control/profile` | `{ftp_w?, scale?}` | Set FTP / scale live (re-resolves every %FTP/zone target). |

Keep the request→mutation mapping a **pure function** (`apply_control(state, plan, op) -> result`)
so it's unit-tested without sockets; add a thin integration smoke against the real server on an
ephemeral port. Surface `messages`, `hold` override, and `erg_setpoint_w` in `/api/live` too, so the
existing poll loop carries them.

### Power-zone / FTP model

`RiderProfile{ftp_w: int, scale: str}` (default e.g. `ftp_w=250`, `scale="stages"` — documented,
overridable by `--ftp`/`--scale` and `/api/control/profile`). **Coggan 7-zone** table (Z1 <55 %,
Z2 56–75 %, Z3 76–90 %, Z4 91–105 %, Z5 106–120 %, Z6 121–150 %, Z7 neuromuscular). A pure
`resolve_target(seg, profile)` returns watts (zone → band-midpoint × FTP) and a zone **label** for
the UI. The resolved active target (or the hold-override) is the `erg_setpoint_w`.

### Phone UI uplift (additive to `APP_HTML`, dependency-free, same polling model)

- **Banner** for agent-pushed messages (latest, with level styling + ttl fade).
- **Current zone chip + %FTP** under the big target number.
- **Hold-override** shown distinctly when the agent sets an ad-hoc target.
- Timeline/plan **re-fetches on `version` change** so live edits appear without reload.
- JS isn't unit-tested → its correctness rides on the **JSON contract** (host-tested) + a desk
  replay check; keep changes additive.

## Phases (each: branch → build + tests in the same commit → PR → green CI → regular-merge → delete)

1. **Plan engine.** `RidePlan` + target-type `Segment` fields + pure `advance_cursor` /
   `derive_state` + mutation ops. Migrate `RideDirector`/`LiveState` to the plan+cursor model; keep
   `Workout` as a template loader. Update `test_ride_director.py` / `test_ride_state.py` for the new
   contract and add golden tests for **every** mutation, auto-advance (incl. multi-segment
   catch-up), and edit-future-vs-active-vs-past. *No API/UI yet.*
2. **Control API.** `/api/control/*` + optional token auth + pure `apply_control` + wire into
   `LiveState`/plan + surface `messages`/hold/`erg_setpoint_w` in `/api/live`. Host tests for each
   op + auth; one ephemeral-port integration smoke.
3. **Power-zone / FTP model.** `RiderProfile` + Coggan table + `resolve_target` + segment
   `%FTP`/zone resolution + zone label + `erg_setpoint_w` + `--ftp`/`--scale` + `/api/control/profile`.
   Host tests across all zone boundaries. Re-express DEMO or add **one** zone workout to prove it.
4. **Phone UI uplift.** Banner, zone chip + %FTP, hold-override visual, version-aware re-fetch in
   `APP_HTML`. Verified by the JSON-contract host tests + a replay desk check.
5. **End-to-end desk smoke (hermetic).** A `pytest` that boots `RideServer` on an ephemeral port
   over a committed replay capture, exercises the control API (push plan / message / target / skip),
   and asserts `/api/live` reflects each within a poll — **proves the agent→director→phone loop with
   no bike.** Also wire `ride_web.py` (new flags, plan loading, control surface) + a small
   `scripts/ride_control.py` (or `sb20proxy.ride.control_client`) so a Claude Code session has an
   ergonomic CLI to steer instead of raw curl.
6. **Docs + ledger.** Short feature doc in `code/findings/`; append a `decisions.md` entry (the
   vision + the resolved decisions above + "it works on replay"); add a `forward-plan.md` pointer;
   note the `erg_setpoint_w` hook for the FTMS erg work.

**Stretch (only after 1–6 are merged green):** a fuller Coggan zone-workout library (Z2 endurance,
2×20 sweet-spot, threshold over-unders, VO2 30/30s) authored in %FTP.

## Out of scope / gated (do **not** build tonight)

- **FTMS erg auto-set** (writing *Set Target Power* to the SB20) — **bike-gated**; tonight only
  exposes the `erg_setpoint_w` hook. See the parked FTMS plan.
- **Actual on-bike use** — a later bike session; tonight is the desk build, proven over replay.
- **ESP32 device-discovery/pairing UI** — unrelated backlog.

## Verification (how the run proves itself)

- Per phase: `pytest -q` green + `ruff check src tests` clean + **CI green on the PR** before merge.
- Pure host tests cover every plan mutation, every zone boundary, the control op mapping, and auth.
- The Phase 5 ephemeral-port smoke proves the **full agent→director→phone loop over replay**, no bike.
- Phone UI: JSON contract host-tested; visual confirmed by `ride_web.py --replay` + curling the
  control API (documented desk check, not a CI blocker).
