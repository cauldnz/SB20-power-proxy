"""Every hardware script must be *importable* without its optional dependency.

A script that calls `sys.exit()` from a module-level `except ImportError:` handler
is a landmine: any importer — a test, a sibling script, a tool — dies with
`SystemExit` instead of a catchable error. When pytest hits that during
collection it aborts the whole module rather than skipping it, so a hermetic
suite stops being hermetic the moment someone installs without an extra.

That is not hypothetical. `tests/test_capture_multi.py` loads
`scripts/07_capture_multi.py` by path purely to unit-test `parse_meter_spec`, a
function with no hardware in it at all; before this guard, a machine without
`openant` importable lost those tests to a collection crash.

The scripts keep their friendly message when a human runs them directly — the
guard is dual-mode (`if __name__ == "__main__"` exits, otherwise it raises). This
test pins the *importable* half of that contract, which is the half nothing else
exercises.
"""
from __future__ import annotations

import importlib.abc
import importlib.util
import sys
from pathlib import Path

import pytest

_SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"

# Add a row whenever a script grows a module-level optional-dependency guard.
GUARDED_SCRIPTS = [
    ("01_capture_stages.py", "openant"),
    ("03_ingest_jsonl_to_influx.py", "influxdb_client"),
    ("06_capture_ble.py", "bleak"),
    ("07_capture_multi.py", "openant"),
    ("16_scan_ant.py", "openant"),
    ("bench_s3.py", "serial"),
    ("capture_ble_multi.py", "bleak"),
    ("capture_ftms.py", "bleak"),
    ("crank_reader.py", "bleak"),
    ("fake_meter.py", "sb20proxy.ble.winrt_peripheral"),
    ("ftms_hw_loop.py", "bleak"),
]

_IDS = [s for s, _ in GUARDED_SCRIPTS]
_guarded = pytest.mark.parametrize(("script", "blocked"), GUARDED_SCRIPTS, ids=_IDS)


class _BlockFinder(importlib.abc.MetaPathFinder):
    """Make one module (and its submodules) look uninstalled."""

    def __init__(self, blocked: str) -> None:
        self._blocked = blocked

    def find_spec(self, fullname, path=None, target=None):
        if fullname == self._blocked or fullname.startswith(self._blocked + "."):
            raise ImportError(f"simulated: no module named {fullname!r}")
        return None


@pytest.fixture
def block_module():
    """Temporarily hide a module from the import system, then restore it."""
    saved_modules: dict[str, object] = {}
    finder: _BlockFinder | None = None

    def _block(name: str) -> None:
        nonlocal finder
        for key in [k for k in sys.modules if k == name or k.startswith(name + ".")]:
            saved_modules[key] = sys.modules.pop(key)
        finder = _BlockFinder(name)
        sys.meta_path.insert(0, finder)

    yield _block

    if finder is not None and finder in sys.meta_path:
        sys.meta_path.remove(finder)
    sys.modules.update(saved_modules)


def _import_by_path(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@_guarded
def test_script_raises_importerror_not_systemexit(script, blocked, block_module):
    """Importing a hardware script without its dep raises ImportError, not SystemExit."""
    path = _SCRIPTS / script
    assert path.exists(), f"{script} moved or was renamed — update GUARDED_SCRIPTS"

    block_module(blocked)

    with pytest.raises(ImportError) as excinfo:
        _import_by_path(path, f"{path.stem}_guard_probe")

    # The actionable install hint must survive into the raised error, so an
    # importer that surfaces the message still tells the human what to do.
    assert str(excinfo.value).strip(), f"{script} raised an ImportError with no message"


@_guarded
def test_script_guard_does_not_leak_systemexit(script, blocked, block_module):
    """Pin the specific regression: SystemExit escaping module import."""
    block_module(blocked)
    try:
        _import_by_path(_SCRIPTS / script, f"{Path(script).stem}_exit_probe")
    except SystemExit as exc:  # pragma: no cover - this is the failure we forbid
        pytest.fail(
            f"{script} called sys.exit({exc.code}) at import time. Use the dual-mode "
            "guard: exit only under `if __name__ == \"__main__\"`, otherwise raise "
            "ImportError so importers can skip."
        )
    except ImportError:
        pass  # the contract
