#!/usr/bin/env python3
"""Phase 0 capture script — Stages crank ANT+ traffic.

Subscribes to a Stages crank (or any standard ANT+ Bike Power device) as a
slave, logs every received message to a JSONL file with both raw bytes and
decoded fields where the page is recognised.

This is a Phase 0 diagnostic tool. It is intentionally verbose: every message
is logged regardless of whether it changed. Future analysis depends on this
being lossless.

Usage:
    python 01_capture_stages.py --device-id 12345 --duration 900 \\
        --output ../findings/captures/A-stagesL-steady-20260510-1830.jsonl

Notes:
- Wake the crank before starting (rotate it) or capture will sit idle.
- This script enables ANT+ extended RX messages (0x66) so every captured
  packet is stamped with its source device number (recorded as
  ext_device_number in the decoded data). That helps disambiguate meters in
  multi-device sessions. NOTE: extended messages do NOT let a passive slave
  see the SB20->crank acknowledged *request* during pairing/zero-reset — a
  slave cannot sniff another slave's uplink to the master. What you CAN
  capture is the crank's calibration *response*, which the Bike Power profile
  sends as an interleaved broadcast page 0x01 (logged as kind="broadcast").
  Run the Session C-0 dry run to confirm that page 0x01 appears on zero-reset
  before relying on Session C. The on_acknowledge_data hook is wired anyway as
  cheap insurance for any ACK the master happens to direct at us.
- For two-stick captures (one for L, one for R), run two instances of this
  script with different --usb-stick-index values.

References:
- ANT+ Bike Power profile: D00001086 Rev 5.x (sections 7, 8, 11, 13)
- Project notes: ../03-central-hypothesis-and-phase-zero.md (Phase 0 plan)
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import sys
import time
from pathlib import Path
from typing import Any

# The ANT+ page codec is the package's, not this script's. `ant/pages.py` used to say "mirrored
# verbatim from code/scripts/01_capture_stages.py ... a follow-up should make the capture script
# import this one" — this is that follow-up. The package copy is the tested one
# (tests/test_ant_pages.py round-trips it against the committed Phase 0 captures).
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from sb20proxy.ant.pages import (  # noqa: E402
    PAGE_BATTERY_STATUS,
    PAGE_CALIBRATION,
    PAGE_CRANK_TORQUE,
    PAGE_MANUFACTURER_INFO,
    PAGE_POWER_ONLY,
    PAGE_PRODUCT_INFO,
    PAGE_TORQUE_EFFECTIVENESS,
)
from sb20proxy.ant.pages import decode_page as _decode_page  # noqa: E402

__all__ = [
    "PAGE_BATTERY_STATUS", "PAGE_CALIBRATION", "PAGE_CRANK_TORQUE", "PAGE_CRANK_TORQUE_FREQUENCY",
    "PAGE_MANUFACTURER_INFO", "PAGE_POWER_ONLY", "PAGE_PRODUCT_INFO",
    "PAGE_TORQUE_EFFECTIVENESS", "PAGE_WHEEL_TORQUE", "CaptureRunner", "decode_page", "main",
]

# openant imports — will fail clearly if openant isn't installed
try:
    from openant.easy.node import Node
    from openant.easy.channel import Channel
    from openant.devices import ANTPLUS_NETWORK_KEY
    from openant.base.message import Message
except ImportError as e:  # pragma: no cover - dependency guard
    _msg = f"openant not installed or import failed: {e}\nRun: pip install openant"
    if __name__ == "__main__":
        print(_msg, file=sys.stderr)
        sys.exit(1)
    raise ImportError(_msg) from e  # importable: let callers skip, don't kill them


# ANT+ Bike Power profile parameters
DEVICE_TYPE_BIKE_POWER = 0x0B  # 11
RF_FREQ_ANT_PLUS = 57          # 2457 MHz
DEFAULT_CHANNEL_PERIOD = 8182  # 4 Hz (per spec)
DEFAULT_TRANSMISSION_TYPE = 0  # wildcard — accept any

# Page IDs — the seven the codec decodes come from the package, so there is exactly one
# definition of each. (0x11 and 0x20 are not decoded by either side; they exist here only to
# name the pages this script may see on the wire.)
PAGE_WHEEL_TORQUE = 0x11
PAGE_CRANK_TORQUE_FREQUENCY = 0x20


# decode_page lives in the sb20proxy package (sb20proxy.ant.pages) — imported here rather than
# duplicated. The two copies were byte-identical apart from a docstring, and the package version
# is the one covered by tests/test_ant_pages.py against the committed Phase 0 captures.
def decode_page(data: bytes) -> dict[str, Any]:
    """Decode a Bike Power broadcast page — see :func:`sb20proxy.ant.pages.decode_page`.

    Kept as a thin wrapper so this script keeps its standalone entry-point shape and any
    existing caller of ``01_capture_stages.decode_page`` still works.
    """
    return _decode_page(data)


class CaptureRunner:
    """Slave channel capture wrapper.

    Subscribes to the configured device ID/type and writes every received
    message (broadcast and acknowledged) to a JSONL output file.
    """

    # Data event codes are dispatched to the per-channel data callbacks already;
    # everything else (search timeout, RX fail, channel closed, collision, queue
    # overflow, ...) is a "true" channel event worth recording.
    _DATA_EVENT_CODES = frozenset({
        Message.Code.EVENT_TX,
        Message.Code.EVENT_RX_BROADCAST,
        Message.Code.EVENT_RX_ACKNOWLEDGED,
        Message.Code.EVENT_RX_BURST_PACKET,
    })

    def __init__(self, *, device_id: int, output_path: Path,
                 transmission_type: int = DEFAULT_TRANSMISSION_TYPE,
                 channel_period: int = DEFAULT_CHANNEL_PERIOD,
                 device_type: int = DEVICE_TYPE_BIKE_POWER,
                 log_channel_events: bool = False):
        self.device_id = device_id
        self.transmission_type = transmission_type
        self.channel_period = channel_period
        self.device_type = device_type
        self.log_channel_events = log_channel_events
        self.output_path = output_path
        self.output_path.parent.mkdir(parents=True, exist_ok=True)
        self._fp = open(self.output_path, "w", buffering=1)  # line-buffered
        self._node: Node | None = None
        self._channel: Channel | None = None
        self._t0 = time.monotonic()
        self._messages_logged = 0
        self._stopped = False

    def _log(self, kind: str, **fields: Any) -> None:
        """Write one JSONL row. Always includes timestamp and uptime."""
        record = {
            "iso_time": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime()),
            "monotonic_s": round(time.monotonic() - self._t0, 6),
            "kind": kind,
            **fields,
        }
        self._fp.write(json.dumps(record) + "\n")
        self._messages_logged += 1

    def _on_data(self, data: bytes) -> None:
        """Broadcast data callback from openant."""
        decoded = decode_page(bytes(data))
        self._log("broadcast", data=decoded)
        self._on_decoded("broadcast", decoded)

    def _on_acknowledged(self, data: bytes) -> None:
        """Acknowledged-data callback. Critical for capturing pairing/calibration ACKs."""
        decoded = decode_page(bytes(data))
        self._log("acknowledged", data=decoded)
        self._on_decoded("acknowledged", decoded)

    def _on_decoded(self, kind: str, decoded: dict[str, Any]) -> None:
        """Hook for subclasses that need each decoded page. No-op by default.

        Exists so a subclass never has to re-implement `_on_data` just to observe traffic.
        `ride_wizard.py` did exactly that, and its copy of the decode-and-log body then had
        to be kept in step with this one by hand - on the ride-day path, where a silently
        skipped log line is expensive. Overriding this instead makes it impossible to drop
        the logging that makes the capture canonical.
        """

    def setup(self) -> None:
        self._log("session_start", device_id=self.device_id,
                  device_type=self.device_type,
                  transmission_type=self.transmission_type,
                  channel_period=self.channel_period,
                  rf_freq=RF_FREQ_ANT_PLUS,
                  output=str(self.output_path))

        self._node = Node()
        self._node.set_network_key(0x00, ANTPLUS_NETWORK_KEY)

        # Slave (BIDIRECTIONAL_RECEIVE) channel. on_acknowledge_data is the
        # correct openant hook for received acknowledged data (node._main
        # dispatches to it); we wire it as insurance, though the Stages
        # calibration response actually arrives as a broadcast page 0x01.
        # Extended messages (enabled below) stamp each packet with its source
        # device ID but do not expose the SB20's slave-to-master uplink.
        self._channel = self._node.new_channel(Channel.Type.BIDIRECTIONAL_RECEIVE)
        self._channel.on_broadcast_data = self._on_data
        self._channel.on_burst_data = self._on_data
        self._channel.on_acknowledge_data = self._on_acknowledged

        self._channel.set_id(
            self.device_id,
            self.device_type,
            self.transmission_type,
        )
        self._channel.set_period(self.channel_period)
        self._channel.set_rf_freq(RF_FREQ_ANT_PLUS)
        # Search timeout: allow plenty of time to find the device on first run.
        self._channel.set_search_timeout(0xFF)  # infinite

        # Enable extended RX messages (0x66 ENABLE_EXT_RX_MESGS). This appends
        # the source channel ID (flag + device number + device type + trans
        # type) to every received payload, so we can record WHICH meter each
        # packet came from (decode_page parses the tail into ext_* fields). It
        # is safe for decode_page() — the extra bytes are appended after the 8
        # data bytes. IMPORTANT: extended messages do NOT make the SB20->crank
        # acknowledged *request* visible — a passive slave cannot sniff another
        # slave's uplink to the master. The Stages calibration *response*
        # arrives as a broadcast page 0x01, which we do capture.
        try:
            self._channel.enable_extended_messages(1)
            self._log("ext_messages", enabled=True)
        except Exception as e:
            # Not fatal: capture still works, we just won't get source-ID tails.
            self._log("ext_messages", enabled=False, error=str(e))

        self._channel.open()

        self._log("channel_open", note="waiting for broadcast")

    def _install_channel_event_tap(self) -> None:
        """Wrap the node's channel-event handler to log non-data events.

        Reaches into openant internals (node.ant.channel_event_function), so it
        is best-effort and gated behind --log-channel-events. By the time run()
        calls this, the openant worker thread has already installed the node's
        own handler (setup() round-trips several config messages through it), so
        we capture that handler and chain through it.
        """
        if self._node is None:
            return
        inner = self._node.ant.channel_event_function

        def _tap(channel: int, event: int, data: Any) -> Any:
            if event not in self._DATA_EVENT_CODES:
                try:
                    self._log("channel_event", event=str(event),
                              event_code=event, channel=channel)
                except Exception:
                    pass
            return inner(channel, event, data)

        self._node.ant.channel_event_function = _tap

    def run(self, duration_s: float) -> None:
        if self._node is None:
            raise RuntimeError("setup() not called")
        try:
            # openant's Node.start() blocks; we set up a deadline via signal.
            # NOTE: signal.SIGALRM / signal.alarm() are Unix-only. This is fine
            # under WSL2 / Linux / macOS (the supported capture environment).
            # On native Windows, SIGALRM does not exist and this raises
            # AttributeError — run captures inside WSL, not native Windows.
            # (A threading.Timer fallback could be added if native-Windows
            # capture is ever needed; not worth the complexity for Phase 0.)
            def _alarm(signum, frame):  # noqa: ARG001
                self._log("duration_reached", duration_s=duration_s)
                self.stop()

            signal.signal(signal.SIGALRM, _alarm)
            signal.alarm(int(duration_s))

            # Optionally tee non-data channel events (RX_FAIL, search timeout,
            # channel closed, collisions) into the JSONL. These are invisible to
            # the per-channel data callbacks but are diagnostic gold for Session
            # C/F, where a failed pairing shows up here rather than in the data
            # stream. Installed now — after setup() has driven the worker thread,
            # so node.ant.channel_event_function is already the node's handler —
            # and chained through it so normal data flow is preserved.
            if self.log_channel_events:
                self._install_channel_event_tap()

            self._node.start()
        except KeyboardInterrupt:
            self._log("interrupted", reason="ctrl-c")
        finally:
            self.stop()

    def stop(self) -> None:
        # Idempotent: stop() runs both from the SIGALRM handler (duration
        # reached) and from run()'s finally block. Without this guard the
        # second call would try to close an already-stopped driver and then
        # log the failure to an already-closed file — crashing with
        # "I/O operation on closed file" at the end of every duration-limited
        # capture, after the data was safely written.
        if self._stopped:
            return
        self._stopped = True
        try:
            if self._channel is not None:
                self._channel.close()
        except Exception as e:
            self._log("close_error", error=str(e))
        try:
            if self._node is not None:
                self._node.stop()
        except Exception as e:
            self._log("node_stop_error", error=str(e))
        self._log("session_end", messages_logged=self._messages_logged)
        self._fp.close()


def main() -> int:
    p = argparse.ArgumentParser(description="Phase 0 ANT+ Bike Power capture")
    p.add_argument("--device-id", type=int, required=True,
                   help="ANT+ device number to subscribe to (the 5-digit ID on the meter sticker)")
    p.add_argument("--duration", type=float, default=900.0,
                   help="Capture duration in seconds (default 900 = 15 min)")
    p.add_argument("--output", type=Path, required=True,
                   help="Output JSONL path")
    p.add_argument("--transmission-type", type=int, default=DEFAULT_TRANSMISSION_TYPE,
                   help="Transmission type byte (default 0 = wildcard)")
    p.add_argument("--channel-period", type=int, default=DEFAULT_CHANNEL_PERIOD,
                   help="Channel period in 1/32768 s units (default 8182 = 4 Hz)")
    p.add_argument("--log-channel-events", action="store_true",
                   help="Also log non-data channel events (RX_FAIL, search "
                        "timeout, channel closed, collisions). Useful for "
                        "diagnosing pairing failures in Sessions C/F. Off by "
                        "default; reaches into openant internals.")
    args = p.parse_args()

    print(f"Starting capture: device {args.device_id}, {args.duration:.0f}s, → {args.output}")
    print("Wake the meter (rotate cranks) if no broadcasts appear within ~30s.")

    runner = CaptureRunner(
        device_id=args.device_id,
        output_path=args.output,
        transmission_type=args.transmission_type,
        channel_period=args.channel_period,
        log_channel_events=args.log_channel_events,
    )
    try:
        runner.setup()
    except Exception as e:
        # CRITICAL (learned the hard way, 2026-06-14 — see
        # code/findings/wsl-capture-runbook.md): if setup() fails *after*
        # openant's Node() has started its background worker thread (e.g. the
        # ANT chip returns CHANNEL_IN_WRONG_STATE at channel assign), a normal
        # exception does NOT end the process — that non-daemon thread keeps
        # running and the process HANGS, still holding the ANT stick's USB
        # handle open. Every subsequent capture then dies with "Resource busy"
        # until someone hunts down and kills the zombie. Force-exit instead so
        # the kernel releases the USB device immediately; the next launch (with
        # a fresh Node + reset_system) can then succeed, optionally after a
        # retry for the transient wrong-state.
        print(f"setup failed: {e}", file=sys.stderr)
        try:
            runner._log("setup_error", error=str(e))
            runner._fp.flush()
        except Exception:
            pass
        os._exit(2)
    runner.run(args.duration)
    print(f"Done. {runner._messages_logged} records written to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
