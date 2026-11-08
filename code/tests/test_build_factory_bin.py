"""build_factory_bin.py: the checks that keep a non-booting Guition image out of a release."""

from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest

_SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "build_factory_bin.py"
_spec = importlib.util.spec_from_file_location("build_factory_bin", _SCRIPT)
assert _spec and _spec.loader
bfb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bfb)


def _image(
    magic: int = 0xE9, size_code: int = 0x4, chip: int = 9, length: int = 2_000_000
) -> bytes:
    blob = bytearray(b"\xff" * length)
    blob[0] = magic
    blob[3] = (size_code << 4) | 0xF
    blob[12:14] = chip.to_bytes(2, "little")
    return bytes(blob)


def test_a_good_guition_image_passes() -> None:
    bfb.check_s3_factory_image(_image())


@pytest.mark.parametrize(
    ("overrides", "why"),
    [
        ({"length": 400_000}, "partial"),
        ({"magic": 0x00}, "no ESP image"),
        ({"chip": 5}, "chip id"),  # a C3 bootloader
        ({"size_code": 0x3}, "flash size"),  # the measured 8 MB header that boot-loops this board
    ],
    ids=["partial", "not-an-image", "wrong-chip", "wrong-flash-size"],
)
def test_a_bad_guition_image_is_refused(overrides: dict[str, int], why: str) -> None:
    with pytest.raises(ValueError, match=why):
        bfb.check_s3_factory_image(_image(**overrides))


@pytest.mark.parametrize(
    ("out", "manifest"),
    [
        ("firmware-factory.bin", "manifest.json"),
        ("firmware-factory-guition.bin", "manifest-guition.json"),
        ("/some/dir/firmware-factory-guition.bin", "manifest-guition.json"),
    ],
)
def test_each_image_gets_its_own_manifest(out: str, manifest: str) -> None:
    assert bfb.manifest_name_for(out) == manifest


def test_only_guition_envs_take_the_premerged_path() -> None:
    assert bfb.is_guition("esp32-guition-live-release")
    assert not bfb.is_guition("esp32c3-oled-live-release")
