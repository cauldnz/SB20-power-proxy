#!/usr/bin/env python3
"""compare_meters.py — live A/B power-meter comparison on the desk (#10): the CLI/bleak seam.

The pairing + rolling-stats core is :class:`sb20proxy.compare.MeterCompare` (the host-tested twin of
``firmware/lib/proxy/MeterCompare.h``, parity-guarded by ``tests/test_compare_parity.py``); this
script is only the seam around it — argument parsing, the terminal dashboard, the no-hardware
``--demo`` self-check, and the two-meter bleak wiring for ``--live``. That mirrors how
:mod:`sb20proxy.ble.multi_capture` is the pure module and ``scripts/capture_ble_multi.py`` is its
seam. The head-unit renders the same numbers on its LVGL Compare screen (``firmware/src/ui/``); the
web app's deep-dive reads them from ``GET /compare``.

    python compare_meters.py --demo                    # self-check: 3 scenarios, no hardware
    python compare_meters.py --live "Assioma" "SB20"   # connect two BLE CPS meters (bleak)
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

from sb20proxy.compare import BAND_W, TORQUE_BAND_NM, MeterCompare  # noqa: E402


def _bias_bar(bias_pct: float) -> str:
    """A 20-char signed bar: right-justified '#'s for LOW (B under A), left for HIGH."""
    bp = max(-20.0, min(20.0, bias_pct))
    k = int(round(abs(bp) / 20 * 20))
    return ("#" * k).rjust(20) if bp < 0 else ("#" * k).ljust(20)


def dashboard(a_name: str, b_name: str, mc: MeterCompare) -> str:
    s = mc.stats()
    if not s.valid:
        return "  (waiting for both meters...)"
    verdict = ("AGREE - within 2%" if s.agrees()
               else f"{b_name} reads {s.mean_bias_pct:+.1f}% "
                    f"{'HIGH' if s.mean_bias_pct > 0 else 'LOW'}")
    lines = [
        f"  {a_name:>12}: {s.a_watts:>4} W     {b_name:>12}: {s.b_watts:>4} W",
        f"  delta {s.delta_w:+d}W   ratio x{s.mean_ratio:.3f}   >>> {verdict}   (n={s.n_pairs})",
        "  bias by power band:",
    ]
    for band in mc.bands():
        if band.n_pairs:
            lines.append(f"    {band.lo_w:>3}-{band.lo_w + BAND_W:<3}W "
                         f"|{_bias_bar(band.mean_bias_pct)}| {band.mean_bias_pct:+5.1f}%")
    torque = [b for b in mc.torque_bands() if b.n_pairs]
    if torque:  # only when the meters gave us cadence — the domain that unmasks torque error
        lines.append("  bias by torque band (reveals torque-dependent error the power view hides):")
        for band in torque:
            lines.append(f"    {band.lo_w:>2}-{band.lo_w + TORQUE_BAND_NM:<2}Nm "
                         f"|{_bias_bar(band.mean_bias_pct)}| {band.mean_bias_pct:+5.1f}%")
    return "\n".join(lines)


def run_demo() -> None:
    scenarios = [
        ("Assioma", "Favero2", lambda a: a),                       # agree
        ("Assioma", "SB20", lambda a: round(a * 1.11)),            # real: SB20 ~11% high (session 7)
        ("Assioma", "XCadey", lambda a: round(a * (1.0 + 0.20 * (a - 80) / 320))),  # diverges high
    ]
    for a_name, b_name, fb in scenarios:
        mc = MeterCompare()
        t = 0
        for _rep in range(6):
            for cad in (70, 90):  # two cadences => a spread of torque, so the torque view populates
                for a in range(80, 401, 20):
                    mc.on_a(a, t, cad)
                    mc.on_b(fb(a), t + 10, cad)
                    t += 1000
        print(f"\n=== {a_name} vs {b_name} ===")
        print(dashboard(a_name, b_name, mc))
    print("\n(demo OK — same math the head-unit Compare screen renders)")


def run_live(a_filter: str, b_filter: str) -> None:
    try:
        import asyncio

        from bleak import BleakClient, BleakScanner
    except Exception as e:  # pragma: no cover
        sys.exit(f"live mode needs bleak: {e}")

    # The project's CPS codec — the twin of firmware/lib/proxy/Cps.h. Parsing the notification by its
    # FLAGS (rather than slicing data[2:4] blind) is what makes this correct on meters that vary the
    # optional fields.
    from sb20proxy.ble.cps import UUID_CP_MEASUREMENT, decode_cps_measurement
    from sb20proxy.ble.multi_capture import sig_uuid

    cps_meas = sig_uuid(UUID_CP_MEASUREMENT)

    def cps_power(data: bytes) -> int:
        return decode_cps_measurement(data).power_w

    async def main() -> None:
        print(f"scanning for '{a_filter}' and '{b_filter}' ...")
        devs = await BleakScanner.discover(timeout=8.0)

        def pick(f: str):
            return next((d for d in devs if d.name and f.lower() in d.name.lower()), None)

        da, db = pick(a_filter), pick(b_filter)
        if not da or not db:
            sys.exit(f"couldn't find both meters (a={da}, b={db})")
        mc = MeterCompare()
        t0 = time.monotonic()

        def ms() -> int:
            return int((time.monotonic() - t0) * 1000)

        async with BleakClient(da) as ca, BleakClient(db) as cb:
            await ca.start_notify(cps_meas, lambda _h, d: mc.on_a(cps_power(d), ms()))
            await cb.start_notify(cps_meas, lambda _h, d: mc.on_b(cps_power(d), ms()))
            print("connected — Ctrl-C to stop\n")
            while True:
                await asyncio.sleep(1.0)
                print("\033[2J\033[H" + dashboard(a_filter, b_filter, mc))

    asyncio.run(main())


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--demo", action="store_true", help="run 3 self-check scenarios (no hardware)")
    p.add_argument("--live", nargs=2, metavar=("A_NAME", "B_NAME"),
                   help="two BLE CPS meter name filters")
    a = p.parse_args()
    if a.live:
        run_live(*a.live)
    else:
        run_demo()
