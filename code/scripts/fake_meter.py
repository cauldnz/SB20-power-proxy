#!/usr/bin/env python3
"""fake_meter.py — advertise a spoofed BLE power meter (Cycling Power Service) on Windows.

The over-the-air counterpart of the in-process loopback: stands up a real CPS peripheral via
WinRT and notifies a power + cadence stream, so the ESP32 BLE *central* (firmware
BleMeterClient) can connect and receive it. This is the test-harness half of **goal #1** —
the ESP32 receiving power-meter data from the Python app.

The ESP32 matches the meter by the advertised CPS service UUID (WinRT doesn't advertise a
custom name), so nothing else need be configured on the firmware side.

Usage (PowerShell, from code/, with the venv active):
    python scripts/fake_meter.py                       # 150 W triangle ramp @ 85 rpm, 1 Hz
    python scripts/fake_meter.py --watts 200 --steady  # constant 200 W
    python scripts/fake_meter.py --cadence 0           # power only, no crank/cadence data

Windows 10+ only (WinRT GATT server). Ctrl-C to stop.

References:
- BLE Cycling Power Service 0x1818, Measurement 0x2A63 (field order in sb20proxy.ble.cps)
- WinRT peripheral: sb20proxy.ble.winrt_peripheral (bless is incompatible with Python 3.13)
"""

from __future__ import annotations

import argparse
import asyncio
import sys
import time

from sb20proxy.ble.cps import F_BALANCE_REF_LEFT, CrankCadence, encode_cps_measurement

try:
    from sb20proxy.ble.winrt_peripheral import WinrtCpsPeripheral
except ImportError as e:  # pragma: no cover - platform guard
    print(f"WinRT GATT server unavailable (Windows 10+ only): {e}", file=sys.stderr)
    sys.exit(1)


def _power_at(base: int, tick: int, steady: bool) -> int:
    if steady:
        return base
    phase = tick % 40           # 0..39
    return base + (phase if phase < 20 else 40 - phase) * 5  # triangle, +/-100 W swing


async def run(args: argparse.Namespace) -> None:
    periph = WinrtCpsPeripheral()
    await periph.start()
    print(f"Advertising spoofed power meter (CPS 0x1818), advertising={periph.advertising}.")
    print("Waiting for a central (the ESP32) to connect and subscribe. Ctrl-C to stop.\n")
    try:
        await _stream(periph, args)
    finally:
        periph.stop()  # stop advertising so a connected central sees the meter go away cleanly


async def _stream(periph: WinrtCpsPeripheral, args: argparse.Namespace) -> None:
    cadence = CrankCadence()
    period = 1.0 / max(0.1, args.hz)
    last_subs = -1
    tick = 0
    deadline = time.monotonic() + args.duration if args.duration > 0 else None
    while True:
        if deadline is not None and time.monotonic() >= deadline:
            print(f"\n(--duration {args.duration}s elapsed) stopping advertising cleanly")
            break
        watts = _power_at(args.watts, tick, args.steady)
        fields: dict[str, int] = {}
        if args.cadence > 0:
            cadence.advance(float(args.cadence), int(period * 1000))
            fields = {
                "cumulative_crank_revs": cadence.cumulative_revs,
                "last_crank_event_time": cadence.last_event_time,
            }
        if args.balance is not None:
            # left-referenced L/R split, like the Assioma DUO: byte = left% × 2 (1/2-% units).
            fields["pedal_balance"] = round(args.balance * 2)
            fields["extra_flags"] = F_BALANCE_REF_LEFT
        await periph.notify(encode_cps_measurement(watts, **fields))

        if periph.subscriber_count != last_subs:
            last_subs = periph.subscriber_count
            state = "connected" if last_subs else "no subscribers yet"
            print(f"\n[subscribers={last_subs}] {state}")
        cad = f"{args.cadence:3d} rpm" if args.cadence > 0 else " (no cadence)"
        bal = f"  L{args.balance:.0f}%" if args.balance is not None else ""
        print(f"  tx {watts:4d} W  {cad}{bal}   subs={periph.subscriber_count}   ",
              end="\r", flush=True)

        tick += 1
        await asyncio.sleep(period)


def main() -> None:
    ap = argparse.ArgumentParser(description="Advertise a spoofed BLE power meter (CPS).")
    ap.add_argument("--watts", type=int, default=150, help="base power in watts (default 150)")
    ap.add_argument("--cadence", type=int, default=85,
                    help="cadence in rpm; 0 = send power only, no crank data (default 85)")
    ap.add_argument("--steady", action="store_true", help="constant power (no triangle ramp)")
    ap.add_argument("--balance", type=float, default=None,
                    help="left pedal %% to emit as L/R balance (e.g. 44 = 44%%L/56%%R); "
                         "omit = no balance field (single-sided)")
    ap.add_argument("--hz", type=float, default=1.0, help="notifications per second (default 1)")
    ap.add_argument("--duration", type=float, default=0.0,
                    help="stop advertising and exit cleanly after N seconds (0 = run until Ctrl-C)")
    args = ap.parse_args()
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        print("\nstopped.")


if __name__ == "__main__":
    main()
