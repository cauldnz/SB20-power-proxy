"""Tests for the async erg drive loop (sb20proxy.mcp.driver) — host-tested against the
in-process FTMS twin; the safety invariant is 'Reset always sent on the way out'."""

from __future__ import annotations

import asyncio

import pytest

from sb20proxy.ble.ftms_erg import ErgController, InProcessFtmsServer
from sb20proxy.mcp.driver import ErgDriver


def _transport(srv: InProcessFtmsServer):
    async def t(cmd: bytes) -> bytes:
        return srv.handle(cmd)
    return t


async def _wait_until(pred, *, timeout_s: float = 1.0, step_s: float = 0.01) -> bool:
    for _ in range(int(timeout_s / step_s)):
        if pred():
            return True
        await asyncio.sleep(step_s)
    return pred()


@pytest.mark.asyncio
async def test_converges_to_setpoint():
    srv = InProcessFtmsServer(power_range=(0, 1000, 1))
    d = ErgDriver(ErgController(), lambda: 230, _transport(srv), poll_s=0.01)
    await d._converge_once()
    assert srv.controlled and srv.started
    assert srv.target_power_w == 230


@pytest.mark.asyncio
async def test_follows_setpoint_change():
    srv = InProcessFtmsServer(power_range=(0, 1000, 1))
    sp = {"w": 200}
    d = ErgDriver(ErgController(), lambda: sp["w"], _transport(srv), poll_s=0.01)
    await d._converge_once()
    assert srv.target_power_w == 200
    sp["w"] = 400
    await d._converge_once()
    assert srv.target_power_w == 400


@pytest.mark.asyncio
async def test_none_setpoint_is_safe():
    srv = InProcessFtmsServer(power_range=(0, 1000, 1))
    d = ErgDriver(ErgController(), lambda: None, _transport(srv), poll_s=0.01)
    await d._converge_once()  # claims control + starts, but sets no target (coast)
    assert srv.controlled and srv.started
    assert srv.target_power_w is None


@pytest.mark.asyncio
async def test_release_on_stop_resets_machine():
    srv = InProcessFtmsServer(power_range=(0, 1000, 1))
    d = ErgDriver(ErgController(), lambda: 250, _transport(srv), poll_s=0.01)
    assert d.start() is True
    assert await _wait_until(lambda: srv.target_power_w == 250)
    await d.stop()
    assert d.active is False
    assert srv.controlled is False        # Reset was sent on teardown
    assert srv.target_power_w is None
    assert d.controller.controlled is False


@pytest.mark.asyncio
async def test_release_on_duration_end():
    srv = InProcessFtmsServer(power_range=(0, 1000, 1))
    d = ErgDriver(ErgController(), lambda: 180, _transport(srv), poll_s=0.01)
    await d.run(duration_s=0.03)
    assert d.running is False
    assert srv.controlled is False
    assert srv.target_power_w is None


@pytest.mark.asyncio
async def test_start_twice_is_noop():
    srv = InProcessFtmsServer(power_range=(0, 1000, 1))
    d = ErgDriver(ErgController(), lambda: 100, _transport(srv), poll_s=0.01)
    assert d.start() is True
    assert d.start() is False
    await d.stop()


@pytest.mark.asyncio
async def test_release_records_transport_failure():
    calls = {"n": 0}

    async def flaky(cmd: bytes) -> bytes:
        calls["n"] += 1
        raise RuntimeError("link dropped")

    d = ErgDriver(ErgController(), lambda: 200, flaky, poll_s=0.01)
    # run a single bounded pass; the converge will raise, but the finally must still
    # attempt Reset and swallow its failure (never propagate out of teardown).
    with pytest.raises(RuntimeError):
        await d.run(duration_s=10)
    assert d.last_error is not None and "reset" in d.last_error
    assert d.running is False
