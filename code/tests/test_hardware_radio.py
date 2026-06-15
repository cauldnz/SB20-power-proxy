"""Hardware tests — need a real ANT+ stick. Skipped unless `pytest --run-hardware`.

A single-stick smoke test of the real radio binding: OpenAntMaster opens, transmits
a few pages as a spoofed crank, and closes without error. This is the most a single
stick can verify (it can't hear its own transmission).

The full on-air loopback (a BikeTwin actually receiving the stream) is a TWO-stick
manual procedure — `03_static_replay.py --radio ant` on one stick +
`10_bike_twin.py --radio ant` on another (or a phone/Garmin as the witness). See
code/README §Tests and forward-plan.md "Two loopbacks".
"""

from __future__ import annotations

import asyncio

import pytest

pytestmark = pytest.mark.hardware


@pytest.mark.asyncio
async def test_openant_master_opens_transmits_closes():
    from sb20proxy.ant.master import ChannelParams
    from sb20proxy.ant.openant_master import OpenAntMaster
    from sb20proxy.reading import PowerReading
    from sb20proxy.targets.stages_ant import StagesAntTarget

    master = OpenAntMaster(ChannelParams(device_number=62145))
    target = StagesAntTarget(master, mode="decoded")
    target.push_reading(PowerReading(timestamp=0.0, power_w=150, source_id="hw-smoke"))

    await target.start()         # opens the channel, primes + threads Node.start()
    await asyncio.sleep(2.0)      # let a few broadcast periods elapse
    await target.stop()
    # Reaching here without an exception means the radio binding opened and transmitted.
