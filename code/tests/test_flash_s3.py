"""Unit tests for the pure helpers in scripts/flash_s3.py (hardware-free).

Guards the 2026-09-23 defect: flash_s3.py wrote pioarduino's merged `firmware.factory.bin`
at 0x0, and that image is padded with 0xFF across the NVS window — so every routine USB
reflash silently BLANKED provisioning (WiFi credentials, meter/crank identity, calibration)
and the board came back up in its setup portal. The fix writes the code regions individually
and skips every data partition; the invariant under test is that no planned region can ever
overlap NVS. Only the table parsing / region planning / argv assembly is exercised here; the
on-device flash is verified manually.
"""

from __future__ import annotations

import importlib.util
import struct
from pathlib import Path

_FLASH_S3 = Path(__file__).resolve().parents[1] / "scripts" / "flash_s3.py"


def _load():
    spec = importlib.util.spec_from_file_location("flash_s3", _FLASH_S3)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


flash_s3 = _load()


def _entry(ptype: int, subtype: int, offset: int, size: int, label: str) -> bytes:
    """One 32-byte partition-table entry: magic|type|subtype|offset|size|label[16]|flags."""
    return (
        b"\xaa\x50"
        + bytes([ptype, subtype])
        + struct.pack("<II", offset, size)
        + label.encode("ascii").ljust(16, b"\x00")
        + struct.pack("<I", 0)
    )


# The layout this project actually ships on the 16 MB S3 boards (default_16MB.csv).
_REAL_TABLE = (
    _entry(1, 2, 0x9000, 0x5000, "nvs")
    + _entry(1, 0, 0xE000, 0x2000, "otadata")
    + _entry(0, 16, 0x10000, 0x640000, "app0")
    + _entry(0, 17, 0x650000, 0x640000, "app1")
    + _entry(1, 130, 0xC90000, 0x360000, "spiffs")
    + _entry(1, 3, 0xFF0000, 0x10000, "coredump")
)


def test_parse_partitions_decodes_the_shipped_layout():
    parts = flash_s3.parse_partitions(_REAL_TABLE)
    assert [p["name"] for p in parts] == ["nvs", "otadata", "app0", "app1", "spiffs", "coredump"]
    nvs = parts[0]
    assert (nvs["type"], nvs["subtype"], nvs["offset"], nvs["size"]) == (1, 2, 0x9000, 0x5000)
    assert parts[2]["offset"] == 0x10000


def test_parse_partitions_stops_at_the_md5_entry_and_padding():
    # A real table is followed by an md5 checksum entry (magic 0xEBEB) then 0xFF padding;
    # neither carries the 0xAA50 magic, so the table must end at the last real entry.
    blob = _REAL_TABLE + b"\xeb\xeb" + b"\xff" * 30 + b"\xff" * 0x1000
    assert len(flash_s3.parse_partitions(blob)) == 6


def test_parse_partitions_tolerates_a_truncated_trailing_entry():
    assert len(flash_s3.parse_partitions(_REAL_TABLE + b"\xaa\x50\x01")) == 6


def test_first_app_offset_is_the_lowest_app_partition():
    # app1 sits lower in the table only in contrived layouts; the bootloader still runs the
    # lowest app offset, so pick by offset, never by table order.
    reordered = (
        _entry(0, 17, 0x650000, 0x640000, "app1")
        + _entry(0, 16, 0x10000, 0x640000, "app0")
    )
    assert flash_s3.first_app_offset(flash_s3.parse_partitions(reordered)) == 0x10000


def test_first_app_offset_is_none_without_an_app_partition():
    data_only = flash_s3.parse_partitions(_entry(1, 2, 0x9000, 0x5000, "nvs"))
    assert flash_s3.first_app_offset(data_only) is None


def test_otadata_offset_finds_the_data_otadata_partition():
    parts = flash_s3.parse_partitions(_REAL_TABLE)
    assert flash_s3.otadata_offset(parts) == 0xE000
    # nvs is also type=data — it must not be mistaken for otadata (subtype differs).
    only_nvs = flash_s3.parse_partitions(_entry(1, 2, 0x9000, 0x5000, "nvs"))
    assert flash_s3.otadata_offset(only_nvs) is None


def _fake_build(tmp_path: Path) -> Path:
    build = tmp_path / "build"
    build.mkdir()
    (build / "bootloader.bin").write_bytes(b"\x00" * 19984)
    (build / "partitions.bin").write_bytes(_REAL_TABLE)
    (build / "firmware.bin").write_bytes(b"\x00" * 1884928)
    return build


