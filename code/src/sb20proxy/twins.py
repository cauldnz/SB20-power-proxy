"""Digital twins — software stand-ins for the real ANT+ devices, so the proxy can
be bench-tested end to end without a stick or a bike.

`BikeTwin` is the consumer twin: it plays the role of the SB20 (or any ANT+ Bike
Power display) receiving from a spoofed crank. It decodes the pages the master
broadcasts, tracks what it 'sees' (power, cadence, identity, calibration), and can
send a manual-zero request back up — exercising the full calibration handshake.

Moving forward, the same pattern extends to other twins (a meter twin that feeds
the proxy as a source, a BLE display twin, etc.).
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
from sb20proxy.ant.master import LoopbackMaster


class BikeTwin:
    """A software SB20/display: receives broadcast pages and tracks what it sees."""

    def __init__(self, name: str = "SB20-twin") -> None:
        self.name = name
        self.pages_received = 0
        self.page_counts: Counter[int] = Counter()
        self.last_power: int | None = None
        self.last_cadence: int | None = None
        self.manufacturer_id: int | None = None
        self.serial_number: int | None = None
        self.calibration_response: dict | None = None
        self._master: LoopbackMaster | None = None

    def attach(self, master: LoopbackMaster) -> None:
        """Connect to a loopback master so its broadcasts arrive at receive()."""
        self._master = master
        master.connect(self.receive)

    def receive(self, page: bytes) -> None:
        """Listener entry point — called for each broadcast page."""
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
        """Send a manual-zero (calibration) request up to the master."""
        if self._master is None:
            raise RuntimeError("BikeTwin not attached to a master")
        self._master.inject_ack(encode_calibration_request())

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
