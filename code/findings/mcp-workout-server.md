# MCP workout server — drive the SB20 erg as agent tools

**Status: ✅ desk-complete (2026-06-26), on-bike drive pending.** The productized form of the
session-9 ad-hoc `ftms_workout.py` driver (forward-plan.md §13). Exposes the SB20's erg as
Model-Context-Protocol tools so Claude (or any MCP client) can **compose a structured workout and
drive it live** on the bike over FTMS — the "agent directs the human" inversion-of-control pattern.

Governs: `code/src/sb20proxy/workout/` (the spec builder + session core), `code/src/sb20proxy/mcp/`
(the driver loop + the FastMCP server), and `code/scripts/mcp_workout_server.py` (the entry point).
Builds on the validated FTMS infra — [`ftms-protocol.md`](ftms-protocol.md), `ble/ftms.py`,
`ble/ftms_erg.py`, and the Ride Director (`ride/`).

## What it is

Two layers, both **host-tested with no bike**, plus a thin hardware seam:

- **`sb20proxy.workout`** — the desk core (no MCP dependency):
  - `builder.build_plan(spec)` turns a workout *spec* into a `ride.director.RidePlan`. Three forms:
    a **built-in name** (`ride.workouts.WORKOUTS`), a **structured** dict (`{segments:[...]}` with
    `repeat` nodes; leaf segments validated by `ride.control.segment_from_json`), or a **shorthand**
    string — `"5min @ 130W; 6x(90s @ 430W; 3min @ 100W); 2min @ 100W"` (targets `W`/`%`/`Z4`/`coast`;
    durations `90s`/`2min`/`1h`/`1:30`/bare-seconds).
  - `session.WorkoutSession` owns a live `ride.state.LiveState` and exposes the agent verbs
    (build / list / start / stop / skip / goto / extend / set_target / message / set_profile /
    status). The erg setpoint it chases is `LiveState`'s `erg_setpoint_w` (active-segment target or
    an ad-hoc hold), so every steering verb moves the erg for free.
- **`sb20proxy.mcp.driver.ErgDriver`** — the async background drive loop (no MCP dependency): pumps
  an `ble.ftms_erg.ErgController` toward the live setpoint each `poll_s`, and — **the safety
  invariant** — ALWAYS sends FTMS **Reset** (resistance → neutral) on stop / duration-end / error.
  This is the teardown `FtmsErgSession` lacks; the bike is never left grinding at target.
- **`sb20proxy.mcp.server.build_server`** — the FastMCP server wiring the above as tools, plus the
  **transport provider** seam: by default a bleak connection to the SB20 (the only un-host-tested
  code); injectable, so the drive tools are tested against the in-process FTMS twin.

## Tools

`list_workouts` · `build_workout(spec|segments, name?, start?)` · `start` · `stop` · `skip` ·
`goto(index)` · `extend(seconds)` · `set_target(power_w?|pct_ftp?, cadence_rpm?, duration_s?, clear?)`
· `message(text, level?, ttl_s?)` · `set_profile(ftp_w?, scale?)` · `status` ·
`start_drive(address?, poll_s?)` · `stop_drive` · `drive_status`.

## Run it

The MCP SDK is the optional **`[mcp]`** extra (the workout core + driver need none of it):

```bash
cd code
pip install -e ".[mcp,ble]"          # ble = bleak, for the on-bike drive
python scripts/mcp_workout_server.py --ftp 250
```

Register that command with an MCP client (Claude Desktop / Claude Code). Then, in chat:
*"Build a 6×90 s @ 430 W workout with 3 min recovery, start it, and start_drive."* Monitor with
`status`; steer with `skip` / `extend` / `set_target`. **Always `stop_drive` when finished** — it
returns the bike's resistance to neutral.

## Tested / not tested

- **Host-tested (CI):** the builder (all spec forms + malformed-spec rejection), the session
  vocabulary + erg-setpoint tracking, the `ErgDriver` loop (convergence, setpoint-follow, and
  Reset-on-stop / -duration / -error), and the FastMCP server tools end-to-end against the
  in-process FTMS twin (`test_workout_builder.py`, `test_workout_session.py`,
  `test_workout_driver.py`, `test_mcp_server.py`). CI installs the `[mcp]` extra so the server
  tests run; without it, `test_mcp_server.py` skips.
- **Not tested (bench/bike):** the bleak transport provider (real SB20 connect + control-point
  write/indicate). The on-air erg path itself is byte-validated (Session 4; `G-sb20-ftms-erg-*`),
  so the remaining proof is just the live MCP-driven ride.

## Remaining

- **On-bike drive** — run an MCP-driven workout end-to-end on the SB20 (measure C3 coex if the spoof
  + WiFi are also up; mirrors `ftms_workout.py`'s on-bike checklist).
- A persisted **workout library** (named user sessions) + adapt-on-the-fly presets, and surfacing
  segment transitions to observability (`sb20proxy.obs`) + a committed workout-log capture.
- A true **pause/resume** (needs a `LiveState` clock-freeze primitive; deliberately deferred to keep
  the shared `ride/state.py` untouched).
