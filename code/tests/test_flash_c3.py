"""Unit tests for the pure helpers in scripts/flash_c3.py (hardware-free).

Guards the 2026-07-05 regression: the flasher preferred the *unversioned*
tool-esptoolpy dir — now the bundled esptool 4.5.1, which wedges the ESP32-C3
USB-JTAG port ("No serial data received") — over a newer 5.x staged in a
`tool-esptoolpy@…` sibling. The fix selects by *version* and, when it lands on a
5.x, emits that release's hyphenated CLI (`write-flash`, `--flash-mode`,
`default-reset`, …). Only the version-parse / newest-select / CLI-dialect /
argv-assembly logic is exercised here; the on-device flash is verified manually.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path

_FLASH_C3 = Path(__file__).resolve().parents[1] / "scripts" / "flash_c3.py"


def _load():
    spec = importlib.util.spec_from_file_location("flash_c3", _FLASH_C3)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


flash_c3 = _load()


def test_parse_version_reads_dotted_version(tmp_path):
    init = tmp_path / "__init__.py"
    init.write_text('__version__ = "5.3.0"\n', encoding="utf-8")
    assert flash_c3._parse_version(init) == (5, 3, 0)


def test_parse_version_single_quotes_and_trailing_text(tmp_path):
    init = tmp_path / "__init__.py"
    init.write_text("__version__ = '4.11.0'  # noqa\n", encoding="utf-8")
    assert flash_c3._parse_version(init) == (4, 11, 0)


def test_parse_version_missing_file_is_none():
    assert flash_c3._parse_version(Path("no/such/__init__.py")) is None


def test_parse_version_no_marker_is_none(tmp_path):
    init = tmp_path / "__init__.py"
    init.write_text("# nothing to see here\n", encoding="utf-8")
    assert flash_c3._parse_version(init) is None


def test_newest_usable_prefers_5x_over_bundled_451():
    # The exact regression: the bundled 4.5.1 must lose to a newer 5.x.
    assert flash_c3._newest_usable({"bare": (4, 5, 1), "src": (5, 3, 0)}) == "src"


def test_newest_usable_lone_451_is_none():
    # 4.5.1 is below MIN_ESPTOOL -> nothing usable -> caller uses the system fallback.
    assert flash_c3._newest_usable({"bare": (4, 5, 1)}) is None


def test_newest_usable_min_boundary_is_included():
    assert flash_c3._newest_usable({"x": flash_c3.MIN_ESPTOOL}) == "x"


def test_newest_usable_skips_unparseable_and_picks_highest():
    assert flash_c3._newest_usable({"x": None, "y": (5, 1, 0)}) == "y"
    assert flash_c3._newest_usable({"x": None}) is None
    assert flash_c3._newest_usable({"a": (4, 11, 0), "b": (5, 3, 0), "c": (5, 1, 2)}) == "b"


def test_cli_dialect_5x_is_hyphenated():
    d = flash_c3._cli_dialect((5, 3, 0))
    assert d == {"sub": "write-flash", "mode": "--flash-mode", "freq": "--flash-freq",
                 "size": "--flash-size", "before": "default-reset", "after": "hard-reset"}


def test_cli_dialect_4x_is_underscored():
    d = flash_c3._cli_dialect((4, 11, 0))
    assert d == {"sub": "write_flash", "mode": "--flash_mode", "freq": "--flash_freq",
                 "size": "--flash_size", "before": "default_reset", "after": "hard_reset"}


def test_flash_args_uses_the_matching_dialect(tmp_path, monkeypatch):
    build = tmp_path / "build"
    build.mkdir()
    for name in ("bootloader.bin", "partitions.bin", "firmware.bin", "boot_app0.bin"):
        (build / name).write_bytes(b"\x00")
    monkeypatch.setattr(flash_c3, "_boot_app0", lambda: str(build / "boot_app0.bin"))

    argv5 = flash_c3._flash_args(["py", "esptool.py"], (5, 3, 0), "COM9", 460800, build)
    assert {"write-flash", "--flash-mode", "default-reset", "hard-reset"} <= set(argv5)
    assert "write_flash" not in argv5
    # global reset opts precede the write subcommand; firmware.bin is the last blob.
    assert argv5.index("--before") < argv5.index("write-flash")
    assert argv5[-1].endswith("firmware.bin") and argv5[-2] == "0x10000"

    argv4 = flash_c3._flash_args(["py", "esptool.py"], (4, 11, 0), "COM9", 460800, build)
    assert {"write_flash", "--flash_mode", "default_reset", "hard_reset"} <= set(argv4)
    assert "write-flash" not in argv4
