#!/usr/bin/env python3
"""flash_c3.py — reliable, hang-resistant flashing for the ESP32-C3 boards.

Why this exists: PlatformIO's bundled esptool (tool-esptoolpy@1.40501.0 = esptool
4.5.1) hits the ESP32-C3 USB-Serial/JTAG "A fatal error occurred: No serial data
received" bug, so `pio run -t upload` fails on these boards. A NEWER esptool
(>= 4.11, including the 5.x PlatformIO now stages in a `tool-esptoolpy@…` dir
beside the buggy one) flashes them cleanly. This helper **auto-picks the newest
esptool it can find** — the highest-versioned `tool-esptoolpy*` under
~/.platformio, else the system `python -m esptool` — and speaks the matching CLI
dialect (esptool 5.x renamed the commands/flags to hyphens: `write-flash`,
`--flash-mode`, `--before default-reset`, …). It flashes the pio-BUILT binaries
**time-bounded with retries** so an autonomous run can never wedge waiting on a
flash (proven on COM10 w/ 4.11, 2026-06-21; COM9/COM10 w/ esptool 5.3.x, 2026-07-05).

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
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FIRMWARE = REPO / "firmware"
PLATFORMIO = Path(os.path.expanduser("~")) / ".platformio" / "packages"

# First esptool release without the ESP32-C3 USB-JTAG "No serial data received"
# wedge. Anything older (the bundled 4.5.1) must NOT be selected.
MIN_ESPTOOL = (4, 11)

# Arduino-ESP32 C3 flash layout (from `pio run -t upload -v`).
OFFSETS = {"bootloader.bin": "0x0000", "partitions.bin": "0x8000",
           "boot_app0.bin": "0xe000", "firmware.bin": "0x10000"}


def _parse_version(init_py: Path) -> tuple[int, ...] | None:
    """(major, minor, …) from an esptool package's __init__.py, or None. The
    `esptool.py` wrapper imports the `esptool/` package sitting beside it, so this
    version is exactly what that wrapper would run."""
    try:
        text = init_py.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return None
    m = re.search(r"""__version__\s*=\s*['"]([0-9]+(?:\.[0-9]+)+)""", text)
    return tuple(int(n) for n in m.group(1).split(".")) if m else None


def _newest_usable(versions: dict[str, tuple[int, ...] | None]) -> str | None:
    """The key (an esptool.py path) with the HIGHEST version >= MIN_ESPTOOL, or
    None if every candidate is too old. Picking by version — not by whether the
    dir is unversioned — is the whole fix: the bare tool-esptoolpy dir is the
    buggy 4.5.1, while a 5.x sits in a `@src-…` sibling."""
    best_key: str | None = None
    best_ver: tuple[int, ...] | None = None
    for key, ver in versions.items():
        if ver and ver >= MIN_ESPTOOL and (best_ver is None or ver > best_ver):
            best_key, best_ver = key, ver
    return best_key


def _system_esptool() -> tuple[list[str], tuple[int, ...]] | None:
    """A system `python -m esptool` that is >= MIN_ESPTOOL, or None. Fallback for
    when nothing new enough is staged under ~/.platformio."""
    seen: set[str] = set()
    for c in (r"C:\Python313\python.exe", shutil.which("python") or "",
              shutil.which("python3") or "", sys.executable):
        if not c or c in seen or not Path(c).exists():
            continue
        seen.add(c)
        try:
            r = subprocess.run([c, "-m", "esptool", "version"],
                               capture_output=True, text=True, timeout=30)
        except Exception:
            continue
        if r.returncode != 0:
            continue
        m = re.search(r"([0-9]+(?:\.[0-9]+)+)", (r.stdout or "") + (r.stderr or ""))
        ver = tuple(int(n) for n in m.group(1).split(".")) if m else None
        if ver and ver >= MIN_ESPTOOL:
            return [c, "-m", "esptool"], ver
    return None


def _esptool() -> tuple[list[str], tuple[int, ...]]:
    """(argv-prefix, version) for the newest usable esptool. The prefix is the
    command up to (but excluding) the write-flash subcommand — either
    `[python, /path/to/esptool.py]` for a ~/.platformio copy, or
    `[python, -m, esptool]` for the system fallback."""
    scripts = glob.glob(str(PLATFORMIO / "tool-esptoolpy*" / "esptool.py"))
    versions = {s: _parse_version(Path(s).parent / "esptool" / "__init__.py")
                for s in scripts}
    best = _newest_usable(versions)
    if best:
        return [_esptool_python(), best], versions[best]
    sysfb = _system_esptool()
    if sysfb:
        return sysfb
    have = ", ".join(f"{s}={'.'.join(map(str, v)) if v else '?'}"
                     for s, v in versions.items()) or "none found"
    sys.exit(f"no esptool >= {'.'.join(map(str, MIN_ESPTOOL))} available "
             f"(the bundled 4.5.1 wedges the ESP32-C3 USB-JTAG port). Found: {have}. "
             "Install a newer one: `pip install -U esptool` or update PlatformIO's "
             "tool-esptoolpy.")


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


def _cli_dialect(version: tuple[int, ...]) -> dict[str, str]:
    """esptool subcommand + flag spellings. esptool 5.x renamed everything to
    hyphens (`write-flash`, `--flash-mode`, `default-reset`, …); 4.x uses
    underscores. Keyed off the major version."""
    if version and version[0] >= 5:
        return {"sub": "write-flash", "mode": "--flash-mode", "freq": "--flash-freq",
                "size": "--flash-size", "before": "default-reset", "after": "hard-reset"}
    return {"sub": "write_flash", "mode": "--flash_mode", "freq": "--flash_freq",
            "size": "--flash_size", "before": "default_reset", "after": "hard_reset"}


def _flash_args(prefix: list[str], version: tuple[int, ...], port: str, baud: int,
                build: Path) -> list[str]:
    d = _cli_dialect(version)
    args = [*prefix, "--chip", "esp32c3", "--port", port, "--baud", str(baud),
            "--before", d["before"], "--after", d["after"],
            d["sub"], "-z", d["mode"], "dio", d["freq"], "80m", d["size"], "4MB"]
    for name, off in OFFSETS.items():
        path = (_boot_app0() if name == "boot_app0.bin" else str(build / name))
        if not Path(path).exists():
            sys.exit(f"missing {name} — build the env first: pio run -e <env>")
        args += [off, path]
    return args


def _child_env() -> dict[str, str]:
    """Environment for the esptool child, forced to UTF-8. esptool 5.x renders a
    Unicode block-glyph progress bar; on a legacy-codepage (cp1252) Windows
    console/pipe that raises UnicodeEncodeError mid-flash and kills the write.
    Forcing UTF-8 lets its own progress output encode cleanly (4.x's ASCII
    progress was unaffected, so this is harmless there)."""
    return {**os.environ, "PYTHONUTF8": "1", "PYTHONIOENCODING": "utf-8"}


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
    p = argparse.ArgumentParser(
        description="Reliable ESP32-C3 flasher (auto-selects the newest esptool >= 4.11)")
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
    prefix, version = _esptool()
    print(f"esptool {'.'.join(map(str, version))} via {' '.join(prefix[1:]) or prefix[0]}")
    cmd = _flash_args(prefix, version, a.port, a.baud, build)

    for attempt in range(1, a.retries + 1):
        print(f"flash {a.env} -> {a.port} (attempt {attempt}/{a.retries})")
        try:
            r = subprocess.run(cmd, timeout=a.timeout, cwd=str(FIRMWARE),
                               env=_child_env())
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
