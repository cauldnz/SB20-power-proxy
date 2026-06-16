"""BleCrankTarget — the spoofed Stages crank as a BLE Cycling Power peripheral.

On the host this drives the in-process LoopbackGatt (software loop); on the ESP32 the
same logic lives in firmware (NimBLE / BleCrankPeripheral). Each PowerReading becomes a
CPS Measurement notification (power + crank-revolution cadence), and a zero-reset (Start
Offset Compensation) written to the control point is answered with the calibration
offset — the BLE analogue of StagesAntTarget's page-0x01 reply.
"""

from __future__ import annotations

from sb20proxy.reading import PowerReading
from sb20proxy.targets import PowerTarget

from .cps import (
    CP_START_OFFSET_COMPENSATION,
    CrankCadence,
    decode_control_point,
    encode_calibration_response,
    encode_cps_measurement,
)
from .loopback import LoopbackGatt


class BleCrankTarget(PowerTarget):
    def __init__(self, gatt: LoopbackGatt, *, cal_offset: int = 0,
                 emit_cadence: bool = True) -> None:
        self._gatt = gatt
        self._cal_offset = cal_offset
        self._emit_cadence = emit_cadence
        self._cadence = CrankCadence()
        self._last_ts: float | None = None
        self.notifications_sent = 0
        self.zero_resets_answered = 0
        gatt.set_control_point_handler(self._on_control_point)

    async def start(self) -> None:
        pass

    async def stop(self) -> None:
        pass

    def push_reading(self, r: PowerReading) -> None:
        crank_fields: dict[str, int] = {}
        if self._emit_cadence:
            if self._last_ts is not None and r.cadence_rpm:
                dt_ms = int(max(0.0, (r.timestamp - self._last_ts) * 1000.0))
                self._cadence.advance(float(r.cadence_rpm), dt_ms)
            self._last_ts = r.timestamp
            crank_fields = {
                "cumulative_crank_revs": self._cadence.cumulative_revs,
                "last_crank_event_time": self._cadence.last_event_time,
            }
        data = encode_cps_measurement(r.power_w, **crank_fields)
        self._gatt.notify_measurement(data)
        self.notifications_sent += 1

    def _on_control_point(self, data: bytes) -> None:
        decoded = decode_control_point(data)
        # A request (not a response) for Start Offset Compensation -> reply with the offset.
        if isinstance(decoded, tuple) and decoded[1] == CP_START_OFFSET_COMPENSATION:
            self._gatt.indicate_control_point(encode_calibration_response(self._cal_offset))
            self.zero_resets_answered += 1
