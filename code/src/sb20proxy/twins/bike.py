"""BikeTwin — a software SB20/display (the consumer side of a power-meter link).

Decodes the pages a crank (real or spoofed) broadcasts, tracks what it 'sees', and
can send a manual-zero request back. Runs over any transport: in-process loopback
(CI), a real ANT+ stick (on-air loopback / vs a real meter), etc.
"""

from __future__ import annotations

from collections import Counter

from sb20proxy.ant import (
    PAGE_CALIBRATION,
    PAGE_MANUFACTURER_INFO,
    PAGE_POWER_ONLY,
    PAGE_PRODUCT_INFO,
    decode_page,
    encode_calibration_request,
)
from sb20proxy.twins.base import DeviceTwin
from sb20proxy.twins.transport import LoopbackTransport, TwinTransport


class BikeTwin(DeviceTwin):
    """A software SB20/display: tracks the power, identity and calibration it sees."""

    def __init__(self, transport: TwinTransport | None = None, name: str = "SB20-twin") -> None:
        self.pages_received = 0
        self.page_counts: Counter[int] = Counter()
        self.last_power: int | None = None
        self.last_cadence: int | None = None
        self.manufacturer_id: int | None = None
        self.serial_number: int | None = None
        self.calibration_response: dict | None = None
        super().__init__(transport, name)

    @classmethod
    def over_loopback(cls, master, name: str = "SB20-twin") -> BikeTwin:
        """Convenience: a BikeTwin bound to an in-process LoopbackMaster."""
        return cls(LoopbackTransport(master), name)

    def _receive(self, page: bytes) -> None:
        self.pages_received += 1
        decoded = decode_page(bytes(page))
        pm = (decoded.get("page") or 0) & 0x7F
        self.page_counts[pm] += 1
        if pm == PAGE_POWER_ONLY:
            self.last_power = decoded.get("instantaneous_power_w")
            self.last_cadence = decoded.get("instantaneous_cadence_rpm")
        elif pm == PAGE_MANUFACTURER_INFO:
            self.manufacturer_id = decoded.get("manufacturer_id")
        elif pm == PAGE_PRODUCT_INFO:
            self.serial_number = decoded.get("serial_number")
        elif pm == PAGE_CALIBRATION:
            self.calibration_response = decoded

    def request_zero(self) -> None:
        """Send a manual-zero (calibration) request up to the meter."""
        self._send_ack(encode_calibration_request())

    @property
    def saw_power(self) -> bool:
        return self.last_power is not None

    def summary(self) -> str:
        cal = self.calibration_response
        cal_str = (
            f"offset={cal.get('calibration_data')} (id={cal.get('calibration_id_hex')})"
            if cal else "none"
        )
        pages = ", ".join(f"0x{p:02X}:{n}" for p, n in sorted(self.page_counts.items()))
        return (
            f"[{self.name}] {self.pages_received} pages [{pages}] | "
            f"power={self.last_power}W cadence={self.last_cadence} | "
            f"mfr={self.manufacturer_id} serial={self.serial_number} | calibration {cal_str}"
        )
