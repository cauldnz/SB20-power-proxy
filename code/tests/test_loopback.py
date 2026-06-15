"""Full software-loopback integration: the digital-twin bench test.

Wires the real pipeline — ReplayFileSource -> ProxyCore -> StagesAntTarget ->
LoopbackMaster -> BikeTwin — entirely in software (no ANT+ stick, no SB20) and
asserts the twin sees a valid spoofed-Stages stream, including the calibration
handshake. This is the test that proves the proxy works without hardware.
"""

from __future__ import annotations

import asyncio

import pytest

from sb20proxy.ant.master import LoopbackMaster
from sb20proxy.core import ProxyCore
from sb20proxy.sources.replay import ReplayFileSource
from sb20proxy.targets.stages_ant import StagesAntTarget
from sb20proxy.twins import BikeTwin

STEADY = "A-stagesL-steady-20260614-165737.jsonl"


def _nowait(_d):
    return asyncio.sleep(0)


async def _pump(condition, limit=5000):
    """Yield to the event loop until condition() is true or we give up."""
    for _ in range(limit):
        if condition():
            return True
        await asyncio.sleep(0)
    return False


@pytest.mark.asyncio
async def test_decoded_loopback_twin_sees_spoofed_power(captures_dir):
    src = ReplayFileSource(captures_dir / STEADY, speed=1e9, sleep=_nowait)
    master = LoopbackMaster(period_s=0.0, sleep=_nowait)
    target = StagesAntTarget(master, mode="decoded", commons_every=4)
    twin = BikeTwin()
    twin.attach(master)
    core = ProxyCore(src, target)

    await core.start()
    ok = await _pump(lambda: twin.saw_power and twin.manufacturer_id is not None
                     and twin.pages_received > 50)
    await core.stop()

    assert ok, twin.summary()
    assert twin.manufacturer_id == 69           # presents as a Stages meter
    assert twin.serial_number == 11821518
    assert 0 <= twin.last_power <= 2000          # real, sane power
    assert twin.page_counts[0x10] > 0            # power pages flowed


@pytest.mark.asyncio
async def test_decoded_loopback_calibration_handshake(captures_dir):
    src = ReplayFileSource(captures_dir / STEADY, loop=True, speed=1e9, sleep=_nowait)
    master = LoopbackMaster(period_s=0.0, sleep=_nowait)
    target = StagesAntTarget(master, mode="decoded", commons_every=8)
    twin = BikeTwin()
    twin.attach(master)
    core = ProxyCore(src, target)

    await core.start()
    await _pump(lambda: twin.pages_received > 20)
    twin.request_zero()                          # BikeTwin asks for a manual zero
    got = await _pump(lambda: twin.calibration_response is not None)
    await core.stop()

    assert got, "twin never received a calibration response"
    assert twin.calibration_response["calibration_id"] == 0xAC
    assert twin.calibration_response["calibration_data"] == 903


@pytest.mark.asyncio
async def test_verbatim_loopback_replays_real_pages(captures_dir, capture_pages):
    pages = [raw for _decoded, raw in capture_pages(STEADY)][:200]
    master = LoopbackMaster(period_s=0.0, sleep=_nowait)
    target = StagesAntTarget(master, mode="verbatim", verbatim_pages=pages)
    twin = BikeTwin()
    twin.attach(master)

    await target.start()
    ok = await _pump(lambda: twin.pages_received >= len(pages))
    await target.stop()

    assert ok
    # The captured slice carries power + the Stages identity commons.
    assert twin.manufacturer_id == 69
    assert twin.saw_power
    # Every page the twin saw is one of ours (byte-for-byte from the capture).
    assert set(twin.page_counts) <= {0x01, 0x10, 0x12, 0x13, 0x50, 0x51, 0x52}
