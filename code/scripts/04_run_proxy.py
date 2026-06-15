#!/usr/bin/env python3
"""Phase 2 live proxy — read a power meter, (optionally correct,) broadcast as a crank.

  meter (ANT+ Bike Power) -> AntPowerSource -> [correction] -> StagesAntTarget -> SB20

Default `--radio loopback` runs the WHOLE chain in software with digital twins (a
PowerMeterTwin driven by a captured ride feeds the proxy, a BikeTwin consumes the
spoofed crank) — no hardware. `--radio ant` runs it for real: listen to `--meter-id`,
broadcast as `--spoof-id`.

Correction (the quantitative meter-to-meter calibration): `--scale` / `--offset` apply
a linear correction now; a fitted power-grid profile is the non-linear next step.

Examples
--------
  # Software demo, meter reports 8% high, correct it back:
  python scripts/04_run_proxy.py --input findings/captures/A-stagesL-steady-20260614-165737.jsonl \
      --meter-error-pct 8 --scale 0.926

  # Real proxy: Assioma 17039 -> spoofed crank 62145:
  python scripts/04_run_proxy.py --radio ant --meter-id 17039 --spoof-id 62145
"""

from __future__ import annotations

import argparse
import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))

from sb20proxy.ant.master import ChannelParams, LoopbackMaster  # noqa: E402
from sb20proxy.core import ProxyCore  # noqa: E402
from sb20proxy.sources.ant_power import AntPowerSource  # noqa: E402
from sb20proxy.sources.replay import ReplayFileSource  # noqa: E402
from sb20proxy.targets.stages_ant import StagesAntTarget  # noqa: E402
from sb20proxy.transform import IdentityTransform, ScaleOffsetTransform  # noqa: E402
from sb20proxy.twins import BikeTwin, LoopbackTransport, PowerMeterTwin  # noqa: E402


def build_transform(args):
    if args.scale != 1.0 or args.offset != 0.0:
        return ScaleOffsetTransform(scale=args.scale, offset=args.offset)
    return IdentityTransform()


async def run_ant(args) -> int:
    from sb20proxy.ant.openant_master import OpenAntMaster
    from sb20proxy.twins.transport import AntSlaveTransport

    source = AntPowerSource(
        AntSlaveTransport(ChannelParams(device_number=args.meter_id)),
        source_id=f"meter:{args.meter_id}",
    )
    target = StagesAntTarget(OpenAntMaster(ChannelParams(device_number=args.spoof_id)))
    proxy = ProxyCore(source, target, build_transform(args))
    print(f"[proxy] LIVE meter {args.meter_id} -> spoofed crank {args.spoof_id} "
          f"(correction: {type(proxy._transform).__name__})")
    await proxy.start()
    try:
        await asyncio.sleep(args.duration)
    finally:
        await proxy.stop()
    print("[proxy] stopped.")
    return 0


async def run_loopback(args) -> int:
    # meter side: a PowerMeterTwin (optionally with a % error) fed by a captured ride.
    err_pct = args.meter_error_pct / 100.0
    meter_bus = LoopbackMaster(period_s=args.period)
    meter = PowerMeterTwin(meter_bus, error=lambda p, c: p * (1.0 + err_pct))
    rider = ReplayFileSource(args.input, loop=args.loop, speed=args.speed)
    rider_meter = ProxyCore(rider, meter)

    # proxy: read the meter, correct, broadcast as a spoofed crank.
    source = AntPowerSource(LoopbackTransport(meter_bus), source_id="meter")
    crank_bus = LoopbackMaster(period_s=args.period)
    target = StagesAntTarget(crank_bus, mode="decoded")
    proxy = ProxyCore(source, target, build_transform(args))

    # bike: a BikeTwin consuming the spoofed crank.
    bike = BikeTwin.over_loopback(crank_bus)

    print(f"[proxy] LOOPBACK rider->meter(+{args.meter_error_pct:.0f}% err)->proxy"
          f"(correction {type(proxy._transform).__name__})->bike-twin")
    await bike.start()
    await proxy.start()        # registers source on meter_bus; opens crank_bus
    await rider_meter.start()  # opens meter_bus; starts the rider
    try:
        elapsed = 0.0
        while elapsed < args.duration:
            await asyncio.sleep(1.0)
            elapsed += 1.0
            raw = proxy.stats.last_source_reading
            sent = proxy.stats.last_reading
            raw_w = raw.power_w if raw else None
            sent_w = sent.power_w if sent else None
            print(f"  {elapsed:4.0f}s meter={raw_w}W -> corrected={sent_w}W | "
                  f"bike sees {bike.last_power}W (mfr {bike.manufacturer_id})")
    finally:
        await rider_meter.stop()
        await proxy.stop()
        await bike.stop()
    ok = bike.saw_power
    print(f"[proxy] {'PASS' if ok else 'NO DATA'}: bike twin saw {bike.last_power}W")
    return 0 if ok else 1


def main() -> int:
    p = argparse.ArgumentParser(description="Phase 2 live proxy (meter -> spoofed crank)")
    p.add_argument("--radio", choices=["loopback", "ant"], default="loopback")
    p.add_argument("--input", type=Path, help="capture to drive the meter twin (loopback mode)")
    p.add_argument("--meter-id", type=int, default=17039, help="meter device id (ant mode)")
    p.add_argument("--spoof-id", type=int, default=62145, help="crank id to broadcast as")
    p.add_argument("--scale", type=float, default=1.0, help="linear correction scale")
    p.add_argument("--offset", type=float, default=0.0, help="linear correction offset (W)")
    p.add_argument("--meter-error-pct", type=float, default=0.0,
                   help="(loopback) inject a %% error into the meter twin to test correction")
    p.add_argument("--duration", type=float, default=8.0)
    p.add_argument("--speed", type=float, default=1.0)
    p.add_argument("--period", type=float, default=0.25)
    p.add_argument("--loop", action="store_true")
    args = p.parse_args()

    if args.radio == "ant":
        return asyncio.run(run_ant(args))
    if not args.input:
        p.error("--input is required for --radio loopback")
    return asyncio.run(run_loopback(args))


if __name__ == "__main__":
    raise SystemExit(main())