def test_region_plan_never_overlaps_nvs(tmp_path):
    """The load-bearing invariant: provisioning survives a flash."""
    build = _fake_build(tmp_path)
    boot_app0 = tmp_path / "boot_app0.bin"
    boot_app0.write_bytes(b"\x00" * 8192)
    parts = flash_s3.parse_partitions(_REAL_TABLE)
    regions = flash_s3.region_plan(build, parts, boot_app0)

    nvs = next(p for p in parts if p["name"] == "nvs")
    lo, hi = nvs["offset"], nvs["offset"] + nvs["size"]
    for off, f in regions:
        end = off + f.stat().st_size
        assert off >= hi or end <= lo, f"{f.name} at {hex(off)}..{hex(end)} covers nvs"


def test_region_plan_writes_otadata_so_a_post_ota_board_boots_the_new_app(tmp_path):
    # After an OTA the device runs from app1. Writing only app0 would leave it booting the
    # stale image, so boot_app0 -> otadata is part of the plan, not an optional extra.
    build = _fake_build(tmp_path)
    boot_app0 = tmp_path / "boot_app0.bin"
    boot_app0.write_bytes(b"\x00" * 8192)
    regions = flash_s3.region_plan(build, flash_s3.parse_partitions(_REAL_TABLE), boot_app0)
    assert (0xE000, boot_app0) in regions
    assert regions[-1][0] == 0x10000 and regions[-1][1].name == "firmware.bin"


def test_region_plan_omits_otadata_when_boot_app0_is_missing(tmp_path):
    build = _fake_build(tmp_path)
    regions = flash_s3.region_plan(build, flash_s3.parse_partitions(_REAL_TABLE), None)
    assert [hex(o) for o, _ in regions] == ["0x0", "0x8000", "0x10000"]


def test_region_plan_rejects_a_table_with_no_app_partition(tmp_path):
    build = _fake_build(tmp_path)
    parts = flash_s3.parse_partitions(_entry(1, 2, 0x9000, 0x5000, "nvs"))
    try:
        flash_s3.region_plan(build, parts, None)
    except ValueError:
        return
    raise AssertionError("region_plan must refuse a table with nowhere to put the app")


def test_build_argv_emits_hex_offset_file_pairs_in_order(tmp_path):
    a, b = tmp_path / "a.bin", tmp_path / "b.bin"
    a.write_bytes(b"a")
    b.write_bytes(b"b")
    argv = flash_s3.build_argv("py", "COM14", [(0x0, a), (0x10000, b)])
    assert argv[:2] == ["py", "-m"]
    assert argv[-4:] == ["0x0", str(a), "0x10000", str(b)]
    assert "write_flash" in argv and "--chip" in argv and "esp32s3" in argv


def test_plan_flash_erase_nvs_writes_the_whole_image_at_zero(tmp_path, monkeypatch):
    # --erase-nvs is the documented way to get a clean slate, and its single 0x0 region DOES
    # cover NVS — that difference is the whole point of the flag.
    build = tmp_path / ".pio" / "build" / "esp32-guition-live"
    build.mkdir(parents=True)
    factory = build / "firmware.factory.bin"
    factory.write_bytes(b"\xff" * 0x10000 + b"\x00" * 16)
    monkeypatch.setattr(flash_s3, "FIRMWARE", tmp_path)
    regions, what = flash_s3.plan_flash("esp32-guition-live", erase_nvs=True)
    assert regions == [(0x0, factory)]
    assert "NVS WILL be erased" in what
    covers_nvs = regions[0][0] < 0xE000 and regions[0][0] + factory.stat().st_size > 0x9000
    assert covers_nvs


def test_plan_flash_keep_nvs_reports_the_data_partitions_it_preserves(tmp_path, monkeypatch):
    build = tmp_path / ".pio" / "build" / "esp32-guition-live"
    build.mkdir(parents=True)
    (build / "bootloader.bin").write_bytes(b"\x00" * 19984)
    (build / "partitions.bin").write_bytes(_REAL_TABLE)
    (build / "firmware.bin").write_bytes(b"\x00" * 1024)
    monkeypatch.setattr(flash_s3, "FIRMWARE", tmp_path)
    regions, what = flash_s3.plan_flash("esp32-guition-live", erase_nvs=False)
    assert "nvs" in what and "spiffs" in what
    assert all(off != 0x9000 for off, _ in regions)


def test_plan_flash_returns_none_when_the_build_is_missing(tmp_path, monkeypatch):
    monkeypatch.setattr(flash_s3, "FIRMWARE", tmp_path)
    assert flash_s3.plan_flash("nope", erase_nvs=False) is None
    assert flash_s3.plan_flash("nope", erase_nvs=True) is None
