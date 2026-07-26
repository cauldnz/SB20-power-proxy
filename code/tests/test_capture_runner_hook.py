"""The capture runner's observer hook, and the reason it exists.

`ride_wizard.py` needs to watch power/cadence go by so its cue thread can print a live
heartbeat. It used to get that by overriding `_on_data` and re-implementing the parent's
decode-and-log body. Two lines, easy to miss - and on the ride-day path, where a capture that
silently stops logging is expensive and only discovered afterwards.

`_on_decoded(kind, decoded)` is the fix: observe without re-implementing. These tests pin the
half that matters, which is that an observer *cannot* cost you the log line.

`CaptureRunner` is instantiated but never `setup()` - no radio is opened, so this stays
hermetic. Only its callback plumbing is under test.
"""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path

import pytest

pytest.importorskip("openant", reason="01_capture_stages.py imports openant at module scope")

_SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "01_capture_stages.py"
_spec = importlib.util.spec_from_file_location("capture_stages_under_test", _SCRIPT)
capture_stages = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(capture_stages)

# A real ANT+ power page 0x10 (standard power-only), as decoded elsewhere in the suite.
_POWER_PAGE = bytes([0x10, 0x0A, 0xFF, 0x50, 0x10, 0x27, 0x64, 0x00])


@pytest.fixture
def runner(tmp_path):
    return capture_stages.CaptureRunner(device_id=62144, output_path=tmp_path / "cap.jsonl")


def _records(path: Path) -> list[dict]:
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line]


def test_broadcast_is_logged_and_observed(runner, tmp_path):
    seen: list[tuple[str, dict]] = []
    runner._on_decoded = lambda kind, decoded: seen.append((kind, decoded))

    runner._on_data(_POWER_PAGE)

    recs = [r for r in _records(tmp_path / "cap.jsonl") if r.get("kind") == "broadcast"]
    assert len(recs) == 1, "the log line is the capture; it must survive an observer"
    assert seen == [("broadcast", recs[0]["data"])], "the observer sees exactly what was logged"


def test_acknowledged_is_logged_and_observed(runner, tmp_path):
    """Acks carry pairing/calibration traffic, so they get the same treatment."""
    seen: list[tuple[str, dict]] = []
    runner._on_decoded = lambda kind, decoded: seen.append((kind, decoded))

    runner._on_acknowledged(_POWER_PAGE)

    recs = [r for r in _records(tmp_path / "cap.jsonl") if r.get("kind") == "acknowledged"]
    assert len(recs) == 1
    assert seen[0][0] == "acknowledged", "the kind lets an observer tell the two paths apart"


def test_default_hook_is_a_no_op(runner, tmp_path):
    """A plain capture run must be unaffected by the hook's existence."""
    runner._on_data(_POWER_PAGE)
    runner._on_acknowledged(_POWER_PAGE)
    kinds = [r.get("kind") for r in _records(tmp_path / "cap.jsonl")]
    assert kinds.count("broadcast") == 1
    assert kinds.count("acknowledged") == 1


def test_ride_wizard_observes_without_reimplementing_the_log(tmp_path):
    """The regression this hook was introduced for, end to end.

    The guided runner must pick up power/cadence *and* leave the capture intact. Before the
    hook it overrode `_on_data`, so those two outcomes were coupled to a hand-copied body.
    """
    ride_wizard_path = _SCRIPT.parent / "ride_wizard.py"
    spec = importlib.util.spec_from_file_location("ride_wizard_under_test", ride_wizard_path)
    ride_wizard = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ride_wizard)

    guided_cls = ride_wizard.make_guided_runner_class(capture_stages)
    guided = guided_cls(device_id=62144, output_path=tmp_path / "guided.jsonl")

    assert "_on_data" not in guided_cls.__dict__, (
        "the guided runner must observe via _on_decoded, not by re-implementing _on_data"
    )

    guided._on_data(_POWER_PAGE)

    recs = [r for r in _records(tmp_path / "guided.jsonl") if r.get("kind") == "broadcast"]
    assert len(recs) == 1, "the guided runner still writes the capture"
    decoded = recs[0]["data"]
    assert guided.last_power == decoded.get("instantaneous_power_w")
    assert guided.last_cadence == decoded.get("instantaneous_cadence_rpm")
    assert guided.last_power is not None, "the heartbeat needs a real value to show"


def test_guided_runner_ignores_acknowledged_pages(tmp_path):
    """The heartbeat reflects broadcast telemetry, not pairing chatter."""
    ride_wizard_path = _SCRIPT.parent / "ride_wizard.py"
    spec = importlib.util.spec_from_file_location("ride_wizard_ack_probe", ride_wizard_path)
    ride_wizard = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ride_wizard)

    guided = ride_wizard.make_guided_runner_class(capture_stages)(
        device_id=62144, output_path=tmp_path / "ack.jsonl")
    guided._on_acknowledged(_POWER_PAGE)

    assert guided.last_power is None and guided.last_cadence is None
    kinds = [r.get("kind") for r in _records(tmp_path / "ack.jsonl")]
    assert kinds.count("acknowledged") == 1, "but it is still captured"
