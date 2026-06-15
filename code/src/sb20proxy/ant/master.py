"""The ANT+ master seam: the proxy's transmit side talks to an `AntMaster`, not
to openant directly.

Two implementations:
- `LoopbackMaster` (here) — a pure-software ANT "air": the master's broadcasts are
  delivered in-process to connected listeners (digital twins), and a listener can
  inject acknowledged data (a calibration request) back up to the master. No
  hardware, no openant — runs anywhere, including CI. This is what makes full
  bench testing with digital twins possible.
- `OpenAntMaster` (in `openant_master.py`) — the real radio. Imported only when
  driving an actual ANT+ stick, so the software path never needs openant.

`StagesAntTarget` registers a TX provider (called each broadcast period to get the
next 8-byte page) and an ack handler (called when a slave sends acknowledged data,
e.g. the SB20's zero-reset request).
"""

from __future__ import annotations

import asyncio
from abc import ABC, abstractmethod
from collections.abc import Awaitable, Callable
from dataclasses import dataclass

# ANT+ Bike Power master channel parameters (the captured Stages crank contract).
ANTPLUS_NETWORK_KEY = (0xB9, 0xA5, 0x21, 0xFB, 0xBD, 0x72, 0xC3, 0x45)


@dataclass(frozen=True)
class ChannelParams:
    """The ANT+ channel identity a master broadcasts under."""

    device_number: int = 62144      # Stages L crank
    device_type: int = 0x0B         # Bike Power
    transmission_type: int = 5
    channel_period: int = 8182      # ~4 Hz
    rf_freq: int = 57               # 2457 MHz


TxProvider = Callable[[], bytes]            # returns the next 8-byte page to broadcast
AckHandler = Callable[[bytes], None]        # receives acknowledged data from a slave
Sleeper = Callable[[float], Awaitable[None]]


class AntMaster(ABC):
    """Abstract ANT+ master/transmitter. The target drives it via these hooks."""

    def __init__(self) -> None:
        self._tx_provider: TxProvider | None = None
        self._ack_handler: AckHandler | None = None

    def set_tx_provider(self, provider: TxProvider) -> None:
        """Register the callback that supplies the next 8-byte page each period."""
        self._tx_provider = provider

    def set_ack_handler(self, handler: AckHandler) -> None:
        """Register the callback invoked when a slave sends acknowledged data."""
        self._ack_handler = handler

    @abstractmethod
    async def open(self) -> None:
        """Begin broadcasting (open the radio / start the loopback clock)."""

    @abstractmethod
    async def close(self) -> None:
        """Stop broadcasting and release resources."""


class LoopbackMaster(AntMaster):
    """In-process software 'air'. Broadcasts the TX provider's pages to listeners
    each period; lets a listener inject acknowledged data back to the master.

    period_s/sleep are injectable so tests run with no wall-clock delay. The same
    class powers the `03_static_replay.py --radio loopback` bench demo.
    """

    def __init__(self, *, period_s: float = 0.25, sleep: Sleeper | None = None) -> None:
        super().__init__()
        self._period_s = period_s
        self._sleep: Sleeper = sleep or asyncio.sleep
        self._listeners: list[Callable[[bytes], None]] = []
        self._task: asyncio.Task | None = None
        self._broadcasts = 0

    def connect(self, listener: Callable[[bytes], None]) -> None:
        """Attach a receiver (e.g. a BikeTwin's receive method)."""
        self._listeners.append(listener)

    def inject_ack(self, data: bytes) -> None:
        """A connected slave sends acknowledged data up to the master."""
        if self._ack_handler is not None:
            self._ack_handler(bytes(data))

    @property
    def broadcasts(self) -> int:
        return self._broadcasts

    async def open(self) -> None:
        if self._task is not None:
            raise RuntimeError("LoopbackMaster already open")
        self._task = asyncio.create_task(self._run())

    async def close(self) -> None:
        if self._task is None:
            return
        self._task.cancel()
        try:
            await self._task
        except asyncio.CancelledError:
            pass
        self._task = None

    async def _run(self) -> None:
        while True:
            if self._tx_provider is not None:
                page = self._tx_provider()
                self._broadcasts += 1
                for listener in self._listeners:
                    listener(page)
            await self._sleep(self._period_s)
