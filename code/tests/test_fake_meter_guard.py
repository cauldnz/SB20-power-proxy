"""Unit tests for fake_meter's advertising guard (hardware-free).

Guards the 2026-09-23 time sink: two fake_meter processes were running, the second one's WinRT
publisher was permanently ABORTED, and the script cheerfully printed `tx 180 W ...` forever at
nobody. Nothing was on the air, so the ESP32 never found a meter — and the debugging went to the
firmware. `_await_advertising` is what makes that state loud instead of silent.

The subtlety it has to respect: a momentary ABORTED right after start() is a NORMAL publisher
transition (decisions.md 2026-06-22), so the guard must wait for it to settle rather than fail on
the first read. Only the wait logic is exercised here; the WinRT peripheral itself is a hardware
seam.
"""

from __future__ import annotations

import asyncio
import importlib.util
from pathlib import Path

import pytest

_FAKE_METER = Path(__file__).resolve().parents[1] / "scripts" / "fake_meter.py"


def _load():
    spec = importlib.util.spec_from_file_location("fake_meter", _FAKE_METER)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


try:
    fake_meter = _load()
except ImportError as e:  # pragma: no cover - platform guard
    # fake_meter imports the WinRT GATT server, which is deliberately Windows-only and kept out
    # of the package import graph (decisions.md 2026-06-22). Skip cleanly on Linux CI rather than
    # failing collection — the guard it tests only ever runs on the Windows bench anyway.
    pytest.skip(f"fake_meter needs WinRT (Windows-only): {e}", allow_module_level=True)


class _Periph:
    """Stand-in for WinrtCpsPeripheral: reports `advertising` from a scripted sequence."""

    def __init__(self, sequence):
        self._seq = list(sequence)
        self.reads = 0

    @property
    def advertising(self) -> bool:
        self.reads += 1
        # Hold the last value once the script runs out (a settled publisher stays settled).
        return self._seq[min(self.reads - 1, len(self._seq) - 1)]


def test_returns_true_immediately_when_already_advertising():
    p = _Periph([True])
    assert asyncio.run(fake_meter._await_advertising(p, timeout=1.0)) is True
    assert p.reads == 1, "should not keep polling once it has settled"


def test_waits_through_the_normal_momentary_aborted_transition():
    # False, False, then True: the documented start-up transition, which must NOT be a failure.
    p = _Periph([False, False, True])
    assert asyncio.run(fake_meter._await_advertising(p, timeout=3.0)) is True


def test_gives_up_when_the_publisher_never_starts():
    p = _Periph([False])
    assert asyncio.run(fake_meter._await_advertising(p, timeout=0.6)) is False


def test_respects_the_timeout_rather_than_hanging():
    p = _Periph([False])
    loop_timer = asyncio.run(_timed(p))
    # Must return near the deadline, not spin forever or return instantly.
    assert 0.4 <= loop_timer <= 3.0


async def _timed(p) -> float:
    import time
    t0 = time.monotonic()
    await fake_meter._await_advertising(p, timeout=0.8)
    return time.monotonic() - t0


def test_run_aborts_with_an_actionable_message_when_advertising_fails(monkeypatch):
    """The whole point: refuse to stream at nobody, and say why."""
    stopped = []

    class _Dead:
        advertising = False

        async def start(self):
            return None

        def stop(self):
            stopped.append(True)

    monkeypatch.setattr(fake_meter, "WinrtCpsPeripheral", lambda: _Dead())
    # Keep the test fast: the guard's own timeout is what we are short-circuiting.
    monkeypatch.setattr(fake_meter, "_await_advertising",
                        lambda periph, timeout=5.0: _false())

    with pytest.raises(SystemExit) as e:
        asyncio.run(fake_meter.run(_Args()))
    msg = str(e.value)
    assert "never started" in msg
    assert "peripheral role" in msg, "must name the likely cause, not just fail"
    assert stopped, "must release the publisher before exiting"


async def _false() -> bool:
    return False


class _Args:
    watts = 180
    cadence = 85
    hz = 1.0
    steady = True
    duration = 0
    balance = None
