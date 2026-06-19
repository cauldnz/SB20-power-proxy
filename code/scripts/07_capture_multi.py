#!/usr/bin/env python3
"""Phase 0 multi-source capture — several ANT+ devices on ONE stick, one clock.

Opens one slave channel per source on a single openant Node and logs them all to
one JSONL, each record tagged by `source`. Because everything shares the same
machine clock, the streams are sample-aligned with no skew — ideal for:
  - the meter-vs-meter calibration grid (Stages crank vs Assioma), and
  - open-question #7 (does the SB20 rescale crank power?) by capturing the bike's
    own FE-C power output alongside the crank.

Supports two device-profile decoders:
  - Bike Power (device type 0x0B): pages 0x10/0x12/0x13/0x50/0x51/0x52/0x01
    (decode reused from 01_capture_stages.py — single source of truth).
  - Fitness Equipment Control / FE-C (device type 0x11): the bike-as-trainer.
    We decode page 0x19 (Specific Trainer Data) for instantaneous power+cadence,
    and page 0x10 (General FE Data). Power layout verified against openant's own
    devices/fitness_equipment.py: power = data[5] | ((data[6] & 0x0F) << 8).

This is also the proxy's input path: "listen to the Assioma" is just one source.

Usage (run detached on ride day — see code/findings/wsl-capture-runbook.md):
    # meter-to-meter calibration (generic — any two Bike Power meters):
    python 07_capture_multi.py \
        --meter xcadey:12345 --meter assioma:17039 \
        --duration 1500 \
        --output ../findings/captures/CAL-xcadey-vs-assioma-20260619.jsonl

    # legacy form (Stages crank vs Assioma + the bike's FE-C output) still works:
    python 07_capture_multi.py \
        --stages-id 62144 --assioma-id 17039 --fec-id 0 \
        --duration 1500 \
        --output ../findings/captures/CAL-multi-20260615-0700.jsonl

  --fec-id 0  => wildcard (lock onto whatever FE-C trainer is in range; its real
                 device number is recorded in ext_device_number). Omit --fec-id
                 to skip the bike-output source entirely.
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
from typing import Any, Callable

try:
    from openant.easy.node import Node
    from openant.easy.channel import Channel
    from openant.devices import ANTPLUS_NETWORK_KEY
except ImportError as e:
    print(f"openant not installed or import failed: {e}", file=sys.stderr)
    print("Run: pip install openant", file=sys.stderr)
    sys.exit(1)

# Reuse Bike Power decode_page from 01 (one source of truth for byte layouts).
_spec = spec_from_file_location(
    "capture_stages", Path(__file__).parent / "01_capture_stages.py")
assert _spec and _spec.loader
_cap = module_from_spec(_spec)
sys.modules["capture_stages"] = _cap
_spec.loader.exec_module(_cap)
decode_page = _cap.decode_page

DEVTYPE_BIKE_POWER = 0x0B
DEVTYPE_FEC = 0x11
RF_FREQ_ANT_PLUS = 57
PERIOD_BIKE_POWER = 8182
PERIOD_FEC = 8192  # FE-C standard channel period (~4 Hz)


def parse_meter_spec(spec: str) -> tuple[str, int]:
    """Parse a ``--meter LABEL:ANT_DEVICE_NUMBER`` argument -> ``(label, device_id)``.

    Used by the generic meter-to-meter calibration capture (e.g. ``--meter xcadey:12345
    --meter assioma:17039``). Raises ``ValueError`` on a malformed spec so the CLI can
    fail fast rather than hold the stick.
    """
    label, sep, num = spec.partition(":")
    label, num = label.strip(), num.strip()
    if not sep or not label or not num:
        raise ValueError(f"--meter must be LABEL:ANTID (e.g. xcadey:12345), got {spec!r}")
    return label, int(num)  # int() raises ValueError on a non-numeric id


def decode_fec(data: bytes) -> dict[str, Any]:
    """Decode an ANT+ FE-C page. Power layout per openant fitness_equipment.py."""
    if len(data) < 8:
        return {"page": None, "raw_hex": data.hex(), "error": "short payload"}
    page = data[0]
    out: dict[str, Any] = {"page": page, "page_hex": f"0x{page:02X}",
                           "raw_hex": data.hex(), "page_no_toggle": page & 0x7F}
    pm = page & 0x7F
    if pm == 0x19:  # Specific Trainer Data — the instantaneous power page
        power = data[5] | ((data[6] & 0x0F) << 8)
        out.update({
            "event_count": data[1],
            "instantaneous_cadence_rpm": data[2] if data[2] != 0xFF else None,
            "accumulated_power": int.from_bytes(data[3:5], "little"),
            "instantaneous_power_w": None if power == 0x0FFF else power,
            "trainer_status_bits": (data[6] >> 4) & 0x0F,
            "fe_state": (data[7] >> 4) & 0x07,
            "flags": data[7] & 0x0F,
        })
    elif pm == 0x10:  # General FE Data
        speed = int.from_bytes(data[4:6], "little")
        out.update({
            "equipment_type": data[1] & 0x1F,
            "elapsed_time_quarter_s": data[2],
            "distance_m": data[3],
            "speed_mm_s": None if speed == 0xFFFF else speed,
            "fe_state": (data[7] >> 4) & 0x07,
        })
    # Extended-message tail (source channel id), same layout as bike power.
    if len(data) >= 13:
        out["ext_flag"] = data[8]
        out["ext_device_number"] = int.from_bytes(data[9:11], "little")
        out["ext_device_type"] = data[11]
        out["ext_transmission_type"] = data[12]
    return out


class Source:
    def __init__(self, label: str, device_id: int, device_type: int,
                 period: int, decoder: Callable[[bytes], dict]):
        self.label = label
        self.device_id = device_id
        self.device_type = device_type
        self.period = period
        self.decoder = decoder


class MultiCaptureRunner:
    def __init__(self, *, sources: list[Source], output_path: Path):
        self.sources = sources
        self.output_path = output_path
        self.output_path.parent.mkdir(parents=True, exist_ok=True)
        self._fp = open(self.output_path, "w", buffering=1)
        self._node: Node | None = None
        self._channels: list[Any] = []
        self._t0 = time.monotonic()
        self._counts: dict[str, int] = {s.label: 0 for s in sources}
        self._messages_logged = 0
        self._stopped = False

    def _log(self, kind: str, **fields: Any) -> None:
        rec = {"iso_time": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime()),
               "monotonic_s": round(time.monotonic() - self._t0, 6),
               "kind": kind, **fields}
        self._fp.write(json.dumps(rec) + "\n")
        self._messages_logged += 1

    def _handler(self, src: Source, kind: str):
        def h(data: bytes) -> None:
            self._counts[src.label] += 1
            self._log(kind, source=src.label, data=src.decoder(bytes(data)))
        return h

    def setup(self) -> None:
        self._log("session_start", protocol="ant+multi",
                  sources=[{"label": s.label, "device_id": s.device_id,
                            "device_type": s.device_type, "period": s.period}
                           for s in self.sources],
                  rf_freq=RF_FREQ_ANT_PLUS, output=str(self.output_path))
        self._node = Node()
        self._node.set_network_key(0x00, ANTPLUS_NETWORK_KEY)
        for s in self.sources:
            ch = self._node.new_channel(Channel.Type.BIDIRECTIONAL_RECEIVE)
            ch.on_broadcast_data = self._handler(s, "broadcast")
            ch.on_burst_data = self._handler(s, "broadcast")
            ch.on_acknowledge_data = self._handler(s, "acknowledged")
            ch.set_id(s.device_id, s.device_type, 0)  # trans type 0 = wildcard
            ch.set_period(s.period)
            ch.set_rf_freq(RF_FREQ_ANT_PLUS)
            ch.set_search_timeout(0xFF)
            try:
                ch.enable_extended_messages(1)
            except Exception as e:
                self._log("ext_messages", source=s.label, enabled=False, error=str(e))
            ch.open()
            self._channels.append(ch)
            self._log("channel_open", source=s.label, device_id=s.device_id,
                      device_type=s.device_type)

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
    p = argparse.ArgumentParser(description="Multi-source ANT+ capture (one stick)")
    p.add_argument("--meter", action="append", default=[], metavar="LABEL:ANTID",
                   help="A Bike Power meter as LABEL:ANT_DEVICE_NUMBER (repeatable). For the "
                        "meter-to-meter calibration: --meter xcadey:12345 --meter assioma:17039")
    p.add_argument("--stages-id", type=int, default=None,
                   help="(legacy) Stages crank device number")
    p.add_argument("--assioma-id", type=int, default=None,
                   help="(legacy) Assioma (2nd meter) device number")
    p.add_argument("--assioma-label", default="assioma")
    p.add_argument("--fec-id", type=int, default=None,
                   help="Bike FE-C device number for the #7 check; 0 = wildcard. "
                        "Omit to skip the bike-output source.")
    p.add_argument("--duration", type=float, default=1500.0, help="Seconds (default 1500 = 25 min)")
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()

    sources: list[Source] = []
    # Legacy explicit flags (back-compat with existing run-sheets).
    if args.stages_id is not None:
        sources.append(Source("stages", args.stages_id,
                              DEVTYPE_BIKE_POWER, PERIOD_BIKE_POWER, decode_page))
    if args.assioma_id is not None:
        sources.append(Source(args.assioma_label, args.assioma_id,
                              DEVTYPE_BIKE_POWER, PERIOD_BIKE_POWER, decode_page))
    # Generic meters (any label:id pair) — the path for XCadey vs Assioma etc.
    for spec in args.meter:
        try:
            label, dev = parse_meter_spec(spec)
        except ValueError as e:
            print(str(e), file=sys.stderr)
            return 2
        sources.append(Source(label, dev, DEVTYPE_BIKE_POWER, PERIOD_BIKE_POWER, decode_page))
    if args.fec_id is not None:
        sources.append(Source("bike_fec", args.fec_id, DEVTYPE_FEC, PERIOD_FEC, decode_fec))
    if len(sources) < 2:
        print("need >=2 sources: use --meter LABEL:ANTID twice (e.g. xcadey + assioma), "
              "or the legacy --stages-id/--assioma-id", file=sys.stderr)
        return 2

    print(f"Multi capture: {[(s.label, s.device_id, hex(s.device_type)) for s in sources]}, "
          f"{args.duration:.0f}s -> {args.output}")
    print("Wake ALL meters (pedal) if no broadcasts appear within ~30s.")

    runner = MultiCaptureRunner(sources=sources, output_path=args.output)
    try:
        runner.setup()
    except Exception as e:
        # Never hang holding the stick on setup failure (runbook lesson).
        print(f"setup failed: {e}", file=sys.stderr)
        try:
            runner._log("setup_error", error=str(e))
            runner._fp.flush()
        except Exception:
            pass
        os._exit(2)
    runner.run(args.duration)
    print(f"Done. {runner._messages_logged} records ({runner._counts}) -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
