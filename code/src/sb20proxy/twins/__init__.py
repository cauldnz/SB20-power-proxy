"""Digital twins — software models of ANT+/BLE devices.

Each twin runs over a `TwinTransport`, so the same logic works as a pure software
twin (CI), over a real ANT+ stick (on-air loopback), or as one side of an
interaction with a real device. `BikeTwin` is the first concrete twin (the
SB20/display consumer); more device twins (a power-meter source twin, a BLE
display twin) follow the same shape.
"""

from __future__ import annotations

from sb20proxy.twins.base import DeviceTwin
from sb20proxy.twins.bike import BikeTwin
from sb20proxy.twins.meter import PowerMeterTwin
from sb20proxy.twins.trainer import TrainerTwin
from sb20proxy.twins.transport import (
    AntSlaveTransport,
    LoopbackTransport,
    TwinTransport,
)

__all__ = [
    "DeviceTwin",
    "BikeTwin",
    "PowerMeterTwin",
    "TrainerTwin",
    "TwinTransport",
    "LoopbackTransport",
    "AntSlaveTransport",
]
