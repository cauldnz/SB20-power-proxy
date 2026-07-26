"""Unit tests for the generic meter-spec parsing in scripts/07_capture_multi.py.

The script's filename starts with a digit, so load it by path (the same
spec_from_file_location pattern the script itself uses to reuse decode_page).
"""
import importlib.util
from pathlib import Path

import pytest

# `parse_meter_spec` is pure string parsing, but it lives in a script whose module
# body imports openant. Skip rather than error if the ANT+ stack isn't importable
# (see test_script_import_guards.py for the dual-mode guard this relies on).
pytest.importorskip("openant", reason="07_capture_multi.py imports openant at module scope")

_PATH = Path(__file__).resolve().parents[1] / "scripts" / "07_capture_multi.py"
_spec = importlib.util.spec_from_file_location("capture_multi_under_test", _PATH)
capture_multi = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(capture_multi)
parse_meter_spec = capture_multi.parse_meter_spec


def test_parse_meter_spec_valid():
    assert parse_meter_spec("xcadey:12345") == ("xcadey", 12345)
    assert parse_meter_spec("assioma:17039") == ("assioma", 17039)


def test_parse_meter_spec_trims_whitespace():
    assert parse_meter_spec("  xcadey : 12345 ") == ("xcadey", 12345)


@pytest.mark.parametrize(
    "bad",
    ["", "noColon", "xcadey:", ":12345", "xcadey:abc", "xcadey:1:2", "  :  "],
)
def test_parse_meter_spec_rejects_malformed(bad):
    with pytest.raises(ValueError):
        parse_meter_spec(bad)
