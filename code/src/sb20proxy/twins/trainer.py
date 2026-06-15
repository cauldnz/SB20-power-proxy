"""TrainerTwin — a software ANT+ FE-C smart trainer.

Broadcasts trainer data (FE-C pages 0x10 + 0x19) as a master, and accepts erg
control: a head unit / controller sends a Target Power page (0x31) and the twin
holds the rider at that power (erg) and reports it. This is the *control* half of
the digital-twin bench — the complement to the power-broadcast twins — and a model
of the SB20's own trainer side (and a stand-in for a real TacX Neo).
"""

from __future__ import annotations

from sb20proxy.ant.fec import (
    PAGE_TARGET_POWER,
    decode_target_power,
    encode_general_fe_data,
    encode_trainer_data,
)
from sb20proxy.ant.master import AntMaster


class TrainerTwin:
    """A software FE-C trainer: broadcasts power, obeys erg Target Power control."""

    def __init__(self, master: AntMaster, *, name: str = "trainer-twin", cadence: int = 85) -> None:
        self._master = master
        self.name = name
        self.cadence = cadence
        self.target_power: float | None = None   # set by erg control (page 0x31)
        self.free_power = 0                       # power when not in erg (rider's own)
        self._event = 0
        self._accum = 0
        self._elapsed_qs = 0
        self._distance = 0
        self._toggle = 0
        master.set_tx_provider(self._next_page)
        master.set_ack_handler(self._on_ack)

    def set_rider_power(self, watts: int) -> None:
        """The power the 'rider' produces when NOT under erg control (free ride)."""
        self.free_power = max(0, int(watts))

    @property
    def reported_power(self) -> int:
        # erg holds the rider at target; otherwise it's the free-ride power.
        return round(self.target_power) if self.target_power is not None else self.free_power

    async def start(self) -> None:
        self._event = self._accum = self._elapsed_qs = self._distance = 0
        await self._master.open()

    async def stop(self) -> None:
        await self._master.close()

    def _on_ack(self, data: bytes) -> None:
        if (data[0] & 0x7F) == PAGE_TARGET_POWER:
            self.target_power = decode_target_power(bytes(data))

    def _next_page(self) -> bytes:
        power = self.reported_power
        self._toggle ^= 1
        if self._toggle:  # alternate trainer-data / general-FE-data
            self._event = (self._event + 1) & 0xFF
            self._accum = (self._accum + power) & 0xFFFF
            return encode_trainer_data(
                event_count=self._event, instantaneous_power_w=power,
                accumulated_power=self._accum, cadence_rpm=self.cadence,
            )
        self._elapsed_qs = (self._elapsed_qs + 1) & 0xFF
        self._distance = (self._distance + 1) & 0xFF
        return encode_general_fe_data(
            elapsed_time_quarter_s=self._elapsed_qs, distance_m=self._distance,
            speed_mm_s=power * 30,  # a plausible speed-from-power stand-in
        )
