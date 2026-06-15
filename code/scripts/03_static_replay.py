#!/usr/bin/env python3
"""Phase 1 static replay — broadcast a captured Stages stream as a spoofed crank.

Default mode is a pure-software LOOPBACK against a BikeTwin (digital twin of the
SB20): no ANT+ stick, no bike — proves the whole pipeline end to end and prints
what the twin "sees". Switch to a real stick with --radio ant for the bench / SB20.

Examples
--------
  # Software loopback (no hardware) — replay decoded power into a BikeTwin:
  python scripts/03_static_replay.py \
      --input findings/captures/A-stagesL-steady-20260614-165737.jsonl \
      --request-zero

  # Verbatim page replay through the loopback:
  python scripts/03_static_replay.py --input <capture.jsonl> --mode verbatim

  # Real ANT+ stick, spoofing a distinct test id (see NEXT-BIKE-SESSION.md §7):
  python scripts/03_static_replay.py --input <capture.jsonl> --radio ant --spoof-id 62145
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from sb20proxy.ant.master import ChannelParams, LoopbackMaster  # noqa: E402
from sb20proxy.core import ProxyCore  # noqa: E402
from sb20proxy.sources.replay import ReplayFileSource  # noqa: E402
from sb20proxy.targets.stages_ant import StagesAntTarget  # noqa: E402
from sb20proxy.twins import BikeTwin  # noqa: E402


def load_pages(path: Path) -> list[bytes]:
    """Read the raw 8-byte pages from a capture JSONL (for verbatim replay)."""
    pages: list[bytes] = []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            if rec.get("kind") not in ("broadcast", "acknowledged"):
                continue
            raw_hex = (rec.get("data") or {}).get("raw_hex")
            if not raw_hex:
                continue
            raw = bytes.fromhex(raw_hex)[:8]
            if len(raw) == 8:
                pages.append(raw)
    if not pages:
        raise ValueError(f"{path}: no pages found to replay")
    return pages


async def run(args: argparse.Namespace) -> int:
    params = ChannelParams(device_number=args.spoof_id)

    if args.radio == "ant":
        from sb20proxy.ant.openant_master import OpenAntMaster
        master = OpenAntMaster(params)
        twin = None
    else:
        master = LoopbackMaster(period_s=args.period)
        twin = BikeTwin.over_loopback(master)
        await twin.start()

    # Build the target (+ source for decoded mode).
    source = None
    if args.mode == "verbatim":
        target = StagesAntTarget(master, mode="verbatim", verbatim_pages=load_pages(args.input))
    else:
        source = ReplayFileSource(args.input, loop=args.loop, speed=args.speed)
        target = StagesAntTarget(master, mode="decoded")

    print(f"[replay] mode={args.mode} radio={args.radio} spoof-id={args.spoof_id} "
          f"input={Path(args.input).name}")

    core = ProxyCore(source, target) if source is not None else None
    if core is not None:
        await core.start()
    else:
        await target.start()

    try:
        elapsed = 0.0
        tick = 1.0
        requested = False
        while elapsed < args.duration:
            await asyncio.sleep(tick)
            elapsed += tick
            want_zero = args.request_zero and twin is not None and not requested
            if want_zero and elapsed >= args.duration / 2:
                print("[replay] BikeTwin -> requesting zero-reset")
                twin.request_zero()
                requested = True
            if twin is not None:
                print(f"  {elapsed:4.0f}s {twin.summary()}")
    finally:
        if core is not None:
            await core.stop()
        else:
            await target.stop()

    if twin is not None:
        ok = twin.saw_power and twin.manufacturer_id == 69
        print(f"[replay] final: {twin.summary()}")
        print(f"[replay] {'PASS' if ok else 'CHECK'}: twin "
              f"{'saw spoofed Stages power' if ok else 'did not see expected data'}")
        return 0 if ok else 1
    print("[replay] broadcasting on real ANT+ — pair the SB20 to the spoofed id.")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description="Phase 1 static replay / loopback")
    p.add_argument("--input", type=Path, required=True, help="capture JSONL to replay")
    p.add_argument("--spoof-id", type=int, default=62145,
                   help="ANT+ device number to broadcast as (default 62145, a distinct test id)")
    p.add_argument("--mode", choices=["decoded", "verbatim"], default="decoded")
    p.add_argument("--radio", choices=["loopback", "ant"], default="loopback",
                   help="loopback = software BikeTwin (no hardware); ant = real stick")
    p.add_argument("--duration", type=float, default=8.0, help="seconds to run (loopback demo)")
    p.add_argument("--speed", type=float, default=1.0, help="replay speed multiplier (decoded)")
    p.add_argument("--period", type=float, default=0.25, help="loopback broadcast period (s)")
    p.add_argument("--loop", action="store_true", help="loop the capture when it ends")
    p.add_argument("--request-zero", action="store_true",
                   help="(loopback) have the BikeTwin request a zero-reset mid-run")
    args = p.parse_args()
    return asyncio.run(run(args))


if __name__ == "__main__":
    raise SystemExit(main())
