"""Tests for the TOML proxy config + the sb20proxy CLI validate path."""

from __future__ import annotations

from pathlib import Path

import pytest

from sb20proxy import cli
from sb20proxy.config import ProxyConfig, load_config
from sb20proxy.transform import GridTransform, IdentityTransform, ScaleOffsetTransform

EXAMPLE_TOML = Path(__file__).resolve().parents[1] / "sb20proxy.example.toml"

_GOOD = """
[proxy]
meter_id = 17039
spoof_id = 62144
source_label = "assioma"

[radio]
source_usb_index = 0
target_usb_index = 1
commons_every = 60

[correction]
scale = 0.92
offset = 0.0
"""


def _write(tmp_path, text) -> Path:
    p = tmp_path / "c.toml"
    p.write_text(text)
    return p


def test_load_maps_fields(tmp_path):
    cfg = load_config(_write(tmp_path, _GOOD))
    assert cfg.meter_id == 17039
    assert cfg.spoof_id == 62144
    assert cfg.source_label == "assioma"
    assert cfg.source_usb_index == 0 and cfg.target_usb_index == 1
    assert cfg.commons_every == 60
    assert cfg.scale == 0.92


def test_defaults_when_minimal(tmp_path):
    cfg = load_config(_write(tmp_path, "[proxy]\nmeter_id = 12345\n"))
    assert cfg.spoof_id == 62144           # default = the real Stages crank
    assert cfg.source_usb_index == 0 and cfg.target_usb_index == 1
    assert cfg.commons_every == 120
    assert cfg.validate() == []


def test_missing_meter_id_raises(tmp_path):
    with pytest.raises(ValueError):
        load_config(_write(tmp_path, "[proxy]\nspoof_id = 62144\n"))


def test_validate_catches_problems(tmp_path):
    cfg = load_config(_write(tmp_path, _GOOD))
    cfg.target_usb_index = 0                # same as source
    cfg.profile = "nope.json"               # missing file + clashes with scale
    errors = cfg.validate()
    assert any("must differ" in e for e in errors)
    assert any("profile" in e for e in errors)


def test_build_transform_variants(tmp_path):
    assert isinstance(ProxyConfig(meter_id=1).build_transform(), IdentityTransform)
    assert isinstance(
        ProxyConfig(meter_id=1, scale=0.9).build_transform(), ScaleOffsetTransform
    )
    # profile -> GridTransform
    from sb20proxy.calibration import CalibrationProfile
    prof = CalibrationProfile(kind="grid", target="d", ref="r",
                              breakpoints=[[100, 1.0], [300, 0.9]])
    path = tmp_path / "p.json"
    prof.save(path)
    cfg = ProxyConfig(meter_id=1, profile=str(path))
    assert cfg.validate() == []
    assert isinstance(cfg.build_transform(), GridTransform)


def test_shipped_example_is_valid():
    cfg = load_config(EXAMPLE_TOML)
    assert cfg.validate() == [], "sb20proxy.example.toml must be valid"
    assert cfg.meter_id == 17039


# ---- CLI (validate path only — never triggers the hardware run) ----

def test_cli_validates_good_config(tmp_path, capsys):
    rc = cli.main(["--config", str(_write(tmp_path, _GOOD)), "--validate-config"])
    assert rc == 0
    assert "config OK" in capsys.readouterr().out


def test_cli_rejects_bad_config(tmp_path):
    bad = "[proxy]\nmeter_id = 17039\n[radio]\nsource_usb_index = 0\ntarget_usb_index = 0\n"
    rc = cli.main(["--config", str(_write(tmp_path, bad)), "--validate-config"])
    assert rc == 1


def test_cli_no_config_returns_1(capsys):
    rc = cli.main([])
    assert rc == 1
    assert "needs a config" in capsys.readouterr().out
