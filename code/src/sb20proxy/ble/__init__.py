"""BLE (Bluetooth Low Energy) path — the Cycling Power Service side of the proxy.

The host can only ever be a BLE *central* (bleak can't advertise a GATT peripheral),
so the spoofed-crank *peripheral* lives on the ESP32. But the protocol is a public
Bluetooth SIG standard, so the codec, the relay logic, and a full software loopback
(in-process, no radio — the BLE analogue of the ANT+ LoopbackMaster loop) are all
host-buildable and host-tested here, validated against real frames captured off the
Stages crank and the Assioma (findings/captures/G-*-ble-*.jsonl).
"""

from __future__ import annotations

from .cps import (
    CpsMeasurement,
    CrankCadence,
    CrankCadenceTracker,
    cadence_rpm_from_crank,
    decode_control_point,
    decode_cps_measurement,
    encode_calibration_response,
    encode_cps_measurement,
)
from .crank import BleCrankTarget
from .loopback import LoopbackGatt
from .source import BleReplaySource
from .twin import BleSb20Twin

__all__ = [
    "CpsMeasurement",
    "CrankCadence",
    "CrankCadenceTracker",
    "cadence_rpm_from_crank",
    "decode_cps_measurement",
    "encode_cps_measurement",
    "decode_control_point",
    "encode_calibration_response",
    "LoopbackGatt",
    "BleCrankTarget",
    "BleSb20Twin",
    "BleReplaySource",
]
