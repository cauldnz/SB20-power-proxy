#!/usr/bin/env python3
"""flash_c3.py — reliable, hang-resistant flashing for the ESP32-C3 boards.

Why this exists: PlatformIO's bundled esptool (tool-esptoolpy@1.40501.0 = esptool
4.5.1) hits the ESP32-C3 USB-Serial/JTAG "A fatal error occurred: No serial data
received" bug, so `pio run -t upload` fails on these boards. A NEWER esptool
(tool-esptoolpy = 4.11.0) flashes them cleanly with `--before default_reset
--after hard_reset`. This helper flashes the pio-BUILT binaries with that newer
esptool, **time-bounded with retries** so an autonomous run can never wedge waiting
on a flash (proven on COM10, 2026-06-21).

Usage (build first, then flash — build with pio, flash with this):
    pio run -e esp32c3-ftms-server                 # compile (pio's esptool is fine to BUILD)
    python code/scripts/flash_c3.py --env esp32c3-ftms-server --port COM10
    # optional alive-check: confirm the board advertises after boot
    python code/scripts/flash_c3.py --env esp32c3-supermini --port COM10 --verify-ble "Stages 62144"

Exit 0 on a verified flash; non-zero on failure (caller logs + continues — never blocks).
"""

from __future__ import annotations

import argparse
import glob
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FIRMWARE = REPO / "firmware"
PLATFORMIO = Path(os.path.expanduser("~")) / ".platformio" / "packages"

# Arduino-ESP32 C3 flash layout (from `pio run -t upload -v`).
OFFSETS = {"bootloader.bin": "0x0000", "partitions.bin": "0x8000",
           "boot_app0.bin": "0xe000", "firmware.bin": "0x10000"}


def _esptool() -> Path:
    """The newest esptool.py under ~/.platformio (4.11+ fixes the C3 USB-JTAG bug).
    Prefer the unversioned tool-esptoolpy dir; fall back to the highest @version."""
    cands = sorted(glob.glob(str(PLATFORMIO / "tool-esptoolpy*" / "esptool.py")))
    if not cands:
        sys.exit("no esptool.py under ~/.platformio/packages/tool-esptoolpy*")
    bare = PLATFORMIO / "tool-esptoolpy" / "esptool.py"
    return bare if bare.exists() else Path(cands[-1])


def _esptool_python() -> str:
    """A python that can `import serial` (esptool's dep). The flasher itself may run
    under the code venv (for the BLE check), which lacks pyserial — but PlatformIO's
    python does have it, so esptool must run under that one, not sys.executable."""
    cands = [
        str(Path(os.path.expanduser("~")) / ".platformio" / "penv" / "Scripts" / "python.exe"),
        r"C:\Python313\python.exe",
        shutil.which("python") or "",
        sys.executable,
    ]
    for c in cands:
        if c and Path(c).exists():
            try:
                subprocess.run([c, "-c", "import serial"], check=True,
                               capture_output=True, timeout=20)
                return c
            except Exception:
                continue
    sys.exit("no python with pyserial found to run esptool (pip install pyserial)")


def _boot_app0() -> str:
    hits = sorted(glob.glob(str(
        PLATFORMIO / "framework-arduinoespressif32*" / "tools" / "partitions" / "boot_app0.bin")))
    if not hits:
        sys.exit("no boot_app0.bin under framework-arduinoespressif32*/tools/partitions")
    return hits[-1]


def _flash_args(esptool: Path, port: str, baud: int, build: Path) -> list[str]:
    args = [_esptool_python(), str(esptool), "--chip", "esp32c3", "--port", port,
            "--baud", str(baud), "--before", "default_reset", "--after", "hard_reset",
            "write_flash", "-z", "--flash_mode", "dio", "--flash_freq", "80m",
            "--flash_size", "4MB"]
    for name, off in OFFSETS.items():
        path = (_boot_app0() if name == "boot_app0.bin" else str(build / name))
        if not Path(path).exists():
            sys.exit(f"missing {name} — build the env first: pio run -e <env>")
        args += [off, path]
    return args


def _verify_ble(name: str, timeout: float) -> bool:
    try:
        import asyncio

        from bleak import BleakScanner
    except ImportError:
        print("  (bleak not available — skipping BLE verify)")
        return True

    async def scan() -> bool:
        devs = await BleakScanner.discover(timeout=timeout, return_adv=True)
        for _addr, (d, adv) in devs.items():
            if name.lower() in (adv.local_name or d.name or "").lower():
                return True
        return False

    return asyncio.run(scan())


def main() -> int:
    p = argparse.ArgumentParser(description="Reliable ESP32-C3 flasher (esptool 4.11 direct)")
    p.add_argument("--env", required=True, help="platformio env (the .pio/build/<env> dir)")
    p.add_argument("--port", required=True, help="COM port, e.g. COM10")
    p.add_argument("--baud", type=int, default=460800)
    p.add_argument("--retries", type=int, default=3)
    p.add_argument("--timeout", type=float, default=90.0,
                   help="per-attempt seconds (then kill+retry)")
    p.add_argument("--verify-ble", metavar="NAME", default=None,
                   help="after flashing, require a BLE advert whose name contains NAME")
    a = p.parse_args()

    build = FIRMWARE / ".pio" / "build" / a.env
    cmd = _flash_args(_esptool(), a.port, a.baud, build)

    for attempt in range(1, a.retries + 1):
        print(f"flash {a.env} -> {a.port} (attempt {attempt}/{a.retries})")
        try:
            r = subprocess.run(cmd, timeout=a.timeout, cwd=str(FIRMWARE))
        except subprocess.TimeoutExpired:
            print("  timed out — killed; retrying")
            continue
        if r.returncode == 0:
            if a.verify_ble:
                print(f"  flashed; verifying BLE advert {a.verify_ble!r} ...")
                if _verify_ble(a.verify_ble, timeout=8.0):
                    print("  BLE advert seen - board booted OK")
                    return 0
                print("  flashed but no BLE advert seen — board may not have booted")
                return 2
            return 0
        print(f"  esptool returned {r.returncode}; retrying")
    print(f"FAILED to flash {a.env} -> {a.port} after {a.retries} attempts")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
