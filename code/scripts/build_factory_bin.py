#!/usr/bin/env python3
"""build_factory_bin.py — merge a built firmware into a single factory image for browser flashing.

ESP Web Tools (the browser installer at firmware/webflash/) flashes ONE combined image at offset 0,
not the four separate parts `flash_c3.py` writes. This merges the pio-built bootloader + partition
table + boot_app0 + app into `firmware/webflash/firmware-factory.bin` and refreshes the manifest, so
a tester can install/recover from Chrome with no CLI.

    pio run -e esp32c3-oled-live-ota                       # build first (the shippable image)
    python code/scripts/build_factory_bin.py --env esp32c3-oled-live-ota

The flash recipe (chip, offsets, mode/freq/size) MIRRORS flash_c3.py — keep them in step.

The Guition is the exception (`--env esp32-guition-live-release`): the pioarduino platform already
merges a `firmware.factory.bin` for it, so that file is checked and copied instead of re-merged. It
goes out under its own names (`firmware-factory-guition.bin`, `manifest-guition.json`) so the two
boards cannot overwrite each other in a release. An S3 image merged by the wrong esptool has been
measured with the wrong flash size in its header, which boot-loops the board (flash_s3.py), hence
`check_s3_factory_image`.

Both factory images are 0xFF across the NVS window, so installing one WIPES WiFi, the meter and the
crank identity. That is right for a first install and a surprise for a reinstall; say so in any
instructions that send someone here.

esptool: `_esptool()` prefers the BARE `tool-esptoolpy` directory, so in practice it runs whatever
PlatformIO installed there (4.5.1 on this machine as of 2026-11-08), not "the newest". That is fine
here and NOT the 4.11+ rule flash_c3.py needs: `merge_bin` only reads and concatenates files, so the
C3 USB-JTAG bug that rule exists for cannot apply — no serial port is opened. What this script IS
sensitive to is the CLI dialect: it emits the 4.x underscore spellings (`merge_bin`, `--flash_mode`),
which esptool 5.x renamed to hyphens (see flash_c3.py's `_cli_dialect`). If a future platform ships a
5.x under that directory, this breaks and needs the same dialect switch. The release workflow guards
the symptom by asserting the merged image is at least 1 MB.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import shutil
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


# ESP image header: magic at 0, flash mode at 2, size/frequency at 3, chip id at 12..13.
_ESP_IMAGE_MAGIC = 0xE9
_ESP32S3_CHIP_ID = 9
_FLASH_SIZE_16MB = 0x4


def is_guition(env: str) -> bool:
    return env.startswith("esp32-guition")


def check_s3_factory_image(blob: bytes) -> None:
    """Refuse a merged Guition image that would not boot, rather than publish it."""
    if len(blob) < 1_000_000:
        raise ValueError(f"factory image is only {len(blob)} bytes; the merge looks partial")
    if blob[0] != _ESP_IMAGE_MAGIC:
        raise ValueError(f"no ESP image at offset 0 (first byte {blob[0]:#04x})")
    chip = int.from_bytes(blob[12:14], "little")
    if chip != _ESP32S3_CHIP_ID:
        raise ValueError(f"bootloader is for chip id {chip}, not the ESP32-S3")
    size = blob[3] >> 4
    if size != _FLASH_SIZE_16MB:
        raise ValueError(f"bootloader header says flash size code {size:#x}, not 16 MB")


def manifest_name_for(out: str) -> str:
    """manifest.json for firmware-factory.bin; manifest-guition.json for the -guition image."""
    stem = Path(out).stem
    suffix = stem[len("firmware-factory"):] if stem.startswith("firmware-factory") else ""
    return f"manifest{suffix}.json"


def write_manifest(name: str, version: str, chip_family: str, out: str) -> Path:
    # The ESP Web Tools manifest: one combined part at offset 0 (we already baked in the offsets).
    manifest = {
        "name": name,
        "version": version,
        "builds": [{
            "chipFamily": chip_family,
            "parts": [{"path": Path(out).name, "offset": 0}],
        }],
    }
    path = WEBFLASH / manifest_name_for(out)
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return path


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
    p.add_argument("--out", default=None,
                   help="output image (default: firmware-factory[-guition].bin under webflash/)")
    p.add_argument("--name", default="SB20 Proxy (OLED, live)",
                   help="display name written into the manifest")
    p.add_argument("--version", default="dev", help="version string for the manifest")
    a = p.parse_args()

    build = FIRMWARE / ".pio" / "build" / a.env
    if not build.is_dir():
        sys.exit(f"no build at {build} — run: pio run -e {a.env}")
    WEBFLASH.mkdir(parents=True, exist_ok=True)
    if a.out is None:
        leaf = "firmware-factory-guition.bin" if is_guition(a.env) else "firmware-factory.bin"
        a.out = str(WEBFLASH / leaf)

    if is_guition(a.env):
        merged = build / "firmware.factory.bin"
        if not merged.exists():
            sys.exit(f"missing {merged} - build the env first: pio run -e {a.env}")
        check_s3_factory_image(merged.read_bytes())
        shutil.copyfile(merged, a.out)
        print(f"  checked and copied {merged.name} -> {a.out} ({Path(a.out).stat().st_size} bytes)")
        print(f"  manifest -> {write_manifest(a.name, a.version, 'ESP32-S3', a.out)}")
        return 0

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

    print(f"  manifest -> {write_manifest(a.name, a.version, 'ESP32-C3', a.out)}")
    print("\nhost firmware/webflash/ over HTTPS (GitHub Pages) to flash from Chrome. See its README.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
