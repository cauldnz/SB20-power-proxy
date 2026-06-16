"""Full software BLE loopback — the BLE analogue of test_loopback.py.

Wires ReplayFileSource -> ProxyCore -> BleCrankTarget -> LoopbackGatt -> BleSb20Twin
entirely in software (no bleak, no ESP32, no SB20) and asserts the twin sees the spoofed
power stream and the zero-reset (Start Offset Compensation) handshake round-trips. This
proves the BLE relay + calibration path without any radio.
"""

from __future__ import annotations

import asyncio

import pytest

from sb20proxy.ble import BleCrankTarget, BleSb20Twin, LoopbackGatt
from sb20proxy.core import ProxyCore
from sb20proxy.sources.replay import ReplayFileSource

STEADY = "A-stagesL-steady-20260614-165737.jsonl"


def _nowait(_d):
    return asyncio.sleep(0)


async def _pump(cond, limit=8000):
    for _ in range(limit):
        if cond():
            return True
        await asyncio.sleep(0)
    return False


@pytest.mark.asyncio
async def test_ble_loopback_twin_sees_spoofed_power(captures_dir):
    src = ReplayFileSource(captures_dir / STEADY, speed=1e9, sleep=_nowait)
    gatt = LoopbackGatt()
    target = BleCrankTarget(gatt, cal_offset=0)
    twin = BleSb20Twin(gatt)
    core = ProxyCore(src, target)

    await core.start()
    ok = await _pump(lambda: twin.saw_power and twin.notifications > 50)
    await core.stop()

    assert ok, f"twin only saw {twin.notifications} notifications"
    assert 0 <= twin.last_power <= 2000
    assert target.notifications_sent == twin.notifications  # every notify reached the central


@pytest.mark.asyncio
async def test_ble_loopback_zero_reset_handshake(captures_dir):
    src = ReplayFileSource(captures_dir / STEADY, loop=True, speed=1e9, sleep=_nowait)
    gatt = LoopbackGatt()
    target = BleCrankTarget(gatt, cal_offset=-7)  # arbitrary, to prove the offset round-trips
    twin = BleSb20Twin(gatt)
    core = ProxyCore(src, target)

    await core.start()
    await _pump(lambda: twin.notifications > 20)
    twin.request_zero()  # the SB20 central writes Start Offset Compensation
    got = await _pump(lambda: twin.calibration_response is not None)
    await core.stop()

    assert got, "twin never received a calibration response"
    assert twin.calibration_response.success
    assert twin.calibration_response.offset == -7
    assert target.zero_resets_answered == 1


@pytest.mark.asyncio
async def test_ble_loopback_recovers_cadence(captures_dir):
    src = ReplayFileSource(captures_dir / STEADY, speed=1e9, sleep=_nowait)
    gatt = LoopbackGatt()
    twin = BleSb20Twin(gatt)
    core = ProxyCore(src, BleCrankTarget(gatt))

    await core.start()
    await _pump(lambda: twin.last_cadence is not None and twin.notifications > 200)
    await core.stop()

    # Cadence recovered from the crank-revolution deltas lands in a plausible cycling range.
    assert twin.last_cadence is not None
    assert 30 <= twin.last_cadence <= 130
