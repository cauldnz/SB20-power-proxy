#!/usr/bin/env python3
"""qa_board.py — pre-ship acceptance test for a flashed SB20-proxy board.

The pre-beta plan ships ~10 pre-flashed boards. This is the gate each one passes before it goes in
an envelope: (optionally) flash it, then confirm off the air that it (a) advertises as the spoof
crank, (b) answers /status and looks healthy, and (c) actually pushes decodable CPS frames. Prints
an acceptance card and exits 0 only if the board is shippable.

The decision logic lives in sb20proxy.qa.acceptance (pure, unit-tested); this script is just the
BLE/HTTP/flash seam that gathers the observations. Real-data-first: sampled CPS frames are decoded
with the same sb20proxy.ble.cps codec the firmware mirrors.

    # validate against a board already running (zero flash risk):
    python code/scripts/qa_board.py --no-flash --connect
    python code/scripts/qa_board.py --no-flash --ip 192.168.1.50          # also pull /status

    # full pre-ship gate: flash, then accept:
    python code/scripts/qa_board.py --port COM10 --env esp32c3-oled-live --connect

Exit 0 = shippable; non-zero = do not ship (or flash failed). Never blocks: flashing is delegated to
flash_c3.py (time-bounded + retried).
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import subprocess
import sys
from pathlib import Path
from urllib.error import URLError
from urllib.request import urlopen

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "code" / "src"))
from sb20proxy.ble import cps  # noqa: E402
from sb20proxy.qa.acceptance import evaluate  # noqa: E402

CPS_MEASUREMENT_UUID = "00002a63-0000-1000-8000-00805f9b34fb"


def _flash(env: str, port: str, spoof_name: str) -> bool:
    """Delegate to flash_c3.py (the hang-resistant flasher) and require its BLE verify."""
    cmd = [sys.executable, str(REPO / "code" / "scripts" / "flash_c3.py"),
           "--env", env, "--port", port, "--verify-ble", spoof_name]
    print(f"$ {' '.join(cmd)}")
    return subprocess.run(cmd).returncode == 0


async def _scan(spoof_name: str, timeout: float) -> tuple[bool, list[str], str | None]:
    """Scan for the spoof advert. Returns (seen?, names_seen, address_of_match)."""
    from bleak import BleakScanner
    seen_names: list[str] = []
    match_addr: str | None = None
    devs = await BleakScanner.discover(timeout=timeout, return_adv=True)
    for addr, (d, adv) in devs.items():
        name = adv.local_name or d.name or ""
        if name:
            seen_names.append(name)
        if spoof_name.lower() in name.lower():
            match_addr = addr
    return (match_addr is not None), seen_names, match_addr


async def _sample_cps(address: str, n: int, timeout: float) -> list[int]:
    """Connect and collect up to n decoded instantaneous-power values from CPS notifications."""
    from bleak import BleakClient
    powers: list[int] = []
    done = asyncio.Event()

    def on_notify(_char, data: bytearray) -> None:
        try:
            powers.append(cps.decode_cps_measurement(bytes(data)).power_w)
        except Exception:  # noqa: BLE001 — a bad frame is a data point, not a crash
            powers.append(-1)  # forces the out-of-range fail in the verdict
        if len(powers) >= n:
            done.set()

    async with BleakClient(address) as client:
        await client.start_notify(CPS_MEASUREMENT_UUID, on_notify)
        try:
            await asyncio.wait_for(done.wait(), timeout=timeout)
        except asyncio.TimeoutError:
            pass
        await client.stop_notify(CPS_MEASUREMENT_UUID)
    return powers


def _fetch_status(ip: str, timeout: float) -> dict | None:
    try:
        with urlopen(f"http://{ip}/status", timeout=timeout) as resp:  # noqa: S310 — LAN device
            return json.loads(resp.read().decode("utf-8"))
    except (URLError, OSError, ValueError) as exc:
        print(f"  /status fetch failed: {exc}")
        return None


def main() -> int:
    p = argparse.ArgumentParser(description="Pre-ship acceptance test for a proxy board",
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--spoof-name", default="Stages 62144",
                   help="the crank name the board should advertise (default: Stages 62144)")
    p.add_argument("--port", help="COM port to flash (omit / use --no-flash to skip flashing)")
    p.add_argument("--env", default="esp32c3-oled-live", help="platformio env to flash")
    p.add_argument("--no-flash", action="store_true", help="don't flash; just accept a running board")
    p.add_argument("--connect", action="store_true",
                   help="connect over BLE and sample CPS frames (a stronger check)")
    p.add_argument("--ip", help="board IP, to also pull and check /status")
    p.add_argument("--scan-timeout", type=float, default=8.0)
    p.add_argument("--frames", type=int, default=3, help="CPS frames to sample with --connect")
    p.add_argument("--json", action="store_true", help="emit the verdict as JSON")
    a = p.parse_args()

    flash_ok: bool | None = None
    if not a.no_flash:
        if not a.port:
            p.error("give --port to flash, or pass --no-flash")
        flash_ok = _flash(a.env, a.port, a.spoof_name)
        if not flash_ok:
            print("flash failed — board is not shippable")

    advert_seen = advert_names = match_addr = None
    if flash_ok is not False:  # don't bother scanning if the flash itself failed
        print(f"scanning {a.scan_timeout:.0f}s for '{a.spoof_name}' ...")
        advert_seen, advert_names, match_addr = asyncio.run(
            _scan(a.spoof_name, a.scan_timeout))

    cps_powers = None
    if a.connect and match_addr:
        print(f"connecting {match_addr} to sample {a.frames} CPS frame(s) ...")
        try:
            cps_powers = asyncio.run(_sample_cps(match_addr, a.frames, timeout=15.0))
        except Exception as exc:  # noqa: BLE001
            print(f"  CPS sample failed: {exc}")
            cps_powers = []
    elif a.connect:
        cps_powers = []  # asked to connect but nothing to connect to

    status = _fetch_status(a.ip, timeout=5.0) if a.ip else None

    report = evaluate(
        expected_spoof_name=a.spoof_name, advert_seen=advert_seen,
        advert_names=advert_names, status=status, cps_powers=cps_powers, flash_ok=flash_ok,
    )

    if a.json:
        print(json.dumps({"passed": report.passed,
                          "checks": [{"name": c.name, "result": c.mark, "detail": c.detail}
                                     for c in report.checks]}, indent=2))
    else:
        print()
        print(report.render(title=f"Board acceptance ({a.spoof_name})"))
    return 0 if report.passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
