"""Transports — the seam that lets one twin run in three places.

A DeviceTwin's logic is transport-agnostic. The SAME twin can run:
- over a `LoopbackTransport`  → pure software, in CI (no hardware);
- over an `AntSlaveTransport` → a real ANT+ stick, for an on-air loopback against
  our own transmitter on a second stick, OR as one side of an interaction with a
  real device (a real power meter / the SB20) on the other.

A transport delivers inbound pages to the twin (`set_page_handler`) and carries the
twin's acknowledged data outbound (`send_ack`, e.g. a zero-reset request).
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from collections.abc import Callable

from sb20proxy.ant.master import ChannelParams

PageHandler = Callable[[bytes], None]


class TwinTransport(ABC):
    """Connects a DeviceTwin to whatever is on the other side."""

    def __init__(self) -> None:
        self._on_page: PageHandler | None = None

    def set_page_handler(self, handler: PageHandler) -> None:
        self._on_page = handler

    def _deliver(self, page: bytes) -> None:
        if self._on_page is not None:
            self._on_page(bytes(page))

    @abstractmethod
    async def open(self) -> None:
        """Begin receiving (subscribe to the loopback / open the radio)."""

    @abstractmethod
    async def close(self) -> None:
        """Stop receiving and release resources."""

    @abstractmethod
    def send_ack(self, data: bytes) -> None:
        """Send acknowledged data to the other side (e.g. a zero-reset request)."""


class LoopbackTransport(TwinTransport):
    """In-process: receive a LoopbackMaster's broadcasts; inject acks back up. Pure
    software — the CI / digital-twin path. No openant, no hardware."""

    def __init__(self, master) -> None:
        super().__init__()
        self._master = master

    async def open(self) -> None:
        self._master.connect(self._deliver)

    async def close(self) -> None:
        pass

    def send_ack(self, data: bytes) -> None:
        self._master.inject_ack(data)


class AntSlaveTransport(TwinTransport):
    """Real ANT+ slave channel on a stick: receive a master's broadcasts and send
    acknowledged data up.

    Hardware — needs an ANT+ stick; not unit-tested. Pairs with `OpenAntMaster` on a
    second stick (on-air loopback) or with a real power meter / the SB20 (twin vs
    real device). openant is imported lazily so the software path never needs it.
    """

    def __init__(
        self, params: ChannelParams, *, search_timeout: int = 0xFF, usb_device=None
    ) -> None:
        super().__init__()
        self._params = params
        self._search_timeout = search_timeout
        self._usb_device = usb_device  # pin a specific stick (multi-stick hosts)
        self._node = None
        self._channel = None
        self._thread = None

    async def open(self) -> None:
        import threading

        from openant.easy.channel import Channel
        from openant.easy.node import Node

        from sb20proxy.ant.master import ANTPLUS_NETWORK_KEY
        from sb20proxy.ant.usb_select import pinned_stick

        with pinned_stick(self._usb_device):
            node = Node()  # openant claims the USB device synchronously here
        node.set_network_key(0x00, list(ANTPLUS_NETWORK_KEY))
        channel = node.new_channel(Channel.Type.BIDIRECTIONAL_RECEIVE)
        channel.set_id(
            self._params.device_number,
            self._params.device_type,
            self._params.transmission_type,
        )
        channel.set_period(self._params.channel_period)
        channel.set_rf_freq(self._params.rf_freq)
        channel.set_search_timeout(self._search_timeout)
        channel.on_broadcast_data = lambda data: self._deliver(bytes(data))
        channel.on_burst_data = lambda data: self._deliver(bytes(data))
        channel.open()

        self._node = node
        self._channel = channel
        self._thread = threading.Thread(target=node.start, daemon=True)
        self._thread.start()

    async def close(self) -> None:
        import threading

        if self._channel is not None:
            try:
                self._channel.close()
            except Exception:
                pass
        if self._node is not None:
            # node.stop()'s join() can block forever if the chip wedged; bound it.
            # The CLI then os._exit()s past any non-daemon openant thread left behind.
            stopper = threading.Thread(target=self._node.stop, daemon=True)
            stopper.start()
            stopper.join(timeout=2.0)
        self._node = None
        self._channel = None
        self._thread = None

    def send_ack(self, data: bytes) -> None:
        if self._channel is not None:
            self._channel.send_acknowledged_data(list(data))
