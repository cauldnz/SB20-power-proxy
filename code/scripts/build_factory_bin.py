#!/usr/bin/env python3
"""build_factory_bin.py — merge a built firmware into a single factory image for browser flashing.

ESP Web Tools (the browser installer at firmware/webflash/) flashes ONE combined image at offset 0,
not the four separate parts `flash_c3.py` writes. This merges the pio-built bootloader + partition
table + boot_app0 + app into `firmware/webflash/firmware-factory.bin` and refreshes the manifest, so
a tester can install/recover from Chrome with no CLI.

    pio run -e esp32c3-oled-live-ota                       # build first (the shippable image)
    python code/scripts/build_factory_bin.py --env esp32c3-oled-live-ota

The flash recipe (chip, offsets, mode/freq/size) MIRRORS flash_c3.py — keep them in step. Uses the
newest esptool under ~/.platformio (4.11+, the one that handles the C3 cleanly).
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FIRMWARE = REPO / "firmware"
WEBFLASH = FIRMWARE / "webflash"
PLATFORMIO = Path(os.path.expanduser("~")) / ".platformio" / "packages"

# Same layout flash_c3.py writes (Arduino-ESP32 C3). boot_app0 comes from the framework, not the build.
OFFSETS = [("0x0", "bootloader.bin"), ("0x8000", "partitions.bin"),
           ("0xe000", "boot_app0.bin"), ("0x10000", "firmware.bin")]


def _esptool() -> Path:
    bare = PLATFORMIO / "tool-esptoolpy" / "esptool.py"
    if bare.exists():
        return bare
    cands = sorted(glob.glob(str(PLATFORMIO / "tool-esptoolpy*" / "esptool.py")))
    if not cands:
        sys.exit("no esptool.py under ~/.platformio/packages/tool-esptoolpy*")
    return Path(cands[-1])


def _esptool_python() -> str:
    """A python that can `import serial` (esptool's dep) — PlatformIO's penv has it."""
    import shutil
    cands = [str(PLATFORMIO.parent / "penv" / "Scripts" / "python.exe"),
             r"C:\Python313\python.exe", shutil.which("python") or "", sys.executable]
    for c in cands:
        if c and Path(c).exists():
            try:
                subprocess.run([c, "-c", "import serial"], check=True,
                               capture_output=True, timeout=20)
                return c
            except Exception:
                continue
    sys.exit("no python with pyserial found to run esptool")


def _boot_app0() -> str:
    hits = sorted(glob.glob(str(
        PLATFORMIO / "framework-arduinoespressif32*" / "tools" / "partitions" / "boot_app0.bin")))
    if not hits:
        sys.exit("no boot_app0.bin under framework-arduinoespressif32*/tools/partitions")
    return hits[-1]


def _part_path(build: Path, name: str) -> str:
    path = _boot_app0() if name == "boot_app0.bin" else str(build / name)
    if not Path(path).exists():
        sys.exit(f"missing {name} — build the env first: pio run -e <env>")
    return path


def main() -> int:
    p = argparse.ArgumentParser(description="Merge a built firmware into a factory image")
    p.add_argument("--env", default="esp32c3-oled-live-ota",
                   help="platformio env whose .pio/build/<env> to merge (default: the OLED live image)")
    p.add_argument("--out", default=str(WEBFLASH / "firmware-factory.bin"))
    p.add_argument("--name", default="SB20 Proxy (OLED, live)",
                   help="display name written into the manifest")
    p.add_argument("--version", default="dev", help="version string for the manifest")
    a = p.parse_args()

    build = FIRMWARE / ".pio" / "build" / a.env
    if not build.is_dir():
        sys.exit(f"no build at {build} — run: pio run -e {a.env}")
    WEBFLASH.mkdir(parents=True, exist_ok=True)

    cmd = [_esptool_python(), str(_esptool()), "--chip", "esp32c3", "merge_bin",
           "-o", a.out, "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "4MB"]
    for off, name in OFFSETS:
        cmd += [off, _part_path(build, name)]

    print(f"merging {a.env} -> {a.out}")
    r = subprocess.run(cmd, cwd=str(FIRMWARE))
    if r.returncode != 0:
        return r.returncode
    size = Path(a.out).stat().st_size
    print(f"  wrote {size} bytes ({size / 1024:.0f} KB)")

    # The ESP Web Tools manifest: one combined part at offset 0 (we already baked in the offsets).
    manifest = {
        "name": a.name,
        "version": a.version,
        "builds": [{
            "chipFamily": "ESP32-C3",
            "parts": [{"path": Path(a.out).name, "offset": 0}],
        }],
    }
    manifest_path = WEBFLASH / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"  manifest -> {manifest_path}")
    print("\nhost firmware/webflash/ over HTTPS (GitHub Pages) to flash from Chrome. See its README.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
