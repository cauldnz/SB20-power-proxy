"""In-process GATT 'air' for the BLE software loop — no radio.

The BLE analogue of `ant.master.LoopbackMaster`: it carries CPS Measurement
notifications from the peripheral (BleCrankTarget) to the central (BleSb20Twin), and
Control-Point writes back the other way, with the peripheral's indicated reply. Pure
and synchronous (callbacks fire in-thread), so the whole BLE relay + calibration
handshake is host-tested with no bleak, no ESP32, no SB20.
"""

from __future__ import annotations

from collections.abc import Callable

Notify = Callable[[bytes], None]


class LoopbackGatt:
    def __init__(self) -> None:
        self._meas_subs: list[Notify] = []
        self._cp_ind_subs: list[Notify] = []
        self._cp_write_handler: Notify | None = None

    # --- peripheral (crank) side ---

    def set_control_point_handler(self, handler: Notify) -> None:
        self._cp_write_handler = handler

    def notify_measurement(self, data: bytes) -> None:
        for cb in list(self._meas_subs):
            cb(data)

    def indicate_control_point(self, data: bytes) -> None:
        for cb in list(self._cp_ind_subs):
            cb(data)

    # --- central (twin) side ---

    def subscribe_measurement(self, cb: Notify) -> None:
        self._meas_subs.append(cb)

    def subscribe_control_point(self, cb: Notify) -> None:
        self._cp_ind_subs.append(cb)

    def write_control_point(self, data: bytes) -> None:
        if self._cp_write_handler is not None:
            self._cp_write_handler(bytes(data))
