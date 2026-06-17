"""Stand up a FAITHFUL Stages crank on the PC (WinRT) and decode every control-point write a
central makes — the fast-iterate rig for the SB20 handshake (Session G Part B), in Python.

    code\\.venv-win\\Scripts\\python.exe code\\scripts\\fake_crank.py [--seconds N]
    (needs PYTHONPATH=code/src; winrt is Windows-only)

Emits the real Stages 0x2F frame (power + pedal-balance + accumulated-torque + crank-rev cadence,
power ramped 100..300 W) and exposes the Cycling Power Control Point — printing + capturing every
write a connected central sends. Point a central at it and watch the handshake.

IMPORTANT (see PC-CRANK.md): WinRT's GattServiceProvider advertises under the PC's *system*
Bluetooth name, NOT a custom "Stages 62144" — and the SB20 pairs the crank by name. So the SB20
will only see this as the crank if you first rename the PC's Bluetooth name to "Stages 62144".
For the reliable SB20 handshake capture, use the ESP32 + its /log instead. This rig is ideal for a
phone CPS app / a bleak client / a renamed-PC SB20 test, and for fast Python iteration once the
SB20 does write to us.
"""

from __future__ import annotations

import argparse
import asyncio

from sb20proxy.ble import cps
from sb20proxy.ble.winrt_peripheral import WinrtCpsPeripheral

STAGES_FEATURE = 0x0008030B  # the real Stages SPM2 CP Feature, captured 2026-06-17


def _on_write(data: bytes) -> None:
    decoded = cps.decode_control_point(data)
    print(f"  [central -> crank] control-point write: {data.hex()}  ->  {decoded}", flush=True)


async def run(seconds: int) -> None:
    cadence = cps.CrankCadence()
    crank = WinrtCpsPeripheral(feature_bits=STAGES_FEATURE, sensor_location=0, on_write=_on_write)
    await crank.start()
    print("Faithful Stages crank up: CPS (power+cadence) + control-point.", flush=True)
    print("NOTE: advertises under THIS PC's Bluetooth name, not 'Stages 62144' (see PC-CRANK.md).",
          flush=True)

    acc_torque = 0
    for t in range(seconds):
        power = 100 + (t % 40 if t % 40 < 20 else 40 - t % 40) * 10  # 100..300..100 ramp
        cadence.advance(85.0, 1000)
        acc_torque = (acc_torque + power // 4) & 0xFFFF  # synthetic, monotonic; SB20 reads power
        frame = cps.encode_cps_measurement(
            power,
            pedal_balance=100,  # 50.0% (single source, no L/R split)
            accumulated_torque=acc_torque,
            cumulative_crank_revs=cadence.cumulative_revs,
            last_crank_event_time=cadence.last_event_time,
            extra_flags=cps.F_BALANCE_REF_LEFT | cps.F_TORQUE_SOURCE_CRANK,
        )
        await crank.notify(frame)
        if t == 0:
            print(f"  frame[0] = {frame.hex()}  (flags=0x{frame[0] | (frame[1] << 8):04x}, "
                  f"subscribers={crank.subscriber_count})", flush=True)
        await asyncio.sleep(1.0)

    crank.stop()
    print(f"\nDone. Captured {len(crank.writes)} control-point write(s):", flush=True)
    for w in crank.writes:
        print(f"  {w.hex()}  ->  {cps.decode_control_point(w)}", flush=True)


def main() -> None:
    ap = argparse.ArgumentParser(description="Faithful Stages crank on the PC (WinRT) + write capture")
    ap.add_argument("--seconds", type=int, default=600, help="how long to advertise + emit (default 600)")
    asyncio.run(run(ap.parse_args().seconds))


if __name__ == "__main__":
    main()
