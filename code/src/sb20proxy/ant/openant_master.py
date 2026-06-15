"""OpenAntMaster — the real ANT+ radio behind the AntMaster seam.

This is the ONLY place openant is imported on the transmit path, so the software
/ loopback / digital-twin path never needs openant or a stick.

API surface verified against the installed openant 1.3.4 (BIDIRECTIONAL_TRANSMIT,
set_id/set_period/set_rf_freq, send_broadcast_data, on_broadcast_tx_data,
on_acknowledge_data). The RUNTIME behaviour — that on_broadcast_tx_data re-arms
each period, that priming + threaded Node.start() transmit cleanly, and that the
SB20's zero-reset arrives on on_acknowledge_data — is NOT unit-tested; it is the
bench-loopback step (forward-plan.md §2 / NEXT-BIKE-SESSION.md). Treat this as
verified-by-API, pending hardware.
"""

from __future__ import annotations

import threading

from sb20proxy.ant.master import ANTPLUS_NETWORK_KEY, AntMaster, ChannelParams


class OpenAntMaster(AntMaster):
    """Broadcast as an ANT+ Bike Power master on a real stick via openant."""

    def __init__(self, params: ChannelParams) -> None:
        super().__init__()
        self._params = params
        self._node = None
        self._channel = None
        self._thread: threading.Thread | None = None

    async def open(self) -> None:
        # Imported here so the loopback path never pulls in openant.
        from openant.easy.channel import Channel
        from openant.easy.node import Node

        node = Node()
        node.set_network_key(0x00, list(ANTPLUS_NETWORK_KEY))
        channel = node.new_channel(Channel.Type.BIDIRECTIONAL_TRANSMIT)
        channel.set_id(
            self._params.device_number,
            self._params.device_type,
            self._params.transmission_type,
        )
        channel.set_period(self._params.channel_period)
        channel.set_rf_freq(self._params.rf_freq)
        channel.on_broadcast_tx_data = self._on_tx
        channel.on_acknowledge_data = self._on_ack
        channel.open()

        self._node = node
        self._channel = channel
        self._send_next()  # prime the first page

        # Node.start() blocks running the ANT message loop; run it off-thread.
        self._thread = threading.Thread(target=node.start, daemon=True)
        self._thread.start()

    async def close(self) -> None:
        if self._channel is not None:
            try:
                self._channel.close()
            except Exception:
                pass
        if self._node is not None:
            try:
                self._node.stop()
            except Exception:
                pass
        self._node = None
        self._channel = None
        self._thread = None

    def _send_next(self) -> None:
        if self._tx_provider is not None and self._channel is not None:
            self._channel.send_broadcast_data(list(self._tx_provider()))

    def _on_tx(self, _data) -> None:
        # Fired each broadcast period: supply the next page.
        self._send_next()

    def _on_ack(self, data) -> None:
        if self._ack_handler is not None:
            self._ack_handler(bytes(data))
