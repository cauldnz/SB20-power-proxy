#!/usr/bin/env python3
"""Run a BikeTwin over a real ANT+ stick — the receive side of an on-air loopback.

This is the digital twin hooked to the real radio stack. It listens as an ANT+ slave
for a given device id and prints what it sees — exactly like the software loopback in
03_static_replay.py, but over the air. Pair it with a transmitter on a SECOND stick
(or a real power meter / the SB20 broadcasting on that id).

Example (two sticks):
  # stick A — broadcast a spoofed crank:
  python scripts/03_static_replay.py --radio ant --input <cap.jsonl> --spoof-id 62145
  # stick B — receive it as a BikeTwin and exercise the zero-reset:
  python scripts/10_bike_twin.py --device-id 62145 --request-zero

Needs a real ANT+ stick (openant). For a no-hardware demo use
03_static_replay.py --radio loopback instead.
"""

from __future__ import annotations

import argparse
import asyncio
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from sb20proxy.ant.master import ChannelParams  # noqa: E402
from sb20proxy.twins import BikeTwin  # noqa: E402
from sb20proxy.twins.transport import AntSlaveTransport  # noqa: E402


async def run(args: argparse.Namespace) -> int:
    from sb20proxy.ant.usb_select import describe_sticks, select_ant_stick

    params = ChannelParams(device_number=args.device_id)
    print(f"[bike-twin] ANT+ sticks: {describe_sticks()}")
    twin = BikeTwin(AntSlaveTransport(params, usb_device=select_ant_stick(args.usb_index)))
    print(f"[bike-twin] listening on ANT+ id {args.device_id} for {args.duration:.0f}s "
          f"— start the transmitter (rotate/start the meter).")

    await twin.start()
    try:
        elapsed = 0.0
        requested = False
        while elapsed < args.duration:
            await asyncio.sleep(1.0)
            elapsed += 1.0
            if args.request_zero and not requested and elapsed >= 3 and twin.saw_power:
                print("[bike-twin] -> requesting zero-reset")
                twin.request_zero()
                requested = True
            print(f"  {elapsed:4.0f}s {twin.summary()}")
    finally:
        await twin.stop()

    ok = twin.saw_power
    print(f"[bike-twin] {'PASS' if ok else 'NO DATA'}: {twin.summary()}")
    return 0 if ok else 1


def main() -> int:
    p = argparse.ArgumentParser(description="Run a BikeTwin over a real ANT+ stick (RX loopback)")
    p.add_argument("--device-id", type=int, required=True,
                   help="ANT+ id to listen for (the transmitter's spoof-id)")
    p.add_argument("--usb-index", type=int, default=0,
                   help="which ANT+ stick to receive on (0=first), for multi-stick hosts")
    p.add_argument("--duration", type=float, default=30.0, help="seconds to listen")
    p.add_argument("--request-zero", action="store_true",
                   help="request a zero-reset once power is seen")
    args = p.parse_args()
    rc = 2
    try:
        rc = asyncio.run(run(args))
    except BaseException as exc:
        print(f"[bike-twin] error: {exc}", file=sys.stderr)
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(rc)  # terminate past openant's non-daemon worker threads (even on failure)


if __name__ == "__main__":
    main()
