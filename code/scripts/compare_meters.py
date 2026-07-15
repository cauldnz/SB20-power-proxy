#!/usr/bin/env python3
"""compare_meters.py — live A/B power-meter comparison on the desk (#10).

The Python twin of firmware/lib/proxy/MeterCompare.h: pair two power streams within a time
window and report rolling agreement — delta, mean ratio (B/A), mean bias %, and a per-power-band
bias table (do the meters diverge at high power?). Two modes:

    python compare_meters.py --demo                 # self-check: 3 scenarios, no hardware
    python compare_meters.py --live "Assioma" "SB20"   # connect two BLE CPS meters (bleak)

The head-unit shows the same numbers via firmware/lib/proxy/MeterCompareRender.h.
"""
from __future__ import annotations

import argparse
import sys
import time

BAND_W = 50
BANDS = 12


class MeterCompare:
    """Mirror of the C++ MeterCompare (pairing + rolling stats)."""

    def __init__(self, pair_window_ms=700, min_w=20, max_pairs=512):
        self.pair_window_ms = pair_window_ms
        self.min_w = min_w
        self.max_pairs = max_pairs
        self.pairs: list[tuple[int, int]] = []
        self._a = self._b = None  # (w, t, seq)
        self._sa = self._sb = 0
        self._paired = (None, None)

    def on_a(self, w, t_ms):
        self._sa += 1
        self._a = (w, t_ms, self._sa)
        self._try_pair()

    def on_b(self, w, t_ms):
        self._sb += 1
        self._b = (w, t_ms, self._sb)
        self._try_pair()

    def _try_pair(self):
        if self._a is None or self._b is None:
            return
        if abs(self._a[1] - self._b[1]) > self.pair_window_ms:
            return
        key = (self._a[2], self._b[2])
        if key == self._paired:
            return
        self._paired = key
        self.pairs.append((self._a[0], self._b[0]))
        if len(self.pairs) > self.max_pairs:
            self.pairs.pop(0)

    def stats(self):
        if not self.pairs:
            return dict(valid=False, a=0, b=0, delta=0, ratio=1.0, bias=0.0, n=0)
        used = [(a, b) for a, b in self.pairs if a >= self.min_w]
        ratio = sum(b / a for a, b in used) / len(used) if used else 1.0
        bias = sum((b - a) / a * 100 for a, b in used) / len(used) if used else 0.0
        a, b = self.pairs[-1]
        return dict(valid=True, a=a, b=b, delta=b - a, ratio=ratio, bias=bias, n=len(self.pairs))

    def bands(self):
        out = [dict(lo=i * BAND_W, n=0, ratio=0.0, bias=0.0) for i in range(BANDS)]
        acc = [[0.0, 0.0, 0] for _ in range(BANDS)]
        for a, b in self.pairs:
            if a < self.min_w:
                continue
            i = a // BAND_W
            if 0 <= i < BANDS:
                acc[i][0] += b / a
                acc[i][1] += (b - a) / a * 100
                acc[i][2] += 1
        for i in range(BANDS):
            if acc[i][2]:
                out[i].update(n=acc[i][2], ratio=acc[i][0] / acc[i][2], bias=acc[i][1] / acc[i][2])
        return out


def dashboard(a_name, b_name, mc: MeterCompare) -> str:
    s = mc.stats()
    if not s["valid"]:
        return "  (waiting for both meters...)"
    agree = -2.0 < s["bias"] < 2.0
    verdict = ("AGREE - within 2%" if agree
               else f"{b_name} reads {s['bias']:+.1f}% {'HIGH' if s['bias'] > 0 else 'LOW'}")
    lines = [
        f"  {a_name:>12}: {s['a']:>4} W     {b_name:>12}: {s['b']:>4} W",
        f"  delta {s['delta']:+d}W   ratio x{s['ratio']:.3f}   >>> {verdict}   (n={s['n']})",
        "  bias by power band:",
    ]
    for band in mc.bands():
        if not band["n"]:
            continue
        bp = max(-20.0, min(20.0, band["bias"]))
        k = int(round(abs(bp) / 20 * 20))
        bar = ("#" * k).rjust(20) if bp < 0 else ("#" * k).ljust(20)
        lines.append(f"    {band['lo']:>3}-{band['lo']+BAND_W:<3}W |{bar}| {band['bias']:+5.1f}%")
    return "\n".join(lines)


def run_demo():
    scenarios = [
        ("Assioma", "Favero2", lambda a: a),                       # agree
        ("Assioma", "SB20", lambda a: round(a * 1.11)),            # real: SB20 ~11% high (session 7)
        ("Assioma", "XCadey", lambda a: round(a * (1.0 + 0.20 * (a - 80) / 320))),  # diverges high
    ]
    for a_name, b_name, fb in scenarios:
        mc = MeterCompare()
        t = 0
        for _rep in range(6):
            for a in range(80, 401, 20):
                mc.on_a(a, t)
                mc.on_b(fb(a), t + 10)
                t += 1000
        print(f"\n=== {a_name} vs {b_name} ===")
        print(dashboard(a_name, b_name, mc))
    print("\n(demo OK — same math the head-unit Compare screen renders)")


def run_live(a_filter, b_filter):
    try:
        import asyncio
        from bleak import BleakScanner, BleakClient
    except Exception as e:  # pragma: no cover
        sys.exit(f"live mode needs bleak: {e}")

    CPS_MEAS = "00002a63-0000-1000-8000-00805f9b34fb"

    def cps_power(data: bytes) -> int:  # CPS measurement: [flags(2 LE), instant_power(2 LE, sint16)]
        return int.from_bytes(data[2:4], "little", signed=True)

    async def main():
        print(f"scanning for '{a_filter}' and '{b_filter}' ...")
        devs = await BleakScanner.discover(timeout=8.0)
        def pick(f):
            return next((d for d in devs if d.name and f.lower() in d.name.lower()), None)
        da, db = pick(a_filter), pick(b_filter)
        if not da or not db:
            sys.exit(f"couldn't find both meters (a={da}, b={db})")
        mc = MeterCompare()
        t0 = time.monotonic()

        def ms():
            return int((time.monotonic() - t0) * 1000)

        async with BleakClient(da) as ca, BleakClient(db) as cb:
            await ca.start_notify(CPS_MEAS, lambda _h, d: mc.on_a(cps_power(d), ms()))
            await cb.start_notify(CPS_MEAS, lambda _h, d: mc.on_b(cps_power(d), ms()))
            print("connected — Ctrl-C to stop\n")
            while True:
                await asyncio.sleep(1.0)
                print("\033[2J\033[H" + dashboard(a_filter, b_filter, mc))

    asyncio.run(main())


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--demo", action="store_true", help="run 3 self-check scenarios (no hardware)")
    p.add_argument("--live", nargs=2, metavar=("A_NAME", "B_NAME"), help="two BLE CPS meter name filters")
    a = p.parse_args()
    if a.live:
        run_live(*a.live)
    else:
        run_demo()
