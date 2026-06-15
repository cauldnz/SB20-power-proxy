"""StagesAntTarget — broadcast as a spoofed Stages crank via an `AntMaster`.

This is the proxy's transmit side. It feeds the master one 8-byte page per
broadcast period and answers the SB20's zero-reset request with the captured
calibration response. It is radio-agnostic: drive it with a `LoopbackMaster`
(software, digital-twin bench testing) or an `OpenAntMaster` (real ANT+ stick).

Two modes:
- "decoded": build page 0x10 (Power-Only) from the current PowerReading, with a
  manufacturer/product/battery commons burst interleaved periodically. This is the
  live-proxy path (works straight off an Assioma source in Phase 2). It needs no
  torque accumulators — power, cadence, balance and an accumulated-power counter
  are all page 0x10 carries.
- "verbatim": re-broadcast a captured page sequence byte-for-byte (highest fidelity
  for the Phase 1 hardware proof). The captured stream already carries its own
  commons, so none are injected.

In both modes a zero-reset request (acknowledged page 0x01, cal id 0xAA) makes the
next few broadcasts the 0x01 0xAC response (the real crank replied as a broadcast).
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass

from sb20proxy.ant import (
    PAGE_CALIBRATION,
    decode_page,
    encode_battery_status,
    encode_calibration_response,
    encode_manufacturer_info,
    encode_power_only,
    encode_product_info,
    pedal_power_byte,
)
from sb20proxy.ant.master import AntMaster
from sb20proxy.ant.pages import CAL_ID_MANUAL_ZERO_REQUEST
from sb20proxy.reading import PowerReading
from sb20proxy.targets import PowerTarget

# ~4 Hz broadcast => 120 pages ≈ 30 s, matching the real crank's commons cadence.
DEFAULT_COMMONS_EVERY = 120
# Captured Stages calibration offset (C0-ack-dryrun).
DEFAULT_CALIBRATION_OFFSET = 903


@dataclass(frozen=True)
class StagesIdentity:
    """Page-content identity of the spoofed crank (from the real captures)."""

    manufacturer_id: int = 69
    model_number: int = 3
    hw_revision: int = 3
    sw_revision_main: int = 18
    sw_revision_supp: int = 2
    serial_number: int = 11821518
    # Battery (page 0x52), from "52 ff 01 d5 e5 01 92 b2".
    battery_id: int = 1
    operating_time_lsb: int = 0x01E5D5
    battery_voltage_frac: int = 0x92
    battery_status_byte: int = 0xB2


class StagesAntTarget(PowerTarget):
    def __init__(
        self,
        master: AntMaster,
        *,
        identity: StagesIdentity | None = None,
        mode: str = "decoded",
        verbatim_pages: list[bytes] | None = None,
        calibration_offset: int = DEFAULT_CALIBRATION_OFFSET,
        commons_every: int = DEFAULT_COMMONS_EVERY,
    ) -> None:
        if mode not in ("decoded", "verbatim"):
            raise ValueError(f"mode must be 'decoded' or 'verbatim', got {mode!r}")
        if mode == "verbatim" and not verbatim_pages:
            raise ValueError("verbatim mode requires verbatim_pages")
        self._master = master
        self._identity = identity or StagesIdentity()
        self._mode = mode
        self._verbatim_pages = [bytes(p) for p in (verbatim_pages or [])]
        self._calibration_offset = calibration_offset
        # Verbatim captures already contain commons; don't double them up.
        self._commons_every = 0 if mode == "verbatim" else commons_every

        self._current: PowerReading | None = None
        self._event_count = 0
        self._accum_power = 0
        self._verbatim_i = 0
        self._since_commons = 0
        self._cal_pending = 0
        self._pending: deque[bytes] = deque()

        master.set_tx_provider(self._next_page)
        master.set_ack_handler(self._on_ack)

    # ---- PowerTarget API ----

    def push_reading(self, r: PowerReading) -> None:
        self._current = r

    async def start(self) -> None:
        # Reset stream state and lead with the identity commons so a display sees
        # who we are immediately, then open the radio / loopback.
        self._event_count = 0
        self._accum_power = 0
        self._verbatim_i = 0
        self._since_commons = 0
        self._cal_pending = 0
        self._pending = deque(self._commons_pages()) if self._commons_every else deque()
        await self._master.open()

    async def stop(self) -> None:
        await self._master.close()

    # ---- page generation (the TX provider) ----

    def _next_page(self) -> bytes:
        # 1. A pending calibration response wins (answer the bike promptly).
        if self._cal_pending > 0:
            self._cal_pending -= 1
            return encode_calibration_response(calibration_data=self._calibration_offset)
        # 2. A queued commons-burst page.
        if self._pending:
            return self._pending.popleft()
        # 3. A data page; schedule the next commons burst periodically.
        if self._commons_every:
            self._since_commons += 1
            if self._since_commons >= self._commons_every:
                self._since_commons = 0
                self._pending.extend(self._commons_pages())
                return self._pending.popleft()
        return self._data_page()

    def _data_page(self) -> bytes:
        if self._mode == "verbatim":
            page = self._verbatim_pages[self._verbatim_i % len(self._verbatim_pages)]
            self._verbatim_i += 1
            return page
        return self._decoded_power_page()

    def _decoded_power_page(self) -> bytes:
        r = self._current
        power = max(0, int(r.power_w)) if r else 0
        cadence = r.cadence_rpm if r else None
        balance = r.left_balance if r else None
        self._event_count = (self._event_count + 1) & 0xFF
        self._accum_power = (self._accum_power + power) & 0xFFFF
        pedal = pedal_power_byte(balance) if balance is not None else 0xFF
        return encode_power_only(
            event_count=self._event_count,
            instantaneous_power_w=power & 0xFFFF,
            accumulated_power=self._accum_power,
            cadence_rpm=cadence,
            pedal_power=pedal,
        )

    def _commons_pages(self) -> list[bytes]:
        i = self._identity
        return [
            encode_manufacturer_info(
                hw_revision=i.hw_revision, manufacturer_id=i.manufacturer_id,
                model_number=i.model_number,
            ),
            encode_product_info(
                sw_revision_main=i.sw_revision_main, sw_revision_supp=i.sw_revision_supp,
                serial_number=i.serial_number,
            ),
            encode_battery_status(
                battery_id=i.battery_id, operating_time_lsb=i.operating_time_lsb,
                battery_voltage_frac=i.battery_voltage_frac,
                battery_status_byte=i.battery_status_byte,
            ),
        ]

    # ---- ack (the bike's zero-reset request) ----

    def _on_ack(self, data: bytes) -> None:
        decoded = decode_page(bytes(data))
        if (decoded.get("page") or 0) & 0x7F != PAGE_CALIBRATION:
            return
        if decoded.get("calibration_id") == CAL_ID_MANUAL_ZERO_REQUEST:
            # Emit the success response on the next few broadcasts.
            self._cal_pending = 3
