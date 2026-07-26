#!/usr/bin/env python3
"""crank_reader.py — read the ESP32's spoofed Stages crank over BLE (bleak central).

The over-the-air counterpart of BleSb20Twin: scans for the ESP32's Cycling Power peripheral,
subscribes to CPS Measurement (0x2A63), decodes power, and recovers cadence from the
crank-revolution deltas exactly as a head unit (or the SB20) does. This is the test-harness
half of **goal #2** — the ESP32's broadcast being received by the Python app.

Match is by advertised NAME by default ("Stages", the firmware SPOOF_NAME) so it locks onto
the ESP32 and ignores the nameless WinRT fake_meter, which also advertises CPS. Run both
fake_meter.py and this together and you've proved the whole chain: Python meter -> ESP32 ->
Python reader.

Usage (PowerShell, from code/, with the venv active):
    python scripts/crank_reader.py                  # find a CPS device named ~"Stages"
    python scripts/crank_reader.py --name 62144     # narrower name match
    python scripts/crank_reader.py --address <addr> # connect to a specific address
    python scripts/crank_reader.py --zero           # also fire a zero-reset and show the reply

Ctrl-C to stop.

References:
- BLE Cycling Power Service 0x1818, Measurement 0x2A63, Control Point 0x2A66
- Decoder/cadence reused from sb20proxy.ble.cps (the same codec the firmware mirrors)
"""

from __future__ import annotations

import argparse
import asyncio
import sys
import time

try:
    from bleak import BleakClient, BleakScanner
except ImportError as e:  # pragma: no cover - dependency guard
    _msg = f"bleak not installed: {e}\nRun: pip install -e \"code/.[ble]\""
    if __name__ == "__main__":
        print(_msg, file=sys.stderr)
        sys.exit(1)
    raise ImportError(_msg) from e  # importable: let callers skip, don't kill them

from sb20proxy.ble.cps import (
    CP_START_OFFSET_COMPENSATION,
    ControlPointResponse,
    cadence_rpm_from_crank,
    decode_control_point,
    decode_cps_measurement,
)


def _sig_uuid(short: int) -> str:
    return f"0000{short:04x}-0000-1000-8000-00805f9b34fb"


CPS_UUID = _sig_uuid(0x1818)
CP_MEASUREMENT = _sig_uuid(0x2A63)
CP_CONTROL_POINT = _sig_uuid(0x2A66)


async def find_device(args: argparse.Namespace):
    if args.address:
        print(f"Looking for {args.address} ...")
        return await BleakScanner.find_device_by_address(args.address, timeout=args.timeout)

    print(f"Scanning {args.timeout:.0f}s for a CPS crank named ~'{args.name}'"
          f"{' (or any CPS device)' if args.any_cps else ''} ...")

    def match(device, adv) -> bool:
        name = device.name or getattr(adv, "local_name", None) or ""
        if args.name and args.name.lower() in name.lower():
            return True
        if args.any_cps:
            uuids = [u.lower() for u in (adv.service_uuids or [])]
            return CPS_UUID in uuids
        return False

    return await BleakScanner.find_device_by_filter(match, timeout=args.timeout)


async def run(args: argparse.Namespace) -> None:
    device = await find_device(args)
    if device is None:
        print("No crank found. Is the ESP32 advertising (check its OLED/web UI)?")
        return
    print(f"Found '{device.name}' [{device.address}] — connecting ...")

    prev_crank: list[tuple[int, int] | None] = [None]
    count = [0]

    def on_measurement(_char, data: bytearray) -> None:
        raw = bytes(data)
        m = decode_cps_measurement(raw)
        cad = ""
        if m.cumulative_crank_revs is not None and m.last_crank_event_time is not None:
            if prev_crank[0] is not None:
                rpm = cadence_rpm_from_crank(prev_crank[0][0], prev_crank[0][1],
                                             m.cumulative_crank_revs, m.last_crank_event_time)
                if rpm > 0:
                    cad = f"   {round(rpm):3d} rpm"
            prev_crank[0] = (m.cumulative_crank_revs, m.last_crank_event_time)
        bal = ""
        if m.balance_pct is not None:
            bal = f"   L{m.balance_pct:.0f}%/R{100 - m.balance_pct:.0f}%"
        count[0] += 1
        print(f"  [{count[0]:4d}] {m.power_w:4d} W{cad}{bal}      raw={raw.hex()}")

    def on_control_point(_char, data: bytearray) -> None:
        decoded = decode_control_point(bytes(data))
        if isinstance(decoded, ControlPointResponse):
            print(f"  zero-reset reply: success={decoded.success} offset={decoded.offset} "
                  f"(raw={bytes(data).hex()})")
        else:
            print(f"  control-point indication: {bytes(data).hex()}")

    async with BleakClient(device) as client:
        print("Connected. Subscribing to CPS Measurement (0x2A63) ...\n")
        await client.start_notify(CP_MEASUREMENT, on_measurement)
        if args.zero:
            try:
                await client.start_notify(CP_CONTROL_POINT, on_control_point)
                await asyncio.sleep(1.0)
                print("Writing Start Offset Compensation (BLE zero-reset) ...")
                await client.write_gatt_char(CP_CONTROL_POINT,
                                             bytes([CP_START_OFFSET_COMPENSATION]), response=True)
            except Exception as e:  # noqa: BLE001 - report and keep streaming
                print(f"  (zero-reset not available: {e})")
        deadline = time.monotonic() + args.seconds if args.seconds > 0 else None
        while client.is_connected:
            if deadline is not None and time.monotonic() >= deadline:
                print(f"\n(--seconds {args.seconds} elapsed)")
                break
            await asyncio.sleep(0.5)
    print("\ndisconnected.")


def main() -> None:
    ap = argparse.ArgumentParser(description="Read the ESP32 spoofed crank over BLE (central).")
    ap.add_argument("--name", default="Stages", help="advertised-name substring to match")
    ap.add_argument("--address", help="connect to a specific BLE address instead of scanning")
    ap.add_argument("--any-cps", action="store_true",
                    help="also match any device advertising the CPS service UUID")
    ap.add_argument("--zero", action="store_true",
                    help="fire a Start Offset Compensation and print the indicated reply")
    ap.add_argument("--timeout", type=float, default=15.0, help="scan timeout (s)")
    ap.add_argument("--seconds", type=float, default=0.0,
                    help="auto-stop after N seconds of streaming (0 = run until Ctrl-C)")
    args = ap.parse_args()
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        print("\nstopped.")


if __name__ == "__main__":
    main()
