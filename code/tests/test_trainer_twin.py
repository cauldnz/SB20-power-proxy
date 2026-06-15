"""TrainerTwin: erg control in software (a head unit drives a software trainer)."""

from __future__ import annotations

import asyncio

import pytest

from sb20proxy.ant.fec import PAGE_TRAINER_DATA, decode_fec_page, encode_target_power
from sb20proxy.ant.master import LoopbackMaster
from sb20proxy.twins.trainer import TrainerTwin


def _nowait(_d):
    return asyncio.sleep(0)


async def _pump(condition, limit=5000):
    for _ in range(limit):
        if condition():
            return True
        await asyncio.sleep(0)
    return False


def _trainer_power(seen):
    return [s.get("instantaneous_power_w") for s in seen
            if (s.get("page", 0) & 0x7F) == PAGE_TRAINER_DATA]


@pytest.mark.asyncio
async def test_erg_control_holds_target():
    master = LoopbackMaster(period_s=0.0, sleep=_nowait)
    trainer = TrainerTwin(master)
    seen: list = []
    master.connect(lambda page: seen.append(decode_fec_page(bytes(page))))

    await trainer.start()
    master.inject_ack(encode_target_power(250))      # controller -> erg 250 W
    ok = await _pump(lambda: 250 in _trainer_power(seen))
    await trainer.stop()

    assert ok, "trainer never reported the erg target"
    assert trainer.target_power == 250.0


@pytest.mark.asyncio
async def test_free_ride_reports_rider_power():
    master = LoopbackMaster(period_s=0.0, sleep=_nowait)
    trainer = TrainerTwin(master)
    trainer.set_rider_power(180)
    seen: list = []
    master.connect(lambda page: seen.append(decode_fec_page(bytes(page))))

    await trainer.start()
    ok = await _pump(lambda: 180 in _trainer_power(seen))
    await trainer.stop()

    assert ok
    assert trainer.target_power is None              # no erg control was sent
