"""sb20proxy console entry point — run the live proxy from a TOML config (or validate it).

  sb20proxy --config sb20proxy.toml --validate-config   # check the config, exit
  sb20proxy --config sb20proxy.toml                      # run the live proxy (needs 2 sticks)

For bench/dev runs without a config, use code/scripts/03_static_replay.py (loopback or
on-air) and 04_run_proxy.py.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from sb20proxy.config import ProxyConfig, load_config


def _describe(cfg: ProxyConfig) -> str:
    return (
        f"meter {cfg.meter_id} ({cfg.source_label}) -> spoof crank {cfg.spoof_id} | "
        f"sticks src#{cfg.source_usb_index}/tgt#{cfg.target_usb_index} | "
        f"correction {type(cfg.build_transform()).__name__}"
    )


async def _run_live(cfg: ProxyConfig) -> None:
    import asyncio

    from sb20proxy.ant.master import ChannelParams
    from sb20proxy.ant.openant_master import OpenAntMaster
    from sb20proxy.ant.usb_select import select_ant_stick
    from sb20proxy.core import ProxyCore
    from sb20proxy.sources.ant_power import AntPowerSource
    from sb20proxy.targets.stages_ant import StagesAntTarget
    from sb20proxy.twins.transport import AntSlaveTransport

    source = AntPowerSource(
        AntSlaveTransport(
            ChannelParams(device_number=cfg.meter_id),
            usb_device=select_ant_stick(cfg.source_usb_index),
        ),
        source_id=cfg.source_label,
    )
    target = StagesAntTarget(
        OpenAntMaster(
            ChannelParams(device_number=cfg.spoof_id),
            usb_device=select_ant_stick(cfg.target_usb_index),
        ),
        commons_every=cfg.commons_every,
    )
    proxy = ProxyCore(source, target, cfg.build_transform())
    await proxy.start()
    print(f"[sb20proxy] running — {_describe(cfg)}. Ctrl-C to stop.")
    try:
        await asyncio.Event().wait()  # run until interrupted
    finally:
        await proxy.stop()


def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        prog="sb20proxy", description="ANT+ power-meter proxy for the Stages SB20"
    )
    p.add_argument("--config", type=Path, help="proxy config TOML (see sb20proxy.example.toml)")
    p.add_argument("--validate-config", action="store_true", help="validate the config and exit")
    args = p.parse_args(argv)

    if not args.config:
        print("sb20proxy needs a config: --config <file.toml> (see sb20proxy.example.toml).")
        print("For bench/dev runs use code/scripts/03_static_replay.py or 04_run_proxy.py.")
        return 1

    try:
        cfg = load_config(args.config)
    except Exception as exc:
        print(f"config error: {exc}", file=sys.stderr)
        return 2

    errors = cfg.validate()
    if errors:
        print("config invalid:", file=sys.stderr)
        for err in errors:
            print(f"  - {err}", file=sys.stderr)
        return 1

    print(f"config OK — {_describe(cfg)}")
    if args.validate_config:
        return 0

    # Live run (two ANT+ sticks + the meter). Force-exit past openant's worker threads.
    import asyncio
    import os

    rc = 2
    try:
        asyncio.run(_run_live(cfg))
        rc = 0
    except BaseException as exc:
        print(f"[sb20proxy] error: {exc}", file=sys.stderr)
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(rc)


if __name__ == "__main__":
    raise SystemExit(main())
