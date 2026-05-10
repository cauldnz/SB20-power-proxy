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
- For pairing-handshake captures, ensure extended messages are enabled so
  you see ACK traffic from the SB20 to the crank. This script enables them
  by default — if you see only outbound broadcasts and no inbound ACKs,
  check that the openant version supports extended messages (or use the
  pirower fork).
- For two-stick captures (one for L, one for R), run two instances of this
  script with different --usb-stick-index values.

References:
- ANT+ Bike Power profile: D00001086 Rev 5.x (sections 7, 8, 11, 13)
- Project notes: ../03-central-hypothesis-and-phase-zero.md (Phase 0 plan)
"""

from __future__ import annotations

import argparse
import json
import signal
import sys
import time
from pathlib import Path
from typing import Any

# openant imports — will fail clearly if openant isn't installed
try:
    from openant.easy.node import Node
    from openant.easy.channel import Channel
    from openant.devices import ANTPLUS_NETWORK_KEY
except ImportError as e:
    print(f"openant not installed or import failed: {e}", file=sys.stderr)
    print("Run: pip install openant", file=sys.stderr)
    sys.exit(1)


# ANT+ Bike Power profile parameters
DEVICE_TYPE_BIKE_POWER = 0x0B  # 11
RF_FREQ_ANT_PLUS = 57          # 2457 MHz
DEFAULT_CHANNEL_PERIOD = 8182  # 4 Hz (per spec)
DEFAULT_TRANSMISSION_TYPE = 0  # wildcard — accept any

# Page IDs we know how to decode (extend as needed)
PAGE_POWER_ONLY = 0x10
PAGE_WHEEL_TORQUE = 0x11
PAGE_CRANK_TORQUE = 0x12
PAGE_TORQUE_EFFECTIVENESS = 0x13
PAGE_CRANK_TORQUE_FREQUENCY = 0x20
PAGE_MANUFACTURER_INFO = 0x50
PAGE_PRODUCT_INFO = 0x51
PAGE_BATTERY_STATUS = 0x52
PAGE_CALIBRATION = 0x01


def decode_page(data: bytes) -> dict[str, Any]:
    """Decode a Bike Power broadcast page. Returns a dict of decoded fields.

    Always includes 'page' and 'raw_hex'. Other fields depend on the page.
    Unknown pages still produce raw_hex; analysis can be added later.

    Field formulas come from D00001086 ANT+ Bicycle Power Device Profile.
    """
    if len(data) < 8:
        return {"page": None, "raw_hex": data.hex(), "error": "short payload"}

    page = data[0]
    decoded: dict[str, Any] = {
        "page": page,
        "page_hex": f"0x{page:02X}",
        "raw_hex": data.hex(),
    }

    if page == PAGE_POWER_ONLY:
        # Byte 1: event count (rolls 0..255)
        # Byte 2: pedal power (LSB = balance%, MSB bit = differentiation flag)
        # Byte 3: instantaneous cadence (RPM, 0xFF = invalid)
        # Bytes 4-5: accumulated power (LE uint16)
        # Bytes 6-7: instantaneous power (LE uint16, watts)
        decoded.update({
            "event_count": data[1],
            "pedal_power_raw": data[2],
            "pedal_power_balance": data[2] & 0x7F if data[2] != 0xFF else None,
            "pedal_power_differentiation": bool(data[2] & 0x80) if data[2] != 0xFF else None,
            "instantaneous_cadence_rpm": data[3] if data[3] != 0xFF else None,
            "accumulated_power": int.from_bytes(data[4:6], "little"),
            "instantaneous_power_w": int.from_bytes(data[6:8], "little"),
        })

    elif page == PAGE_CRANK_TORQUE:
        # Byte 1: event count
        # Byte 2: crank ticks
        # Byte 3: instantaneous cadence
        # Bytes 4-5: accumulated crank period (1/2048 s units)
        # Bytes 6-7: accumulated torque (1/32 Nm units)
        decoded.update({
            "event_count": data[1],
            "crank_ticks": data[2],
            "instantaneous_cadence_rpm": data[3] if data[3] != 0xFF else None,
            "accumulated_crank_period": int.from_bytes(data[4:6], "little"),
            "accumulated_torque": int.from_bytes(data[6:8], "little"),
        })

    elif page == PAGE_TORQUE_EFFECTIVENESS:
        decoded.update({
            "event_count": data[1],
            "left_te_raw": data[2],
            "right_te_raw": data[3],
            "left_ps_raw": data[4],
            "right_ps_raw": data[5],
        })

    elif page == PAGE_MANUFACTURER_INFO:
        # Byte 1: HW revision
        # Bytes 2-3: manufacturer ID (LE uint16) — Stages? Favero? Other?
        # Bytes 4-5: model number (LE uint16)
        decoded.update({
            "hw_revision": data[3],   # spec says byte 3 (1-indexed in spec, 0-indexed here)
            "manufacturer_id": int.from_bytes(data[4:6], "little"),
            "model_number": int.from_bytes(data[6:8], "little"),
        })

    elif page == PAGE_PRODUCT_INFO:
        # Byte 1: SW revision (supplemental)
        # Byte 2: SW revision (main)
        # Bytes 4-7: serial number (LE uint32)
        decoded.update({
            "sw_revision_supp": data[2],
            "sw_revision_main": data[3],
            "serial_number": int.from_bytes(data[4:8], "little"),
        })

    elif page == PAGE_BATTERY_STATUS:
        decoded.update({
            "battery_id": data[2],
            "operating_time_lsb": int.from_bytes(data[3:6], "little"),
            "battery_voltage_frac": data[6],
            "battery_status_byte": data[7],
        })

    elif page == PAGE_CALIBRATION:
        # Byte 1: calibration ID (0xAC = success, 0xAF = failure, 0xAA = manual zero request)
        # Byte 2: auto-zero status / response sub-id
        # Bytes 6-7: calibration data (offset value for zero-offset)
        decoded.update({
            "calibration_id": data[1],
            "calibration_id_hex": f"0x{data[1]:02X}",
            "auto_zero_status": data[2],
            "calibration_data": int.from_bytes(data[6:8], "little", signed=True),
        })

    # Bit 7 of page is sometimes used as a toggle bit on the first byte; record it.
    decoded["page_toggle_bit"] = bool(page & 0x80)
    decoded["page_no_toggle"] = page & 0x7F

    return decoded


class CaptureRunner:
    """Slave channel capture wrapper.

    Subscribes to the configured device ID/type and writes every received
    message (broadcast and acknowledged) to a JSONL output file.
    """

    def __init__(self, *, device_id: int, output_path: Path,
                 transmission_type: int = DEFAULT_TRANSMISSION_TYPE,
                 channel_period: int = DEFAULT_CHANNEL_PERIOD,
                 device_type: int = DEVICE_TYPE_BIKE_POWER):
        self.device_id = device_id
        self.transmission_type = transmission_type
        self.channel_period = channel_period
        self.device_type = device_type
        self.output_path = output_path
        self.output_path.parent.mkdir(parents=True, exist_ok=True)
        self._fp = open(self.output_path, "w", buffering=1)  # line-buffered
        self._node: Node | None = None
        self._channel: Channel | None = None
        self._t0 = time.monotonic()
        self._messages_logged = 0

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

    def _on_event(self, data: Any) -> None:
        """Channel event callback. Useful for tracking RX_FAIL, channel closes, etc."""
        self._log("channel_event", event=str(data))

    def _on_acknowledged(self, data: bytes) -> None:
        """Acknowledged-data callback. Critical for capturing pairing/calibration ACKs."""
        decoded = decode_page(bytes(data))
        self._log("acknowledged", data=decoded)

    def setup(self) -> None:
        self._log("session_start", device_id=self.device_id,
                  device_type=self.device_type,
                  transmission_type=self.transmission_type,
                  channel_period=self.channel_period,
                  rf_freq=RF_FREQ_ANT_PLUS,
                  output=str(self.output_path))

        self._node = Node()
        self._node.set_network_key(0x00, ANTPLUS_NETWORK_KEY)

        # Slave channel — RX-only by default in openant.easy.Channel.
        # We want to also see ACK traffic; openant may need extended messages
        # enabled at the node level for this. Document this limitation in the
        # output if it's not visible.
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
        self._channel.open()

        self._log("channel_open", note="waiting for broadcast")

    def run(self, duration_s: float) -> None:
        if self._node is None:
            raise RuntimeError("setup() not called")
        try:
            # openant's Node.start() blocks; we set up a deadline via signal.
            def _alarm(signum, frame):  # noqa: ARG001
                self._log("duration_reached", duration_s=duration_s)
                self.stop()

            signal.signal(signal.SIGALRM, _alarm)
            signal.alarm(int(duration_s))
            self._node.start()
        except KeyboardInterrupt:
            self._log("interrupted", reason="ctrl-c")
        finally:
            self.stop()

    def stop(self) -> None:
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
    args = p.parse_args()

    print(f"Starting capture: device {args.device_id}, {args.duration:.0f}s, → {args.output}")
    print("Wake the meter (rotate cranks) if no broadcasts appear within ~30s.")

    runner = CaptureRunner(
        device_id=args.device_id,
        output_path=args.output,
        transmission_type=args.transmission_type,
        channel_period=args.channel_period,
    )
    runner.setup()
    runner.run(args.duration)
    print(f"Done. {runner._messages_logged} records written to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
