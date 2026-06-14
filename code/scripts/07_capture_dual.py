#!/usr/bin/env python3
"""Phase 0 dual-meter capture — two ANT+ Bike Power devices on ONE stick.

Opens two slave channels on a single openant Node and logs BOTH meters to one
JSONL with each record tagged by `source` ("stages" / "assioma" / a label you
pass). Both are timestamped by the same machine clock, so the two streams are
sample-aligned with no skew — ideal for the meter-vs-meter calibration model
(see code/findings/decisions.md, calibration-model entry). This also is the
proxy's eventual input path: "listen to the Assioma" is exactly channel 1 here.

Why one stick is enough: each meter broadcasts at 4 Hz on 2457 MHz; the ANTUSB2
time-shares the radio across up to 8 channels. Two 4 Hz devices is light load.

Usage (run via run_capture-style detached launch on ride day — see runbook):
    python 07_capture_dual.py \
        --stages-id 62144 --assioma-id 17039 --duration 720 \
        --output ../findings/captures/CAL-dual-20260614-1830.jsonl

Each non-stages source can be relabelled with --assioma-label if you point it at
something other than an Assioma.

References:
- ANT+ Bike Power profile D00001086; openant easy.Node multi-channel dispatch.
- Decode logic is imported from 01_capture_stages.py (single source of truth).
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import sys
import time
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
from typing import Any

try:
    from openant.easy.node import Node
    from openant.easy.channel import Channel
    from openant.devices import ANTPLUS_NETWORK_KEY
except ImportError as e:
    print(f"openant not installed or import failed: {e}", file=sys.stderr)
    print("Run: pip install openant", file=sys.stderr)
    sys.exit(1)

# Reuse decode_page from 01_capture_stages.py — one source of truth for the
# byte layouts (so a fix there applies here too).
_spec = spec_from_file_location(
    "capture_stages", Path(__file__).parent / "01_capture_stages.py")
assert _spec and _spec.loader
_cap = module_from_spec(_spec)
sys.modules["capture_stages"] = _cap  # register before exec (dataclass/3.12 safe)
_spec.loader.exec_module(_cap)
decode_page = _cap.decode_page

DEVICE_TYPE_BIKE_POWER = 0x0B
RF_FREQ_ANT_PLUS = 57
DEFAULT_CHANNEL_PERIOD = 8182


class DualCaptureRunner:
    """Open one slave channel per source on a single Node; log both to one file."""

    def __init__(self, *, sources: list[tuple[str, int]], output_path: Path,
                 channel_period: int = DEFAULT_CHANNEL_PERIOD):
        # sources: list of (label, device_id), e.g. [("stages",62144),("assioma",17039)]
        self.sources = sources
        self.channel_period = channel_period
        self.output_path = output_path
        self.output_path.parent.mkdir(parents=True, exist_ok=True)
        self._fp = open(self.output_path, "w", buffering=1)
        self._node: Node | None = None
        self._channels: list[Any] = []
        self._t0 = time.monotonic()
        self._counts: dict[str, int] = {label: 0 for label, _ in sources}
        self._messages_logged = 0
        self._stopped = False

    def _log(self, kind: str, **fields: Any) -> None:
        record = {
            "iso_time": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime()),
            "monotonic_s": round(time.monotonic() - self._t0, 6),
            "kind": kind,
            **fields,
        }
        self._fp.write(json.dumps(record) + "\n")
        self._messages_logged += 1

    def _make_handler(self, label: str, kind: str):
        def handler(data: bytes) -> None:
            decoded = decode_page(bytes(data))
            self._counts[label] += 1
            self._log(kind, source=label, data=decoded)
        return handler

    def setup(self) -> None:
        self._log("session_start", protocol="ant+dual",
                  sources=[{"label": l, "device_id": d} for l, d in self.sources],
                  device_type=DEVICE_TYPE_BIKE_POWER,
                  channel_period=self.channel_period, rf_freq=RF_FREQ_ANT_PLUS,
                  output=str(self.output_path))

        self._node = Node()
        self._node.set_network_key(0x00, ANTPLUS_NETWORK_KEY)

        for label, device_id in self.sources:
            ch = self._node.new_channel(Channel.Type.BIDIRECTIONAL_RECEIVE)
            ch.on_broadcast_data = self._make_handler(label, "broadcast")
            ch.on_burst_data = self._make_handler(label, "broadcast")
            ch.on_acknowledge_data = self._make_handler(label, "acknowledged")
            ch.set_id(device_id, DEVICE_TYPE_BIKE_POWER, 0)
            ch.set_period(self.channel_period)
            ch.set_rf_freq(RF_FREQ_ANT_PLUS)
            ch.set_search_timeout(0xFF)
            try:
                ch.enable_extended_messages(1)
            except Exception as e:
                self._log("ext_messages", source=label, enabled=False, error=str(e))
            ch.open()
            self._channels.append(ch)
            self._log("channel_open", source=label, device_id=device_id)

    def run(self, duration_s: float) -> None:
        if self._node is None:
            raise RuntimeError("setup() not called")
        try:
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
        if self._stopped:
            return
        self._stopped = True
        for ch in self._channels:
            try:
                ch.close()
            except Exception as e:
                self._log("close_error", error=str(e))
        try:
            if self._node is not None:
                self._node.stop()
        except Exception as e:
            self._log("node_stop_error", error=str(e))
        self._log("session_end", messages_logged=self._messages_logged,
                  counts=self._counts)
        self._fp.close()


def main() -> int:
    p = argparse.ArgumentParser(description="Dual-meter ANT+ capture (one stick, two channels)")
    p.add_argument("--stages-id", type=int, required=True, help="Stages crank ANT+ device number")
    p.add_argument("--assioma-id", type=int, required=True, help="Assioma (or 2nd meter) ANT+ device number")
    p.add_argument("--assioma-label", default="assioma", help="Label for the second source (default 'assioma')")
    p.add_argument("--duration", type=float, default=720.0, help="Capture seconds (default 720 = 12 min)")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--channel-period", type=int, default=DEFAULT_CHANNEL_PERIOD)
    args = p.parse_args()

    sources = [("stages", args.stages_id), (args.assioma_label, args.assioma_id)]
    print(f"Dual capture: {sources}, {args.duration:.0f}s -> {args.output}")
    print("Wake BOTH meters (pedal) if no broadcasts appear within ~30s.")

    runner = DualCaptureRunner(sources=sources, output_path=args.output,
                               channel_period=args.channel_period)
    try:
        runner.setup()
    except Exception as e:
        # Same hard-won lesson as 01_capture_stages.py: never hang holding the
        # stick on a setup failure (see code/findings/wsl-capture-runbook.md).
        print(f"setup failed: {e}", file=sys.stderr)
        try:
            runner._log("setup_error", error=str(e))
            runner._fp.flush()
        except Exception:
            pass
        os._exit(2)
    runner.run(args.duration)
    print(f"Done. {runner._messages_logged} records "
          f"({runner._counts}) -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
