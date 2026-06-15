"""AntPowerSource decode + the full Phase 2 software loop (meter twin -> proxy -> bike).

The headline test wires the entire live-proxy chain in software with digital twins:
a PowerMeterTwin broadcasts power, AntPowerSource reads it, ProxyCore (optionally
correcting) feeds StagesAntTarget, and a BikeTwin consumes the spoofed crank — no
ANT+ stick, no bike. The correction test proves a known meter error is recovered.
"""

from __future__ import annotations

import asyncio

import pytest

from sb20proxy.ant import encode_power_only
from sb20proxy.ant.master import LoopbackMaster
from sb20proxy.core import ProxyCore
from sb20proxy.reading import PowerReading
from sb20proxy.sources.ant_power import AntPowerSource
from sb20proxy.targets.stages_ant import StagesAntTarget
from sb20proxy.transform import ScaleOffsetTransform
from sb20proxy.twins import BikeTwin, LoopbackTransport, PowerMeterTwin


def _nowait(_d):
    return asyncio.sleep(0)


async def _pump(condition, limit=8000):
    for _ in range(limit):
        if condition():
            return True
        await asyncio.sleep(0)
    return False


# ----------------------------- decode -----------------------------

@pytest.mark.asyncio
async def test_source_decodes_power_pages():
    master = LoopbackMaster(period_s=0.0, sleep=_nowait)
    source = AntPowerSource(LoopbackTransport(master), source_id="meter:99")
    got: list[PowerReading] = []
    source.on_reading(got.append)
    await source.start()
    # deliver a known 0x10 page through the loopback transport
    page = encode_power_only(event_count=5, instantaneous_power_w=242,
                             cadence_rpm=88, accumulated_power=1000)
    master.inject_broadcast(page)
    await source.stop()

    assert len(got) == 1
    assert got[0].power_w == 242
    assert got[0].cadence_rpm == 88
    assert got[0].source_id == "meter:99"


# --------------------- full Phase 2 software loop ---------------------

async def _build_loop(*, meter_error=None, transform=None, commons_every=4):
    """meter twin (busA) -> AntPowerSource -> ProxyCore -> StagesAntTarget (busB) -> BikeTwin."""
    meter_bus = LoopbackMaster(period_s=0.0, sleep=_nowait)
    meter = PowerMeterTwin(meter_bus, error=meter_error, commons_every=999999)
    source = AntPowerSource(LoopbackTransport(meter_bus), source_id="meter")
    crank_bus = LoopbackMaster(period_s=0.0, sleep=_nowait)
    crank = StagesAntTarget(crank_bus, mode="decoded", commons_every=commons_every)
    proxy = ProxyCore(source, crank, transform)
    bike = BikeTwin.over_loopback(crank_bus)

    # order: register receivers (bike on busB, source on busA) before their buses open.
    await bike.start()
    await proxy.start()   # opens crank_bus (broadcasts) + registers source on meter_bus
    return meter, meter_bus, proxy, bike


@pytest.mark.asyncio
async def test_full_software_loop_relays_power():
    meter, meter_bus, proxy, bike = await _build_loop()
    meter.push_reading(PowerReading(timestamp=0.0, power_w=250, cadence_rpm=90, source_id="rider"))
    await meter.start()  # meter_bus opens; meter broadcasts 250 W

    ok = await _pump(lambda: bike.last_power == 250 and bike.pages_received > 30)
    await meter.stop()
    await proxy.stop()
    await bike.stop()

    assert ok, f"bike saw {bike.last_power}"
    assert bike.last_power == 250          # relayed faithfully (identity transform)
    assert proxy.stats.readings_forwarded > 0


@pytest.mark.asyncio
async def test_full_software_loop_corrects_meter_error():
    # Meter reports 10% high; a ScaleOffsetTransform(1/1.1) corrects it back to truth.
    meter, meter_bus, proxy, bike = await _build_loop(
        meter_error=lambda p, c: p * 1.10,
        transform=ScaleOffsetTransform(scale=1.0 / 1.10),
    )
    meter.push_reading(PowerReading(timestamp=0.0, power_w=200, cadence_rpm=85, source_id="rider"))
    await meter.start()

    # raw meter reports 220; corrected should land back on ~200.
    ok = await _pump(lambda: bike.last_power is not None and bike.pages_received > 30
                     and proxy.stats.last_source_reading is not None)
    await meter.stop()
    await proxy.stop()
    await bike.stop()

    assert ok
    assert proxy.stats.last_source_reading.power_w == 220   # the meter's (errored) reading
    assert abs(bike.last_power - 200) <= 2                  # corrected back to true power
