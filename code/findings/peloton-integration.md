# Peloton Power Zone classes as a workout source — research + the Phase 0 capture recipe

**Status: RESEARCH + PHASE 0 RECIPE (2026-09-24); no code yet; issue #342.** ROADMAP **Now #1
(PELOTON)**. Will govern: `code/src/sb20proxy/workout/importers.py::from_peloton()` and the desk
poller (Phase 1), the on-device fetch (Phase 2), and the Phase-0 captures
`code/findings/captures/PELOTON-*` (none committed yet — §4.5 says how). **Nothing in here has been
checked against Peloton's servers by us.** Every "expected" below is a hypothesis the Phase-0 capture
confirms or refutes; the measured answers go into §4.9 and `decisions.md` when the ride is done.

> **Clean-room notice.** §2 was learned by *reading* qdomyos-zwift — `src/peloton.cpp`,
> `src/peloton.h`, `src/homeform.cpp`, `src/WebPelotonAuth.qml` in the owner's fork
> (`C:\repos\cauldnz\qdomyos-zwift`, branch `feat/sb20-buttons` @ `79598059a`). That is **GPL prior
> art read for understanding only.** What follows are facts about *Peloton's* protocol and data
> shapes, in our own words; no qz code, structure, identifiers or client credentials are reproduced,
> and none may flow into this MIT repo ([`qz-upstream-contribution.md`](qz-upstream-contribution.md),
> the GPL/MIT boundary). The first-party API facts in §4.2 were cross-checked against public,
> non-GPL community references (pelo-tech/peloton-api-spec, geudrik/peloton-client-library,
> justmedude/pylotoncycle), not against qz.

Related, already built — reuse, do not rebuild: [`on-device-workout-engine.md`](on-device-workout-engine.md)
(the engine Peloton feeds), [`ride-director.md`](ride-director.md) (`Segment` / `RiderProfile`, the
zone model), [`mcp-workout-server.md`](mcp-workout-server.md) (the desk-side erg driver),
[`ftms-protocol.md`](ftms-protocol.md) (the erg write).

---

## 1. What we want, and the design conclusion

