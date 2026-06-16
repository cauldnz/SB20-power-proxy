#!/usr/bin/env python3
"""Web ride director + live dashboard — drives our capture / calibration sessions.

It opens a little dashboard in your browser that talks you through a structured
workout (big target watts + a countdown for each block) while the meters stream
live and the capture logs to JSONL (the canonical record). The same UI works two
ways:

  Desk demo (no ANT+ stick — replays a real committed capture):
      python code/scripts/ride_web.py --replay code/findings/captures/<file>.jsonl

  On the bike (live dual-meter capture, one stick, one clock):
      python code/scripts/ride_web.py --live \
          --stages-id 62144 --assioma-id 17039 \
          --output code/findings/captures/CAL-multi-$(date +%Y%m%d-%H%M).jsonl

Open the printed URL, get pedalling, press Start. --workout calibration|demo.
The pure parts live in sb20proxy.ride and are host-tested; this script is just the
wiring (and the live path is the only bit that needs the stick).
"""

from __future__ import annotations

import argparse
import sys
import threading
import time
import webbrowser
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from sb20proxy.ride import WORKOUTS, LiveState, RideDirector  # noqa: E402
from sb20proxy.ride.replay import replay_into  # noqa: E402
from sb20proxy.ride.server import RideServer  # noqa: E402


def _run_replay(args: argparse.Namespace, state: LiveState) -> None:
    replay_into(args.replay, state, speed=args.speed,
                default_source=args.replay_source, on_done=state.set_capture_stopped)


def _run_live(args: argparse.Namespace, state: LiveState) -> None:
    # Import the capture runner lazily — it pulls in openant (only present on the
    # bike / WSL box with the stick). Keeps the desk/replay path import-clean.
    spec = spec_from_file_location("capture_multi", HERE / "07_capture_multi.py")
    assert spec and spec.loader
    cap = module_from_spec(spec)
    sys.modules["capture_multi"] = cap
    spec.loader.exec_module(cap)

    sources = [
        cap.Source("stages", args.stages_id, cap.DEVTYPE_BIKE_POWER,
                   cap.PERIOD_BIKE_POWER, cap.decode_page),
        cap.Source("assioma", args.assioma_id, cap.DEVTYPE_BIKE_POWER,
                   cap.PERIOD_BIKE_POWER, cap.decode_page),
    ]
    if args.fec_id is not None:
        sources.append(cap.Source("bike_fec", args.fec_id, cap.DEVTYPE_FEC,
                                  cap.PERIOD_FEC, cap.decode_fec))

    class TappedRunner(cap.MultiCaptureRunner):
        """The real capture, plus a tap that mirrors each live reading into the
        dashboard's LiveState. The tap only reads the decoded record and updates
        a thread-safe holder — the JSONL write (super()._log) is unchanged."""

        def _log(self, kind: str, **fields):
            super()._log(kind, **fields)
            if kind == "broadcast":
                data = fields.get("data") or {}
                p = data.get("instantaneous_power_w")
                c = data.get("instantaneous_cadence_rpm")
                if p is not None or c is not None:
                    state.update(fields.get("source", "?"), p, c)

    runner = TappedRunner(sources=sources, output_path=Path(args.output))
    runner.setup()
    print(f"Capturing -> {args.output}  (wake all meters by pedalling)")
    runner.run(args.duration)  # blocks on the main thread; SIGALRM handles --duration
    state.set_capture_stopped()


def main() -> int:
    p = argparse.ArgumentParser(description="Web ride director + live dashboard")
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument("--replay", type=Path, metavar="CAPTURE.jsonl",
                      help="desk demo: replay a committed capture (no stick)")
    mode.add_argument("--live", action="store_true",
                      help="bike: live dual-meter ANT+ capture (needs the stick)")
    p.add_argument("--workout", choices=sorted(WORKOUTS), default="calibration")
    p.add_argument("--port", type=int, default=8080)
    p.add_argument("--no-browser", action="store_true", help="don't auto-open a browser")
    # replay options
    p.add_argument("--speed", type=float, default=1.0, help="replay time multiplier")
    p.add_argument("--replay-source", default="stages",
                   help="meter label for single-source captures (default: stages)")
    # live options
    p.add_argument("--stages-id", type=int, help="Stages crank device number (live)")
    p.add_argument("--assioma-id", type=int, help="Assioma device number (live)")
    p.add_argument("--fec-id", type=int, default=None, help="bike FE-C number; 0=wildcard (live)")
    p.add_argument("--duration", type=float, default=1500.0, help="live capture seconds")
    p.add_argument("--output", type=Path, help="live capture JSONL path")
    args = p.parse_args()

    if args.live and not (args.stages_id and args.assioma_id and args.output):
        p.error("--live needs --stages-id, --assioma-id and --output")

    workout = WORKOUTS[args.workout]
    if args.live:
        state = LiveState(mode="live", output=str(args.output))
        feed = _run_live
    else:
        if not args.replay.exists():
            p.error(f"capture not found: {args.replay}")
        state = LiveState(mode="replay", output=args.replay.name)
        feed = _run_replay

    director = RideDirector(workout)
    server = RideServer(state, director, port=args.port)
    server.start()
    url = f"http://localhost:{server.port}/"
    print(f"\n  Ride director:  {url}")
    print(f"  Workout:        {workout.name}  ({workout.total_s/60:.0f} min)")
    print("  Open it, start pedalling, press Start. Ctrl-C to quit.\n")
    if not args.no_browser:
        try:
            webbrowser.open(url)
        except Exception:
            pass

    try:
        if args.live:
            feed(args, state)  # blocks on the main thread (capture)
        else:
            t = threading.Thread(target=feed, args=(args, state), daemon=True)
            t.start()
        # keep the UI up after the capture/replay finishes
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nbye")
    finally:
        server.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
