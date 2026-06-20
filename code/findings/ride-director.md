# Ride Director — the steerable session engine

**Status: built (Phases 1–6), desk-complete & host-tested.** The phone web app is the rider's
session interface; an agent monitors and steers it in real time over an HTTP control API; workouts can
be expressed in power zones / %FTP. Built per [`ride-director-uplift-plan.md`](ride-director-uplift-plan.md).
This is the *what it is now* reference; the plan doc is the *how it was built*.

## What it is

`python code/scripts/ride_web.py` serves a small, dependency-free dashboard the rider opens on their
phone. It talks them through a workout (big target watts, countdown, zone) while the meters stream and
the capture logs JSONL. The new capability: the workout is a **live, agent-editable plan**, and a
second client — the **agent** (a Claude Code session, or `ride_control.py`) — reads live state and
steers the plan; edits land on the phone within its ~1 s poll.

```
   rider's phone  ──GET /api/live, /api/workout (poll)──▶  RideServer ◀── meters (capture/replay feed)
        ▲                                                      │
        └── target / zone / banner / timeline update ◀─────────┤
                                                               │
   agent ──POST /api/control/* (steer) · GET /api/control/state (monitor)──┘
```

All of it is pure + host-tested; the only hardware seam is the live meter feed, which has a replay
twin (`--replay`). The whole thing runs and is proven at the desk (`test_ride_e2e.py`).

## The pieces (`code/src/sb20proxy/ride/`)

- **`director.py`** — the pure core. `RidePlan` (mutable, **versioned** segment list; `Workout` stays
  an immutable template loaded via `from_workout`). `Cursor` (active index + `started_at`) pins the
  active block by wall-clock, so a live edit to a *future* segment never retro-shifts the active one.
  `advance_cursor` / `derive_state` are total functions of (plan, cursor, now). `Segment` targets are
  one of absolute `power_w`, `pct_ftp`, or Coggan `zone` (precedence in that order). `RiderProfile`
  {ftp_w, scale} + `COGGAN_ZONES` resolve %FTP/zone → watts and classify watts → zone.
- **`state.py` — `LiveState`** — the live owner (meters, ride clock, plan, cursor, profile, messages,
  hold) behind one lock. Steering methods: `start/stop`, `skip/goto/extend`, `append/insert/replace/
  delete/replace_plan` (each keeps the cursor consistent), `post_message`, `set_hold/clear_hold`,
  `set_profile`. `snapshot()` is the `/api/live` payload.
- **`control.py`** — `apply_control(state, op, body)` is socket-free, so the whole control vocabulary
  is host-tested with a FakeClock. `control_state()` is the agent's rich monitoring view.
- **`server.py`** — stdlib HTTP, no deps. Routes below. Optional `control_token` gates `/api/control/*`.
- **`webapp.py`** — `director_view` / `workout_json` (pure view transforms) + `APP_HTML` (the phone page).
- **`workouts.py`** — `CALIBRATION`, `DEMO` (absolute) + a %FTP/zone library: `sweetspot`,
  `sweetspot2x20`, `endurance` (Z2), `overunder` (threshold over-unders), `vo2` (30/30s), and
  `calgrid` (the meter-vs-meter calibration grid — power spine + cadence rows + coast; feeds
  `09_fit_calibration.py`, run sheet in the session-4 doc §D).

## HTTP surface

Rider/phone (always open):

| Method · path | Purpose |
|---|---|
| `GET /` | the dashboard page |
| `GET /api/live` | meters + director + `message` / `hold` / `erg_setpoint_w` / `profile` (browser polls) |
| `GET /api/workout` | the timeline (targets resolved against the profile) |
| `POST /api/start` · `/api/stop` | the ride clock |

Agent control (gated by `control_token` if set — header `X-Control-Token` or `?token=`):

| Method · path | Body | Effect |
|---|---|---|
| `GET /api/control/state` | — | rich snapshot (live + full plan) |
| `POST /api/control/plan` | `{name?, segments:[seg…]}` | replace the whole plan |
| `POST /api/control/segments` | `{op:append\|insert\|replace\|delete, index?, segment?}` | one-segment edit |
| `POST /api/control/skip` · `/goto` · `/extend` | `{}` / `{index}` / `{seconds}` | cursor controls |
| `POST /api/control/message` | `{text, level?, ttl_s?}` | phone-banner coaching message |
| `POST /api/control/target` | `{power_w?\|pct_ftp?, cadence_rpm?, duration_s?}` or `{clear:true}` | ad-hoc hold override |
| `POST /api/control/profile` | `{ftp_w?, scale?}` | set FTP / scale live |

A `seg` is `{duration_s, label?, power_w?\|pct_ftp?\|zone?, cadence_rpm?, note?}`.

## Driving it

```bash
# serve (desk replay; or --live for the bike). --ftp resolves %FTP/zone targets.
python code/scripts/ride_web.py --replay code/findings/captures/<cap>.jsonl \
    --workout sweetspot --ftp 365 [--control-token SECRET]

# steer from a Claude Code session (or by hand) — see `ride_control.py --help`:
python code/scripts/ride_control.py state            --base http://sb20proxy.local:8080
python code/scripts/ride_control.py message "Settle into 220 W" --level info
python code/scripts/ride_control.py target 250 --cadence 90 --for 120
python code/scripts/ride_control.py target --pct 0.90        # 90% FTP
python code/scripts/ride_control.py skip
python code/scripts/ride_control.py profile --ftp 365
```

## The erg hook (forward link)

Every snapshot carries **`erg_setpoint_w`** = the hold override if one is set, else the active
segment's resolved target. This is the value the (bike-gated) **FTMS *Set Target Power*** path will
write to the SB20 so a Power-Zone workout *auto-sets* the trainer — chase-the-number now, auto-erg
later. See the FTMS plan / `forward-plan.md`. Wiring that write is **gated on the on-bike FTMS capture**
(real-data-first); the Ride Director is ready for it today.

## What's deliberately not here

- **Auto-erg to the SB20** — bike-gated (only the `erg_setpoint_w` hook exists). 
- **On-bike use** — a later bike session; this is the desk build, proven over replay.

(The Coggan zone-workout library — endurance / sweet-spot / over-unders / VO2 — is **built**; see
`workouts.py`. More workouts are easy to add or push live via `POST /api/control/plan`.)
