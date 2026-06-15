"""DeviceTwin — base for software models of ANT+/BLE devices.

A twin holds the device's logic (decode what it receives, track state, decide what
to send back) and runs over any `TwinTransport`. That single seam is what lets the
same twin be a pure digital twin (CI), a real-radio participant (loopback on a
stick), or one side of an interaction with a real device.
"""

from __future__ import annotations

from abc import ABC, abstractmethod

from sb20proxy.twins.transport import TwinTransport


class DeviceTwin(ABC):
    """Transport-agnostic device model. Subclasses implement `_receive`."""

    def __init__(self, transport: TwinTransport | None = None, name: str = "twin") -> None:
        self.name = name
        self._transport: TwinTransport | None = None
        if transport is not None:
            self.bind(transport)

    def bind(self, transport: TwinTransport) -> None:
        """Attach a transport; its inbound pages will arrive at `_receive`."""
        self._transport = transport
        transport.set_page_handler(self._receive)

    @abstractmethod
    def _receive(self, page: bytes) -> None:
        """Handle one inbound 8-byte page from the other side."""

    async def start(self) -> None:
        if self._transport is None:
            raise RuntimeError(f"{self.name}: no transport bound")
        await self._transport.open()

    async def stop(self) -> None:
        if self._transport is not None:
            await self._transport.close()

    def _send_ack(self, data: bytes) -> None:
        if self._transport is None:
            raise RuntimeError(f"{self.name}: no transport bound")
        self._transport.send_ack(data)
