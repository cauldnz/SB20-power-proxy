"""BleSb20Twin — a software SB20 acting as the BLE central (the consumer side).

Subscribes to CPS Measurement notifications (decodes power, recovers cadence from the
crank-revolution deltas exactly as a head unit does), and can fire a zero-reset on the
control point and capture the indicated response. The BLE analogue of BikeTwin; runs
over the in-process LoopbackGatt in CI (or, later, a real bleak central).
"""

from __future__ import annotations

from .cps import (
    CP_START_OFFSET_COMPENSATION,
    ControlPointResponse,
    cadence_rpm_from_crank,
    decode_control_point,
    decode_cps_measurement,
)
from .loopback import LoopbackGatt


class BleSb20Twin:
    def __init__(self, gatt: LoopbackGatt, name: str = "SB20-ble-twin") -> None:
        self.name = name
        self.notifications = 0
        self.last_power: int | None = None
        self.last_cadence: int | None = None
        self.calibration_response: ControlPointResponse | None = None
        self._prev_crank: tuple[int, int] | None = None
        self._gatt = gatt
        gatt.subscribe_measurement(self._on_measurement)
        gatt.subscribe_control_point(self._on_control_point)

    def _on_measurement(self, data: bytes) -> None:
        self.notifications += 1
        m = decode_cps_measurement(data)
        self.last_power = m.power_w
        if m.cumulative_crank_revs is not None and m.last_crank_event_time is not None:
            if self._prev_crank is not None:
                rpm = cadence_rpm_from_crank(self._prev_crank[0], self._prev_crank[1],
                                             m.cumulative_crank_revs, m.last_crank_event_time)
                if rpm > 0:
                    self.last_cadence = round(rpm)
            self._prev_crank = (m.cumulative_crank_revs, m.last_crank_event_time)

    def _on_control_point(self, data: bytes) -> None:
        decoded = decode_control_point(data)
        if isinstance(decoded, ControlPointResponse):
            self.calibration_response = decoded

    def request_zero(self) -> None:
        """Write a Start Offset Compensation (the BLE manual-zero) to the control point."""
        self._gatt.write_control_point(bytes([CP_START_OFFSET_COMPENSATION]))

    @property
    def saw_power(self) -> bool:
        return self.last_power is not None
