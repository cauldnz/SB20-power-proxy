# Ride director + live dashboard (`ride_web.py`)

A browser dashboard that **drives our capture / calibration sessions**: it talks the
rider through a structured workout (big target watts + a per-block countdown) while the
meters stream live and the capture logs to JSONL (the canonical record). Same idea as
the ESP32 `/ui` — the laptop just serves small JSON; the browser does the rendering.
Stdlib only, no web framework.

## Try it now — no ANT+ stick (replays a real committed capture)

```bash
cd code
python scripts/ride_web.py --replay findings/captures/A-stagesL-steady-20260614-165737.jsonl
```

Opens <http://localhost:8080/>. Press **Start** — the director walks the calibration
workout while the replayed real power/cadence streams into the dashboard. Useful flags:
`--speed 30` fast-forwards a long capture, `--workout demo` is an 80-second version,
`--no-browser` if you'll open it yourself.

## On the bike — live dual-meter capture (one stick, one clock)

```bash
python scripts/ride_web.py --live \
    --stages-id 62144 --assioma-id 17039 \
    --output findings/captures/CAL-multi-$(date +%Y%m%d-%H%M).jsonl
```

This **is** `07_capture_multi.py` — the same dual-meter capture to the same JSONL — with
a live tap into the dashboard. Open the URL on your phone, start pedalling, press Start.
Each meter's live power shows side by side with the **delta** (the meter-vs-meter the
calibration fit needs); the JSONL stays the canonical record and `09_fit_calibration.py`
consumes it afterwards exactly as today. (Live mode needs the stick in WSL — same setup
as the other capture scripts.)

## The workout

`--workout calibration` (default, ~16 min) is the structured form of `ride_wizard.py`'s
Session A: warm-up → endurance → tempo → threshold → surge → **coast** (zero-power) →
low-cadence grind → high-cadence spin → cool-down. Powers are **Stages watts** (what the
bike/app shows), chosen to sample the full meter-vs-meter curve. Edit
`src/sb20proxy/ride/workouts.py` to change it.

## How it's built

- Pure, host-tested core in `src/sb20proxy/ride/`: `director.py` (the workout state
  machine — a total function of elapsed time), `workouts.py`, `state.py` (thread-safe
  live state with an injectable clock), `webapp.py` (the live-JSON transform + the page),
  `server.py` (stdlib `http.server`), `replay.py` (the hardware-free feed). 24 hermetic
  tests; the replay test runs against a committed capture (real-data-first).
- The capture/replay feed is the hardware seam; `ride_web.py` is just the wiring, and the
  live path is the only part that touches `openant`.

## Not yet — follow-ups

- **Trainer control (erg):** drive each block's target watts into the bike over FE-C, so
  it controls *the trainer*, not just the rider. The FE-C Target-Power codec and
  `twins/trainer.py` already exist — wire `director target → FE-C Target Power`.
- **Annotate the JSONL** with ride/segment markers so the analysis can split samples by
  block (the `event_sink` hook is built; wiring it for live wants a lock around the
  capture's `_log`).
- Full target **timeline overlay** on the chart (it currently draws the current target as
  a dashed line).
