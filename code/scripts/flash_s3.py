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

  * That merged image is padded with 0xFF across the **NVS** window, so writing it at
    0x0 BLANKS NVS -- WiFi credentials, meter/crank identity and calibration -- on every
    flash (measured 2026-09-23: factory.bin is exactly firmware.bin + a 64 KB header, and
    bytes 0x9000..0xE000 are all 0xFF while the partition table puts nvs at 0x9000+0x5000).
    The Guition came back up in the setup portal after a routine reflash because of this.
    So the DEFAULT here writes the four regions individually and SKIPS the NVS window;
    --erase-nvs restores the old whole-image behaviour when you want a clean slate.

This helper flashes that pre-built image, time-bounded with one retry so an autonomous run
can't wedge on a flash.  BUILD first with pio, then flash with this.

Usage:
    python code/scripts/flash_s3.py                       # env esp32s3-pio, port COM16
    python code/scripts/flash_s3.py --env esp32s3-pio-live --port COM16
    python code/scripts/flash_s3.py --erase-nvs           # wipe provisioning too (clean slate)
    python code/scripts/flash_s3.py --verify-ble "Stages 62144"   # confirm advert after boot
"""

from __future__ import annotations

import argparse
import glob
import os
import struct
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


# -- partition table -> "which regions do we write" ------------------------------------------
# An ESP-IDF partition table is a flat array of 32-byte entries, each starting with the magic
# 0xAA 0x50: <magic:2><type:1><subtype:1><offset:u32><size:u32><label:16>. Parsing it (rather
# than hardcoding 0x10000 etc.) keeps this correct if the partition scheme changes.
PART_ENTRY_LEN = 32
PART_MAGIC = b"\xaa\x50"
PART_TYPE_APP, PART_TYPE_DATA = 0x00, 0x01
PART_SUBTYPE_OTADATA = 0x00
PARTITIONS_OFFSET = 0x8000


def parse_partitions(blob: bytes) -> list[dict]:
    """Decode a partition-table image into [{name, type, subtype, offset, size}, ...]."""
    out: list[dict] = []
    for base in range(0, len(blob) - PART_ENTRY_LEN + 1, PART_ENTRY_LEN):
        entry = blob[base:base + PART_ENTRY_LEN]
        if entry[:2] != PART_MAGIC:
            break  # md5 checksum entry or 0xFF padding: the table ends here
        ptype, subtype = entry[2], entry[3]
        offset, size = struct.unpack("<II", entry[4:12])
        label = entry[12:28].split(b"\x00")[0].decode("ascii", "replace")
        out.append({"name": label, "type": ptype, "subtype": subtype,
                    "offset": offset, "size": size})
    return out


def first_app_offset(parts: list[dict]) -> int | None:
    """Offset of the partition the bootloader will run (the lowest app partition)."""
    apps = [p["offset"] for p in parts if p["type"] == PART_TYPE_APP]
    return min(apps) if apps else None


def otadata_offset(parts: list[dict]) -> int | None:
    for p in parts:
        if p["type"] == PART_TYPE_DATA and p["subtype"] == PART_SUBTYPE_OTADATA:
            return p["offset"]
    return None


def region_plan(build: Path, parts: list[dict], boot_app0: Path | None) -> list[tuple[int, Path]]:
    """The (offset, file) pairs that reflash the code and leave every DATA partition alone.

    boot_app0 -> otadata is not optional: after an OTA the device boots from app1, and writing
    only app0 would leave it running the stale image. Writing otadata pins it back to app0.
    """
    app = first_app_offset(parts)
    if app is None:
        raise ValueError("no app partition in the table")
    plan: list[tuple[int, Path]] = []
    for off, name in ((0x0, "bootloader.bin"), (PARTITIONS_OFFSET, "partitions.bin")):
        f = build / name
        if f.exists():
            plan.append((off, f))
    ota = otadata_offset(parts)
    if ota is not None and boot_app0 and boot_app0.exists():
        plan.append((ota, boot_app0))
    plan.append((app, build / "firmware.bin"))
    return plan


def find_boot_app0() -> Path | None:
    """boot_app0.bin ships with the Arduino framework package (glob: pioarduino renames it)."""
    pat = "framework-arduinoespressif32*/tools/partitions/boot_app0.bin"
    hits = sorted(glob.glob(str(PLATFORMIO / "packages" / pat)))
    return Path(hits[-1]) if hits else None


def build_argv(python: str, port: str, regions: list[tuple[int, Path]]) -> list[str]:
    """esptool argv for one write_flash covering every (offset, file) region."""
    argv = [python, "-m", "esptool", "--chip", "esp32s3", "--port", port,
            "--baud", "115200", "--before", "default_reset", "--after", "hard_reset",
            "write_flash", "--flash_size", "16MB"]
    for off, f in regions:
        argv += [hex(off), str(f)]
    return argv


def plan_flash(env: str, erase_nvs: bool) -> tuple[list[tuple[int, Path]], str] | None:
    """Resolve what to write: the whole merged image, or the code regions only (NVS kept).

    Returns (regions, human_description), or None if the build outputs are missing.
    """
    build = FIRMWARE / ".pio" / "build" / env
    if erase_nvs:
        factory = build / "firmware.factory.bin"
        if not factory.exists():
            print(f"[flash_s3] no factory image at {factory}\n"
                  f"           build first:  pio run -e {env}   (from native PowerShell/penv)",
                  file=sys.stderr)
            return None
        return [(0x0, factory)], f"{factory.name} (whole image - NVS WILL be erased)"

    app_bin, table = build / "firmware.bin", build / "partitions.bin"
    for f in (app_bin, table):
        if not f.exists():
            print(f"[flash_s3] missing {f}\n"
                  f"           build first:  pio run -e {env}   (from native PowerShell/penv)",
                  file=sys.stderr)
            return None
    parts = parse_partitions(table.read_bytes())
    try:
        regions = region_plan(build, parts, find_boot_app0())
    except ValueError as e:
        print(f"[flash_s3] cannot plan a region flash ({e}); retry with --erase-nvs",
              file=sys.stderr)
        return None
    kept = ", ".join(q["name"] for q in parts
                     if q["type"] == PART_TYPE_DATA and q["subtype"] != PART_SUBTYPE_OTADATA)
    return regions, f"{len(regions)} regions, keeping {kept or 'no data partitions'}"


def flash(env: str, port: str, timeout: int, erase_nvs: bool = False) -> bool:
    planned = plan_flash(env, erase_nvs)
    if planned is None:
        return False
    regions, what = planned
    cmd = build_argv(_esptool_python(), port, regions)
    for attempt in (1, 2):
        print(f"[flash_s3] attempt {attempt}: {what} -> {port} @115200")
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
    ap.add_argument("--erase-nvs", action="store_true",
                    help="write the merged factory image instead, wiping NVS "
                         "(WiFi creds, meter/crank identity, calibration) for a clean slate")
    ap.add_argument("--verify-ble", metavar="NAME", default=None,
                    help="after flashing, scan for a BLE advert with this local name")
    a = ap.parse_args()

    if not flash(a.env, a.port, a.timeout, a.erase_nvs):
        return 1
    if a.verify_ble and not verify_ble(a.verify_ble):
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
