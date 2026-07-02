#!/usr/bin/env python3
r"""flash_s3.py — reliable flashing for the Waveshare ESP32-S3-Touch-LCD-1.47.

Why this exists (separate from flash_c3.py):
  * The S3 head-unit runs on the **pioarduino** platform (Arduino 3.x / IDF 5.5);
    the stock espressif32@6.7.0 (IDF 4.4) bootloader crash-loops on this module
    (decisions.md 2026-07-03).  BUILD from native PowerShell with the penv python:
      & "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m platformio run -e esp32s3-pio
  * The S3's native USB-Serial-JTAG **drops the flasher stub at 460800** (same class
    of bug as the C3), so `pio -t upload` (which uses 460800) hangs at connect.
  * pioarduino emits a single **merged image** (`firmware.factory.bin`, bootloader +
    partitions + boot_app0 + app at 0x0).  Flashing that one file at 0x0 at **115200**
    is the route proven to work (COM16, 2026-07-03).

This helper flashes that pre-built factory image, time-bounded with one retry so an
autonomous run can't wedge on a flash.  BUILD first with pio, then flash with this.

Usage:
    python code/scripts/flash_s3.py                       # env esp32s3-pio, port COM16
    python code/scripts/flash_s3.py --env esp32s3-pio-live --port COM16
    python code/scripts/flash_s3.py --verify-ble "Stages 62144"   # confirm advert after boot
"""

from __future__ import annotations

import argparse
import glob
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FIRMWARE = REPO / "firmware"
PLATFORMIO = Path(os.path.expanduser("~")) / ".platformio"


def _esptool_python() -> str:
    """Return a python whose `esptool` is >= 5.0 (or at least 4.11).

    IMPORTANT: PlatformIO's penv ships esptool **4.5.1**, which (a) hits the ESP32
    USB-JTAG "No serial data received" bug and (b) mis-writes the flash-size field of
    a merged image's bootloader header (an 8 MB header on this 16 MB chip -> the
    bootloader rejects the 16 MB partition table and boot-loops; 2026-07-03). So we
    must NOT use the penv python here — probe candidates and pick the newest esptool."""
    import shutil

    cands = [
        r"C:\Python313\python.exe",
        shutil.which("python") or "",
        sys.executable,
        str(PLATFORMIO / "penv" / "Scripts" / "python.exe"),  # last resort (esptool 4.5.1)
    ]
    best, best_ver = None, (0, 0, 0)
    for c in cands:
        if not c or not Path(c).exists():
            continue
        try:
            out = subprocess.run([c, "-c", "import esptool;print(getattr(esptool,'__version__','0'))"],
                                 capture_output=True, text=True, timeout=20)
            parts = out.stdout.strip().split(".")
            ver = tuple(int(x) for x in (parts + ["0", "0", "0"])[:3] if x.isdigit()) or (0,)
            ver = (ver + (0, 0, 0))[:3]
        except Exception:
            continue
        if ver >= (4, 11, 0) and ver > best_ver:
            best, best_ver = c, ver
    if best:
        return best
    # nothing new enough found — use the current interpreter and hope for the best
    return sys.executable


def flash(env: str, port: str, timeout: int) -> bool:
    factory = FIRMWARE / ".pio" / "build" / env / "firmware.factory.bin"
    if not factory.exists():
        print(f"[flash_s3] no factory image at {factory}\n"
              f"           build first:  pio run -e {env}   (from native PowerShell/penv)",
              file=sys.stderr)
        return False
    cmd = [_esptool_python(), "-m", "esptool", "--chip", "esp32s3", "--port", port,
           "--baud", "115200", "--before", "default_reset", "--after", "hard_reset",
           "write_flash", "--flash_size", "16MB", "0x0", str(factory)]
    for attempt in (1, 2):
        print(f"[flash_s3] attempt {attempt}: {factory.name} -> {port} @115200")
        try:
            r = subprocess.run(cmd, timeout=timeout)
            if r.returncode == 0:
                print("[flash_s3] OK — hash verified, board hard-reset")
                return True
        except subprocess.TimeoutExpired:
            print(f"[flash_s3] attempt {attempt} timed out after {timeout}s", file=sys.stderr)
    return False


def verify_ble(name: str, secs: int = 12) -> bool:
    try:
        import asyncio
        from bleak import BleakScanner
    except Exception as e:  # pragma: no cover - optional dep
        print(f"[flash_s3] --verify-ble needs bleak ({e}); skipping", file=sys.stderr)
        return True

    async def scan() -> bool:
        await asyncio.sleep(4)  # let the board finish booting + start advertising post-reset
        found = {"hit": False}

        def cb(_d, adv):
            if name in (adv.local_name or ""):
                found["hit"] = True
        sc = BleakScanner(detection_callback=cb)
        await sc.start(); await asyncio.sleep(secs); await sc.stop()
        return found["hit"]

    ok = asyncio.run(scan())
    print(f"[flash_s3] BLE advert '{name}': {'FOUND' if ok else 'NOT FOUND'}")
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--env", default="esp32s3-pio", help="pio env (default esp32s3-pio)")
    ap.add_argument("--port", default="COM16", help="serial port (default COM16)")
    ap.add_argument("--timeout", type=int, default=120, help="per-attempt seconds (default 120)")
    ap.add_argument("--verify-ble", metavar="NAME", default=None,
                    help="after flashing, scan for a BLE advert with this local name")
    a = ap.parse_args()

    if not flash(a.env, a.port, a.timeout):
        return 1
    if a.verify_ble and not verify_ble(a.verify_ble):
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