Ride a **Peloton Power Zone (PZ) cycling class** and have *our* stack drive erg from the class's own
targets, the way qz does today for the owner: detect the class the rider has started, read its
power-zone target timeline, and steer the SB20's resistance segment by segment as the class runs — on
either bike, with qz not in the loop. Per `PROJECT-MAP.md` the hard half already exists: the on-device
workout engine, the `/workout` routes, the FTMS erg drive and zone/%FTP targets resolved by a rider
FTP. So Peloton is a **new workout *source***, not a new erg engine — a `from_peloton()` beside
`from_zwo()` / `from_fit()` (#342).

**Design conclusion (closed 2026-09-24, #342):** it is a **downloaded timeline, played back locally
— not a streamed target feed.** The ride object carries the complete target timeline up front
(`target_metrics_data.target_metrics[]`, each entry with `offsets.start` / `offsets.end` in seconds);
qz converts it to its own program rows once and plays them against the class clock; the only thing
it polls afterwards is the workout's *status* string. That is exactly what `WorkoutRuntime.h` already
does with a clock and a segment list from NVS, so: **one fetch at class start, then the existing
engine runs the ride even if WiFi drops.** No streaming, no continuous connectivity, and Phase 2
(on-device) is far smaller than first written up.

## 2. What qz does — the protocol, in prose

### 2.1 Hosts and authentication

- qz talks to Peloton's **third-party API host**, `https://api-3p.onepeloton.com/api/v1/...`, with an
  **OAuth 2.0 bearer token**. Authorization-code flow: authorize at
  `https://auth.onepeloton.com/oauth/authorize`, exchange and refresh at
  `https://auth.onepeloton.com/oauth/token`. Scope requested: `openid offline_access 3p.profile:r
  3p.workout:r` (the `offline_access` is what yields a refresh token). Extra authorize parameters:
  `audience=https://api-3p.onepeloton.com/` and `approval_prompt=force`. The code exchange is a
  form-encoded POST of `client_id`, `code`, `grant_type=authorization_code`, `redirect_uri`; refresh is
  `client_id`, `refresh_token`, `grant_type=refresh_token`. Responses carry `access_token` +
  `refresh_token`; qz stores both per Peloton user id in its app settings and re-fetches `/me` after
  a refresh.
- **The `client_id` is a build-time secret** — qz's CI writes it into a `secret.h` from a GitHub
  secret; it is not in the tree — and the registered redirect URI is a page on the qz project's own
  domain, which relays the code to a localhost listener (desktop) or is caught by an in-app WebView
  (mobile). **We cannot reuse any of it**, and Peloton publishes no self-service developer programme
  (its support pages describe third-party connectivity through named partners only). Consequence:
  **Phase 0 uses the first-party session path (§4.2); the product's OAuth path is an owner action
  (§6).**
- Every API request carries `Authorization: Bearer <token>`, `Content-Type: application/json` and a
  User-Agent naming the app. A non-zero `status` field in the `/me` body is treated as "credentials
  wrong" (polling stops); an empty `data[]` from the workouts list is treated as an auth problem —
  the next poll cycle re-runs the login (`/me`) before polling again.

### 2.2 The request chain (what it asks for, in order)

1. `GET /api/v1/me` → `id` (the user id, needed for the per-user paths), `username`, `total_workouts`.
2. `GET /api/v1/user/workouts?sort_by=-created&page=0&limit=1` → the rider's **most recent workout**:
   `data[0].id` (the workout id), `data[0].status` (contains `IN_PROGRESS` while the class runs),
   `data[0].start_time` (epoch seconds). *(On the first-party host the equivalent path carries the
   user id: `/api/user/{user_id}/workouts` — §4.2.)*
3. When that flips to in-progress (or a *different* workout id is in progress): `GET
   /api/v1/workout/{workout_id}/summary` (fetched, effectively unused) and `GET
   /api/v1/workout/{workout_id}` → the embedded **`ride`** object: `id` (the **ride id** — the class),
   `title`, `instructor_id`, `fitness_discipline` (`cycling`, `walking`, …), `duration` (**pedaling
   seconds**, e.g. 2700 for a 45-min class), `image_url`, `scheduled_start_time` (epoch, the original
   air time). Then `GET /api/v1/instructor/{instructor_id}` → `name`.
4. `GET /api/v1/ride/{ride_id}/details?stream_source=multichannel` → **the timeline** (§2.4). This is
   the one response Phase 1 really needs.
5. Fallback, only if the details produced no usable rows: `GET
   /api/v1/workout/{workout_id}/performance_graph?every_n=1` → `target_metrics_performance_data.
   target_metrics[]` (same entry shape as §2.4) plus `segment_list[]` and `target_performance_metrics`.
6. Further fallbacks to third-party class-target databases (a websocket service at pzpack.com with
   its own login; HomeFitnessBuddy by air-date + instructor + duration). Out of scope for us.

### 2.3 Polling and timing

- The **status poll runs every 10 s** while nothing is in progress, examining only `status` and `id`.
  A transition into `IN_PROGRESS` — or a new workout id while in progress — fires the chain in §2.2
  (only if a trainer is connected; otherwise it just keeps polling). A 1-minute timer bounds the
  chain; an empty workout body is retried 3× at 2 s.
- Once rows exist the poll **slows to 30 s** and is purely a status watch. **Targets are never
  re-fetched during the ride.** Nothing is pushed by Peloton; everything is client pull.

### 2.4 Data shapes (ride details)

- Top level: `pedaling_start_offset` (int, "generally 60"), `duration` (pedaling seconds),
  `instructor_cues[]`, `segments.segment_list[]`, `target_metrics_data{…}`. (The pelo-tech community
  spec also lists `pedaling_duration`, example 2700.)
- `target_metrics_data.target_metrics[]` — **one entry per target block**, each:
  - `offsets{start, end}` — seconds on the **class timeline** (video time), **inclusive** at both ends:
    consecutive blocks leave a 1-s gap (`next.start == prev.end + 1`).
  - `segment_type` — a string; bootcamp classes carry `floor` / `free_mode` blocks (no cycling target).
  - `metrics[]` — each `{name, lower, upper}`. Names seen for bikes: **`power_zone`**, `resistance`,
    `cadence`; treadmill: `pace_intensity`, `speed`, `incline`; rower: `strokes_rate`, `pace_intensity`.
  - Also under `target_metrics_data`: `pace_intensities[]` (tread/row pace tables — irrelevant to us).
- `instructor_cues[]` — `offsets{start,end}` + `resistance_range{lower,upper}` +
  `cadence_range{lower,upper}`: the **resistance/cadence** class model (non-PZ rides).
- `segments.segment_list[]` → `subsegments_v2[]` of `{display_name, length}` — a coarser, *named*
  structure ("Zone 3", "Sweet Spot", "Spin Ups"…), used only when no `target_metrics` exist.
- **Not guaranteed chronological**: qz explicitly scans for the *minimum* `offsets.start` rather than
  trusting element 0, and sorts before building rows. **`from_peloton()` must sort; a fixture must
  pin it.**

### 2.5 The timeline model (offsets → rows)

- Sort by `offsets.start`. Row duration = `end − start + 1`. A gap larger than the 1-s seam between
  two blocks becomes a **no-target filler row**; `floor` / `free_mode` blocks become no-target rows.
  Fillers are only inserted *between* rows — row 0 starts at the first block's `start`, which is also
  the **intro offset** (`min(offsets.start)`, default 60 when absent).
- **Sanity rule**: Σ row durations is compared with `ride.duration`. A shortfall ≥ 10 s means "Peloton
  sent fewer metrics than the class is long" → the timeline is discarded and a fallback source tried;
  a 1–9 s shortfall is a warning only. A timeline with no usable power target is discarded.

### 2.6 The FTP / zone mapping

- For a `power_zone` metric, **`lower` / `upper` are read as zone indices 1–7** (a `switch` on the
  integer). The user's "difficulty" preference chooses `lower`, `upper` or their integer average. Zone
  → watts is **FTP × {Z1 0.50, Z2 0.66, Z3 0.83, Z4 0.98, Z5 1.13, Z6 1.35, Z7 1.50}**, FTP a user
  setting. The performance-graph variant reads only the first metric's `lower`.
- The named fallback (`subsegments_v2.display_name`) maps "ZONE 1".."ZONE 7" with the same multipliers,
  plus "RECOVERY" 0.45, "FLAT ROAD" 0.50, "INTERVALS" 0.75, "SWEET SPOT" 0.91, "SPIN UPS" (a
  0.50→0.83 ramp, or no target), "DESCENDING RECOVERY" (a 0.50→0.45 ramp).
- Non-PZ rides carry `resistance` (Peloton's 0–100 scale, converted to the trainer's own resistance
  scale) + `cadence`: a **resistance-mode** workout, not power. Our erg drive is power (FTMS Set Target
  Power); those classes are out of scope until someone maps Peloton resistance to SB20 watts.
- Our Coggan table (`director.py:37-45`) has the same seven breakpoints as Peloton's published PZ
  bands (Z1 < 55 %, Z2 56–75, Z3 76–90, Z4 91–105, Z5 106–120, Z6 121–150, Z7 > 150 % of FTP); the
  representative %FTP differs at Z2 (0.65 vs 0.66) and Z7 (1.70 vs 1.50). **#342 says prefer the
  numbers over zone names** — so Phase 1 should emit `pct_ftp` from the Peloton band (lower bound,
  midpoint or upper — the rider's "difficulty") rather than a `zone` id, and the capture must confirm
  the app's displayed watt ranges match `ftp × band`.

### 2.7 The class clock (the intro, auto-start, pause)

- `start_time` (workout list) is the epoch second the workout began — i.e. when the rider pressed
  start on the class, whose video opens with the intro. qz auto-starts its own program clock only if
  *now* is within **180 s** of `start_time`: with the intro, at `start_time + intro_offset + 4 s`
  (intro = `min(offsets.start)`, normally 60; +4 s for buffering); with the "without intro"
  preference (the rider presses skip), at `start_time + 6 s` (time to press "skip" plus a 3-s
  countdown). Outside that window, or with neither preference, it asks the rider to press start
  manually.
- Because row 0 is the first target block, **qz's t = 0 is pedaling start (class offset ≈ 60), not
  video start.** Pause is handled by qz's own program clock; **there is no mid-ride re-sync from the
  API** (status only). What Peloton's `status` does on pause, and whether `start_time` moves, is not
  observable from qz — Phase 0 must measure it (§4.9).

### 2.8 Things not to conflate

The `powerzonepack` module (pzpack.com, a websocket API with its own username/password) and
HomeFitnessBuddy are **separate class-target databases** qz falls back to; neither is Peloton's API.
A leftover `www.peloton.com/oauth/token` request in the code is a legacy path. The treadmill/rower
pace tables and `instructor_cues` resistance rows are irrelevant to a PZ bike ride.

## 3. What already exists here that Peloton plugs into (verified 2026-09-24)

| Already built (#342's table) | Where — file:line, verified |
|---|---|
| On-device workout engine: structured workouts persisted in NVS, executed deterministically as the FTMS erg controller | `firmware/lib/proxy/WorkoutEngine.h:26-33` (`WkSegment`: `durationS`, `powerW` / `pctFtp` / `zone`, `cadenceRpm`), `:59-68` (`Workout`, `ftpW`), `:114-151` (`parseWorkout`, tolerant JSON), `:166-190` (`workoutStateAt`, the stepper); `firmware/lib/proxy/WorkoutRuntime.h:16-102` (the live clock — `start/pause/resume/skip/stop`, injected `millis()`, host-tested) |
| The `/workout` route | `firmware/lib/proxy/WebRoutes.h:573-576` (`GET /workout`, `GET /workout/state`, `POST /workout/load`, `POST /workout/preset`) + `:594-601` (the five control verbs `start/pause/resume/skip/stop`) |
| On-device FTMS erg drive (head unit drives the trainer; no phone/PC) | `firmware/lib/proxy/RuntimeConfig.h:93` (`trainerNameFilter` — "FTMS trainer to erg-drive from the workout engine"; empty = erg off); `docs/system-reference.md` §4 row "+ trainer configured" |
| Canonical compact-JSON workout format, 1:1 with the Python `Segment` | `code/src/sb20proxy/workout/importers.py:155-170` (`to_device_json(workout, ftp_w)` → `{"name","ftp_w","segments":[{"t","label","power_w"\|"pct_ftp"\|"zone","cadence_rpm"}]}`); the shape comment at `WorkoutEngine.h:18-23` |
| Workout importers to copy the shape of | `importers.py:38-88` (`from_zwo`), `:123-152` (`from_fit`), and especially `:91-107` (`fit_steps_to_segments` — the **pure, host-tested step→`Segment` map** pattern `from_peloton()` should mirror: a pure `peloton_metrics_to_segments(entries)` over the captured JSON, no network) |
| Zone targets already modelled — `Segment.zone` ("Z4") and `pct_ftp`, resolved to watts by `RiderProfile` | `code/src/sb20proxy/ride/director.py:24-46` (`Zone`, `COGGAN_ZONES`), `:49-74` (`RiderProfile.ftp_w` default **250**, `watts_for_pct`, `watts_for_zone`), `:77-102` (`Segment`: `power_w` → `pct_ftp` → `zone` precedence); firmware twin `WorkoutEngine.h:37-57` (`zonePct`, `segmentTargetW`) |
| Desk POST path | `code/scripts/import_workout.py` (`--ftp`, `--post <base>` → `<base>/workout/load`, lines 5-9, 27-29, 51) |
| The erg-controller rule | `docs/system-reference.md:141-143` — §6 rule 3, *never two erg controllers on one bike* |
| Rider FTP dependency | `ROADMAP.md` Next item 3 — `ftp_w` in `RuntimeConfig`; presets assume 250 W |

So the Phase-1 chain is entirely existing plumbing: **captured ride JSON → `from_peloton()` →
`Workout` → `to_device_json(…, ftp_w)` → `POST /workout/load` → `POST /workout/start` → the engine
writes each segment's target over FTMS.**

## 4. Phase 0 — the capture recipe (run-sheet)

> *Real-data-first: don't build the mapping ahead of the capture that grounds it.* One real PZ ride on
> the owner's own account, captured passively from a laptop. **The bike is untouched** — ride the class
> exactly as today (qz driving erg; our head unit a spoof-only crank with `trainerNameFilter` empty,
> §6.3 of the system reference). The capture is HTTP only.

### 4.1 Prerequisites

- [ ] A Peloton account with an upcoming (on-demand is fine) **Power Zone** cycling class picked — PZ
      Endurance / Power Zone / PZ Max all qualify. Note the class's **ride id**: open the class on
      `members.onepeloton.com`; the class-details URL carries `classId=<32-hex>`. That is the ride id.
- [ ] A laptop on the home network with Python ≥ 3.10 and `pip install requests` (the snippet uses
      `requests` for readability; `urllib.request` works too). Nothing from this repo is required —
      the snippet is stdlib + `requests` on purpose.
- [ ] `jq` (optional, for the one-liners in §4.7).
- [ ] The auth credential from §4.2 in an **environment variable**, never in a file or on a command line.
- [ ] A way to note wall-clock times during the ride (phone notes / paper): the narration in §4.4.
- [ ] Optional but valuable: a screenshot of the app's **Power Zones** settings page (the rider's
      Peloton FTP and the seven watt ranges) — it grounds §2.6 (owner's call whether to commit it: it
      shows the owner's FTP; this repo already discusses FTP openly).

### 4.2 Authentication for Phase 0 — pick one

| | Method | How | Notes |
|---|---|---|---|
| **A (recommended)** | Reuse the **browser session** | Log in at `members.onepeloton.com` → DevTools → Application → Cookies → copy the value of `peloton_session_id` → `$env:PELOTON_SESSION = "<value>"` (PowerShell) / `export PELOTON_SESSION=…` (bash). Requests send `Cookie: peloton_session_id=$PELOTON_SESSION` and the header `peloton-platform: web`. | No password, no client registration; it is the same session the web app holds. **It is a live credential** — treat like a token: never in a file, argv or a commit; unset it after; log out of the browser afterwards if you want it invalidated. |
| B | `POST https://api.onepeloton.com/auth/login` | JSON body `{"username_or_email": …, "password": …}` → response `session_id` + `user_id`; then the same cookie as A. Read the password with `getpass`, never from argv. | Documented by the public community libraries (pelo-tech spec, geudrik, pylotoncycle). Use only if A is awkward. |
| C | OAuth (the product path, §2.1) | — | **Not available today**: needs a registered `client_id` we do not have (§6). |

The first-party host is `https://api.onepeloton.com` and its paths have **no `/v1`**
(`/api/me`, `/api/user/{user_id}/workouts`, `/api/workout/{id}`, `/api/ride/{id}/details`,
`/api/workout/{id}/performance_graph`); qz's third-party host has `/api/v1/…`. The JSON shapes are
expected to match — that is open question 7.

If the first request returns 403 from the edge (bot check), send the browser's own `User-Agent`
string instead of the snippet's; if `/api/user/{id}/workouts` returns 4xx, add `peloton-platform: web`
(already set) — some newer first-party endpoints require it.

### 4.3 The capture snippet

Save as `peloton_phase0.py` **outside the repo** (e.g. `~/peloton-raw/`). It writes **raw** responses
outside the repo and only the `redact` step writes into `code/findings/captures/`. Phase 1 promotes
this into `code/scripts/` with tests; for Phase 0 it is deliberately throwaway.

```python
#!/usr/bin/env python3
"""Phase-0 Peloton capture (#342). RAW files land in ~/peloton-raw/ (never in git);
`redact` writes the scrubbed copies into code/findings/captures/.

  $env:PELOTON_SESSION = "<peloton_session_id cookie>"          # auth option A
  python peloton_phase0.py prestage --ride RIDE [--also OTHER_RIDE]   # the evening before
  python peloton_phase0.py poll --ride RIDE                     # start BEFORE pressing start; Ctrl-C after
  python peloton_phase0.py postride --ride RIDE --workout WORKOUT_ID
  python peloton_phase0.py redact --user-id USER_ID <raw files...> <captures dir>
"""
import argparse, datetime as dt, json, os, pathlib, time
import requests  # pip install requests

API = "https://api.onepeloton.com"
RAW = pathlib.Path.home() / "peloton-raw"


def session():
    s = requests.Session()
    s.headers.update({"peloton-platform": "web", "Content-Type": "application/json",
                      "User-Agent": "sb20proxy-phase0/0.1"})
    s.cookies.set("peloton_session_id", os.environ["PELOTON_SESSION"])
    return s


def get(s, path, **params):
    r = s.get(API + path, params=params, timeout=20)
    body = None
    try:
        body = r.json()
    except ValueError:
        pass
    return {"t": time.time(), "iso": dt.datetime.now().isoformat(timespec="seconds"),
            "url": r.url, "status_code": r.status_code, "body": body}


def save(ride, what, rec, suffix="json"):
    RAW.mkdir(exist_ok=True)
    p = RAW / f"PELOTON-{ride}-{what}-{dt.datetime.now():%Y%m%d-%H%M}.{suffix}"
    p.write_text(json.dumps(rec, indent=1), encoding="utf-8")
    print("saved", p, "HTTP", rec["status_code"])
    return p


def prestage(a):
    s = session()
    me = get(s, "/api/me")
    save(a.ride, "me", me)
    print("user id:", (me["body"] or {}).get("id"), "<- keep for `redact --user-id`")
    save(a.ride, "ride-details", get(s, f"/api/ride/{a.ride}/details", stream_source="multichannel"))
    save(a.ride, "ride-details-nostream", get(s, f"/api/ride/{a.ride}/details"))
    if a.also:  # a NON-PZ cycling class, for the resistance/cadence contrast (no ride needed)
        save(a.also, "ride-details", get(s, f"/api/ride/{a.also}/details", stream_source="multichannel"))


def poll(a):
    s = session()
    uid = (get(s, "/api/me")["body"] or {})["id"]
    log = RAW / f"PELOTON-{a.ride}-status-poll-{dt.datetime.now():%Y%m%d-%H%M}.jsonl"
    RAW.mkdir(exist_ok=True)
    fetched, idle = None, 0
    print("polling every 10 s; Ctrl-C when the class is over ->", log)
    while True:
        rec = get(s, f"/api/user/{uid}/workouts", sort_by="-created", page=0, limit=1)
        with log.open("a", encoding="utf-8") as f:
            f.write(json.dumps(rec) + "\n")
        data = ((rec["body"] or {}).get("data") or [{}])
        wid, status = data[0].get("id"), str(data[0].get("status"))
        print(rec["iso"], wid, status, data[0].get("start_time"))
        if "IN_PROGRESS" in status.upper() and wid and wid != fetched:
            fetched = wid
            w = save(a.ride, "workout", get(s, f"/api/workout/{wid}"))
            ride = ((json.loads(w.read_text())["body"] or {}).get("ride") or {})
            if ride.get("instructor_id"):
                save(a.ride, "instructor", get(s, f"/api/instructor/{ride['instructor_id']}"))
            save(a.ride, "ride-details-inclass",
                 get(s, f"/api/ride/{ride.get('id', a.ride)}/details", stream_source="multichannel"))
        idle = idle + 1 if (fetched and "IN_PROGRESS" not in status.upper()) else 0
        if idle >= 6:  # a minute past the end -> done
            break
        time.sleep(10)


def postride(a):
    s = session()
    save(a.ride, "workout-complete", get(s, f"/api/workout/{a.workout}"))
    save(a.ride, "workout-summary", get(s, f"/api/workout/{a.workout}/summary"))
    save(a.ride, "performance-graph", get(s, f"/api/workout/{a.workout}/performance_graph", every_n=1))


DROP_KEYS = {"session_id", "access_token", "refresh_token", "id_token", "password", "email",
             "username", "user_id", "first_name", "last_name", "middle_name", "location", "birthday",
             "gender", "height", "weight", "phone", "phone_number", "address", "facebook_id",
             "strava_id", "fitbit_id", "apple_id", "google_id", "member_id", "customer_id",
             "referral_code", "default_heart_rate_zones", "default_max_heart_rate"}
USER_KEEP = {"id", "cycling_ftp", "cycling_workout_ftp", "estimated_cycling_ftp",
             "cycling_ftp_source", "cycling_ftp_workout_id", "total_workouts"}


def redact(x, uid):
    if isinstance(x, dict):
        if "email" in x or "username" in x:  # a user object -> whitelist only
            x = {k: v for k, v in x.items() if k in USER_KEEP}
        return {k: ("<redacted>" if k in DROP_KEYS else redact(v, uid)) for k, v in x.items()}
    if isinstance(x, list):
        return [redact(v, uid) for v in x]
    if isinstance(x, str) and uid and uid in x:
        return x.replace(uid, "<user-id>")
    return x


def redact_cmd(a):
    out = pathlib.Path(a.paths[-1])
    for src in map(pathlib.Path, a.paths[:-1]):
        lines = src.read_text(encoding="utf-8").splitlines() if src.suffix == ".jsonl" else [src.read_text(encoding="utf-8")]
        scrubbed = [json.dumps(redact(json.loads(ln), a.user_id), indent=(None if src.suffix == ".jsonl" else 1)) for ln in lines if ln.strip()]
        (out / src.name).write_text("\n".join(scrubbed) + "\n", encoding="utf-8")
        print("redacted ->", out / src.name)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("prestage", "poll", "postride"):
        p = sub.add_parser(name); p.add_argument("--ride", required=True)
        if name == "prestage": p.add_argument("--also")
        if name == "postride": p.add_argument("--workout", required=True)
    r = sub.add_parser("redact"); r.add_argument("--user-id", required=True); r.add_argument("paths", nargs="+")
    a = ap.parse_args()
    handler = {"prestage": prestage, "poll": poll, "postride": postride, "redact": redact_cmd}.get(a.cmd)
    handler(a)
```

Equivalent `curl` for a single request (bash):
`curl -sS -H "Cookie: peloton_session_id=$PELOTON_SESSION" -H "peloton-platform: web"
"https://api.onepeloton.com/api/ride/<ride-id>/details?stream_source=multichannel" > raw.json`.

### 4.4 The run-sheet (Plan; record Actual inline — `✅`/`❌`/`⚠️` + timestamps, per `sessions/PLAYBOOK.md`)

**Desk pre-stage — the evening before (≈ 15 min, no bike)**

1. [ ] Set `PELOTON_SESSION` (§4.2 A). Run `python peloton_phase0.py prestage --ride <ride-id>
       [--also <a non-PZ cycling class id>]`. ✅ = three (or four) files in `~/peloton-raw/`, all HTTP 200,
       and the `me` body has an `id`. Note the user id (it is needed for `redact`).
       *Actual:* ⏱ ____ · ____
2. [ ] Look at the pre-class `ride-details`: `jq '.body.target_metrics_data.target_metrics | length'`
       > 0, and the §4.7 table renders. **If the timeline is already there the night before, the
       "downloaded timeline" conclusion is confirmed before anyone pedals** — the ride only adds the
       status/clock answers. *Actual:* ____ entries; `pedaling_start_offset` = ____; `duration` = ____
3. [ ] Confirm the poller starts (`poll`, watch two lines print, Ctrl-C). *Actual:* ____

**Ride day (the class itself; rider cost ≈ zero beyond narration)**

4. [ ] Start `python peloton_phase0.py poll --ride <ride-id>` on the laptop **before** pressing start
       in the Peloton app; leave it running. ⏱ started ____
5. [ ] **Narrate** (phone note, `HH:MM:SS`): **T0** pressed start on the class · **T1** the pedaling
       countdown ended / the first zone target appeared on screen · any **pause** (start/stop; one
       deliberate ~30 s pause mid-class is worth it if the class allows) · **Tend** the class ended.
       Also note the first zone shown and whether you skipped the intro.
       *Actual:* T0 ____ · T1 ____ · pause ____–____ · Tend ____ · skipped intro? ____
6. [ ] Ride the class as normal (qz drives erg as today). Glance at the laptop once: the poller should
       have printed `IN_PROGRESS` and `saved … workout / instructor / ride-details-inclass`.
       *Actual:* first `IN_PROGRESS` at ⏱ ____ (= T0 + ____ s)
7. [ ] After the class: let the poller exit on its own (six idle polls) or Ctrl-C. Run `postride
       --ride <ride-id> --workout <workout-id>` (the id the poller printed). *Actual:* ____

**Desk, afterwards (≈ 30 min)**

8. [ ] Redact (§4.6), verify, copy into `code/findings/captures/`, add the index rows (§4.5), fill §4.9
       and this run-sheet's *Actual* fields, flip this doc's Status, log the numbers in `decisions.md`,
       open the PR. *Actual:* ____

### 4.5 What to save, and the filenames

All files: `code/findings/captures/PELOTON-<ride-id>-<what>-<YYYYMMDD-HHMM>.json` (one HTTP response
per file, as the `{t, iso, url, status_code, body}` record the snippet writes — `url` is redacted of
the user id), except the status poll, which is a **`.jsonl`** (one record per poll, timestamped — the
repo's canonical lossless form). The captures-index test (`code/tests/test_captures_index.py`) covers
both extensions.

| `<what>` | Endpoint | When | Why Phase 1 needs it |
|---|---|---|---|
| `me` | `/api/me` | pre-stage | the user-object *shape* + any FTP fields (whitelisted; everything else stripped) |
| `ride-details` | `/api/ride/{ride}/details?stream_source=multichannel` | pre-stage | **the timeline fixture** for `from_peloton()` |
| `ride-details-nostream` | same, no query | pre-stage | does `stream_source` change the payload? |
| `ride-details` of `--also` | a non-PZ cycling class | pre-stage | the `resistance`/`cadence` contrast (what to reject) |
| `status-poll` (`.jsonl`) | `/api/user/{uid}/workouts?sort_by=-created&page=0&limit=1` every 10 s | ride | detection latency, `status` values, `start_time` meaning, pause behaviour |
| `workout` | `/api/workout/{wid}` | at first `IN_PROGRESS` | the `ride` sub-object (`duration`, `pedaling_start_offset`?, `scheduled_start_time`), the poll→ride-id link |
| `instructor` | `/api/instructor/{id}` | at first `IN_PROGRESS` | display name only (public) |
| `ride-details-inclass` | as `ride-details` | at first `IN_PROGRESS` | must be byte-identical to the pre-stage one (proves "downloaded, not streamed") |
| `workout-complete`, `workout-summary` | `/api/workout/{wid}`, `/summary` | post-ride | final `status`, `end_time`, whether pauses stretch wall-clock |
| `performance-graph` | `/api/workout/{wid}/performance_graph?every_n=1` | post-ride | the `target_metrics_performance_data` variant + the rider's actual per-second output (owner's call to commit — it is personal ride data, as the existing `RIDE-*` captures are) |

**Add a row per file to [`captures/README.md`](captures/README.md)** (a new "Peloton Phase 0
(YYYY-MM-DD, #342)" table, columns *File · Source · What it is*) **in the same commit** — CI fails on
an unindexed capture, and also on an index row naming a file that is not there, so do not add rows
ahead of the files.

### 4.6 Redaction — before anything is committed

Captured bodies contain the session credential's fingerprint, the user id, username, e-mail and
profile data. The rule: **strip secrets and personal account data; keep class data.**

- **Never saved at all:** request/response *headers* (that is where the cookie and any `Set-Cookie`
  live) — the snippet stores only `url`, `status_code`, `body`.
- **Must strip (the snippet's `DROP_KEYS`, at any depth):** `session_id`, `access_token`,
  `refresh_token`, `id_token`, `password`, `email`, `username`, `user_id`, names, `location`,
  `birthday`, `gender`, `height`, `weight`, phone/address, linked-account ids (`facebook_id`,
  `strava_id`, `fitbit_id`, `apple_id`, `google_id`), `member_id`/`customer_id`/`referral_code`, and
  heart-rate zones. Any object that has an `email` or `username` key is a **user object** and is
  reduced to the whitelist (`id` → `<user-id>`, the FTP fields, `total_workouts`). **Every occurrence
  of the user id string** (values *and* URLs) becomes `<user-id>`.
- **Safe to keep (class data, public or the owner's own workout structure):** everything under
  `ride` (`id`, `title`, `instructor_id`, `duration`, `pedaling_start_offset`, `scheduled_start_time`,
  `fitness_discipline`, `image_url`, difficulty fields, `target_metrics_data`, `instructor_cues`,
  `segments`), instructor `id`/`name`/`image_url` (public figures), workout `id`, `status`,
  `start_time`, `end_time`, `created_at`, `device_type`, `workout_type`, `fitness_discipline`.
- **Owner's call:** the FTP fields, `total_work`, personal-record flags, and the `performance-graph`
  per-second output (power/cadence/resistance/HR). The repo already commits whole rides
  (`RIDE-ant-ride-20260622.jsonl`), so keeping them is consistent — HR is the one to think about.

Run: `python peloton_phase0.py redact --user-id <user-id> ~/peloton-raw/PELOTON-* code/findings/captures/`,
then **verify before `git add`** (bash; both must print nothing):

```bash
grep -l "<the-user-id>" code/findings/captures/PELOTON-*
grep -lE '"(session_id|access_token|refresh_token|email|username|user_id)"' code/findings/captures/PELOTON-*
```

and eyeball `git diff --cached --stat` + one file. Also `grep -c '@' …` as a cheap e-mail check.
Redaction is a **filter over the raw record**, so the JSONL/JSON rule "never edit a capture" holds: the
raw file stays raw (outside git); the committed file is a derived, documented projection of it.

### 4.7 What a good capture looks like

Run these against the pre-stage `ride-details` (bash + `jq`; the same in Python is a five-liner):

```bash
F=code/findings/captures/PELOTON-<ride-id>-ride-details-<ts>.json
jq -r '.body.target_metrics_data.target_metrics | sort_by(.offsets.start)[]
       | [.offsets.start, .offsets.end, (.offsets.end - .offsets.start + 1), .segment_type,
          ((.metrics[]? | select(.name=="power_zone") | "Z\(.lower)-\(.upper)") // "-")] | @tsv' "$F"
jq '.body | {duration, pedaling_start_offset, pedaling_duration, fitness_discipline,
             n: (.target_metrics_data.target_metrics | length),
             first: ([.target_metrics_data.target_metrics[].offsets.start] | min),
             last:  ([.target_metrics_data.target_metrics[].offsets.end] | max),
             names: ([.target_metrics_data.target_metrics[].metrics[]?.name] | unique)}' "$F"
```

Expected for a 45-min PZ class (each line is a hypothesis to tick or refute):

- `target_metrics` present, **~10–40 entries** (one per zone block), `names` ⊇ `["power_zone"]`.
- After sorting, `start` is **strictly increasing**, blocks **don't overlap**, and consecutive blocks
  have `next.start == prev.end + 1` (1-s seams) — note any larger gaps (warm-up/cool-down without a
  target?) and the sum of durations + gaps vs `duration`.
- `power_zone.lower`/`upper` are **small integers 1–7** (zone indices). If they are ≫ 7 they are
  watts or %FTP — record which; that is question 1.
- `first` ≈ `pedaling_start_offset` ≈ **60**; `last + 1` ≈ `pedaling_start_offset + duration`
  (**2760** for 2700 s of pedaling). `duration` is pedaling time, not video time.
- The pre-stage and in-class `ride-details` are **identical** (`diff <(jq -S .body A) <(jq -S .body B)`).
- The status poll shows `IN_PROGRESS` within a poll or two of T0, then a terminal status after Tend;
  `start_time` compared with T0/T1 says what it measures (press-start vs pedaling-start).
- The raw (un-redacted) array order: was it already chronological? Record it — it decides whether the
  sort test is defensive or load-bearing.

A capture that shows `target_metrics` absent or empty for a PZ class, or `power_zone` values that are
not zone indices, is **not a failure** — it is the finding; the design (§1) then needs revisiting
before Phase 1.

### 4.8 Acceptance checklist for Phase 0

- [ ] Captures committed under `code/findings/captures/PELOTON-*` with rows in `captures/README.md`;
      `pytest -q tests/test_captures_index.py tests/test_findings_index.py tests/test_doc_links.py`
      green; the §4.6 greps print nothing.
- [ ] §4.9 answered from the captures (numbers, not adjectives), this doc's Status flipped to
      `PHASE 0 DONE (date)`, the run-sheet's *Actual* fields filled.
- [ ] The measured values (offset conventions, unit of `lower`/`upper`, detection latency, `start_time`
      semantics, pause behaviour) appended to [`decisions.md`](decisions.md).
- [ ] The Phase-1 fixture named: which file is *the* `from_peloton()` golden vector, and which
      non-PZ file is the "must reject" vector.
- [ ] The auth decision for the product path recorded (§6): first-party session vs an OAuth client id.

### 4.9 Open questions the capture must answer

| # | Question | How the capture answers it | Answer (fill after the ride) |
|---|---|---|---|
| 1 | **Units of `power_zone.lower`/`upper`** — zone indices 1–7 (qz's reading), watts, or %FTP? | the value range in `ride-details`; cross-check the app's zone watt table (§4.1 screenshot) = `ftp × band` | |
| 2 | **Which class types carry PZ targets?** Do PZ Endurance / PZ / PZ Max all have `power_zone`; does a non-PZ cycling class carry `resistance`/`cadence` instead (or nothing)? | `names` from the PZ file vs the `--also` file; the ride's class-type fields | |
| 3 | **Per second or per segment?** Are entries one-per-zone-block with inclusive offsets and 1-s seams, or per second? | the §4.7 table: entry count and the seam pattern | |
| 4 | **Clock alignment** — what is `pedaling_start_offset`, does it equal `min(offsets.start)`, and is the timeline's zero the video start (intro included) or the pedaling start? | `pedaling_start_offset`, `first`, `last`, `duration`; T0 vs T1 from the narration | |
| 5 | **Class-start detection** — does the workout list show `IN_PROGRESS` before the pedaling clock starts (during the intro)? How many seconds after T0? What other `status` values appear? Does `start_time` mean T0 or T1? | the `status-poll` JSONL timestamps vs T0/T1 | |
| 6 | **Pause / late join** — does `status` change on pause, does `start_time` move, does `end_time − start_time` exceed `duration` by the paused time? (Late join: a second, optional ride started mid-class.) | the poll during the deliberate pause + `workout-complete` | |
| 7 | **Host parity** — are the first-party (`api.onepeloton.com`) shapes the same as the third-party (`api-3p`) ones qz reads? | unanswerable until an OAuth client exists (§6); until then Phase 1 builds on the first-party shape and re-validates on the switch | |
| 8 | **Rider FTP from Peloton** — does `/api/me` carry `cycling_ftp` / `cycling_workout_ftp` / `estimated_cycling_ftp`, and does the app's watt table derive from it? | the whitelisted `me` fields + the screenshot | |
| 9 | **Gaps and non-cycling blocks** — any `segment_type` other than cycling in a PZ ride, any untargeted gap (warm-up / cool-down), and does Σ durations match `duration` within qz's 10-s rule? | the §4.7 sums | |
| 10 | **Does `stream_source=multichannel` change the details payload?** | `diff` of the two pre-stage files | |
| 11 | **Order** — was the raw `target_metrics` array chronological in this class? | the raw-file order noted before redaction | |

## 5. Phase 1 and Phase 2 — scope and acceptance

**Phase 1 — desk adapter (the fastest path to a real erg ride).**
Scope: a pure `peloton_metrics_to_segments(entries, *, difficulty)` (mirrors
`fit_steps_to_segments`: sort by `offsets.start`, duration `end − start + 1`, gaps and non-cycling
blocks → untargeted `Segment`s, `power_zone` → `pct_ftp` from the Peloton band per the §4.9 answers),
`from_peloton(ride_details_json)` → `Workout`, and a small poller script (first-party session from an
env var → find the in-progress workout → ride details → `to_device_json(…, ftp_w)` → `POST
/workout/load` → `POST /workout/start` at the aligned moment, §2.7). No firmware change.
Acceptance: golden-vector tests against the committed capture **including one that shuffles the
array and asserts the sort**, and one that rejects the non-PZ file; the hermetic suite green; then
**a real PZ ride driving erg end-to-end with our head unit as the sole erg controller** — qz closed.

**Phase 2 — on the head unit (no phone, no PC).** Scope: one HTTPS fetch chain at class start (the
Guition, 8 MB PSRAM, is the plausible host; the C3 is tight), JSON → `Workout` in `WorkoutEngine.h`
(the parser is already tolerant), credential storage in NVS, a "Peloton" entry on the Workout screen
that finds the in-progress class and starts the clock. Acceptance: the same ride with the laptop off.
**Blocked on the auth decision in §6** — a session cookie expires; an unattended device needs a
refresh-token flow, which needs an OAuth client id.

**Design constraint for both — the qz interlock.** `docs/system-reference.md` §6 rule 3: *never two
erg controllers on one bike.* Note that "one consumer per peripheral" (§6 rule 2) does **not** protect
us here: qz drives the SB20's resistance *without* holding its FTMS surface (the 2026-07-06 passive
capture, `QDZ-sniff-qdomyos-sb20-20260706-0742.pcap`), so both controllers can physically coexist and
fight. The Peloton source therefore implies **our head unit is the erg controller** (`trainerNameFilter`
set, qz not connected to that bike), and the existing rule's converse (empty `trainerNameFilter` on a
qz day) stays. Make it a product interlock, not discipline: the Peloton start refuses unless the
trainer is configured, the Workout screen shows *erg controller: this head unit*, and the ride-day
checklist carries one line — "qz closed". The decision itself is #342's acceptance item.

## 6. Dependencies

- **Per-rider FTP — ROADMAP Next item 3** (`ftp_w` in `RuntimeConfig`; presets assume 250 W). PZ
  targets are **zone indices**: without the rider's FTP they resolve to nothing, and a wrong FTP is a
  silently wrong ride. Phase 1 gets away with `to_device_json(workout, ftp_w)` per POST (as
  `import_workout.py --ftp` does today) because the JSON carries `ftp_w` — but with **two riders on two
  head units** each board must own its FTP, or the poller must know which board is which rider; Phase 2
  cannot work without it. Question 8 may hand us Peloton's own FTP for the rider as a seed.
- **An auth path for the product.** #342 wants OAuth (no password stored). That needs a Peloton
  third-party `client_id`, which qz has as a private build secret and Peloton does not hand out via any
  public programme — **owner action**: obtain one (partner route), or accept the first-party session
  path (a `peloton_session_id` provisioned via `/setup`, re-entered when it expires) for round zero.
  Question 7 (host parity) is downstream of this choice.
- **The unofficial-API risk**, accepted knowingly (#342): no public contract; shapes can change; every
  assumption here is pinned by a committed capture so a change shows up as a failing golden vector,
  not a wrong ride.
- **HTTPS/TLS + JSON on the device** (Phase 2 only) — the Guition build; not a Phase-0/1 concern.
