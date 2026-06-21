#!/usr/bin/env python3
"""Watch SEVERAL BLE devices at once, on one clock, to one JSONL.

The multi-device companion to 06_capture_ble.py / capture_ftms.py: it connects to
each ``--device`` concurrently (the SB20's FTMS feed, a Stages crank's CPS, an
Assioma's CPS, …) and tags every notification with the device ``label`` — so the
SQLite analysis layer keys each meter's power stream by label and reconciles them
(the power-topology Phase 2 question: which meter does the SB20 erg use, and what
is the real-watts scale?). Runs on native Windows (bleak; WSL has no BT). Pure
logic + decoders live in ``sb20proxy.ble.multi_capture`` (host-tested); this is the
bleak wiring. PASSIVE — it only reads + subscribes, never writes.

Each ``--device`` is ``LABEL:KIND:ADDRESS`` (the MAC keeps its colons):
  KIND = ftms (Indoor Bike Data + Status + CP indications)
       | cps  (Cycling Power Measurement)
       | all  (every notify/indicate char — use for the SB20: FTMS + shifter + …)

Example — the Phase 2 BLE half (the ANT+ half is 07_capture_multi.py in WSL):
  python code/scripts/capture_ble_multi.py --duration 600 \
      --device sb20:all:E4:AA:5A:D6:0E:D4 \
      --device stagesL:cps:<stages-ble-addr> \
      --device assioma:cps:<assioma-ble-addr> \
      --output code/findings/captures/MULTI-ble-$(date +%Y%m%d-%H%M).jsonl

Then reconcile against the ANT+ capture on wall-clock seconds:
  python code/scripts/13_build_sqlite.py --reconcile --basis iso \
      --capture-a MULTI-ble-….jsonl --stream-a sb20 \
      --capture-b MULTI-ant-….jsonl --stream-b assioma
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
import time
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

try:
    from bleak import BleakClient
except ImportError as e:  # pragma: no cover - desk machines without bleak
    print(f"bleak not installed: {e}\nRun: pip install -e \"code/.[ble]\"", file=sys.stderr)
    sys.exit(1)

from sb20proxy.ble.multi_capture import (  # noqa: E402
    DeviceSpec,
    build_notification_data,
    parse_device_spec,
    subscriptions_for,
)


class MultiBleCapture:
    """Hold N concurrent BLE connections, log every notification to one JSONL."""

    def __init__(self, devices: list[DeviceSpec], output_path: Path) -> None:
        self.devices = devices
        self.output_path = output_path
        output_path.parent.mkdir(parents=True, exist_ok=True)
        self._fp = open(output_path, "w", buffering=1, encoding="utf-8")
        self._t0 = time.monotonic()
        self._counts: dict[str, int] = {d.label: 0 for d in devices}

    def _log(self, kind: str, **fields: Any) -> None:
        self._fp.write(json.dumps({
            "iso_time": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime()),
            "monotonic_s": round(time.monotonic() - self._t0, 6),
            "kind": kind, **fields,
        }) + "\n")

    def _on_notify(self, label: str, char_label: str, char_uuid: str,
                   svc_uuid: str | None, data: bytes) -> None:
        self._counts[label] += 1
        self._log("ble_notification", device=label, char=char_label,
                  char_uuid=char_uuid, service_uuid=svc_uuid,
                  data=build_notification_data(char_label, data))

    async def _subscribe(self, client: BleakClient, dev: DeviceSpec) -> None:
        if dev.kind == "all":
            # every notify/indicate char — catches FTMS + the shifter + vendor chars
            for svc in client.services:
                for ch in svc.characteristics:
                    if not ({"notify", "indicate"} & set(ch.properties)):
                        continue
                    label = (ch.description or ch.uuid).lower().replace(" ", "_")
                    try:
                        await client.start_notify(
                            ch.uuid, self._make_cb(dev.label, label, ch.uuid, svc.uuid))
                        self._log("ble_notify_subscribed", device=dev.label,
                                  char=label, char_uuid=ch.uuid, service_uuid=svc.uuid)
                    except Exception as exc:  # noqa: BLE001
                        self._log("ble_error", device=dev.label, phase="subscribe_all",
                                  char_uuid=ch.uuid, error=str(exc))
            return
        for uuid, char_label, _indicate in subscriptions_for(dev.kind):
            try:
                await client.start_notify(
                    uuid, self._make_cb(dev.label, char_label, uuid, None))
                self._log("ble_notify_subscribed", device=dev.label,
                          char=char_label, char_uuid=uuid)
            except Exception as exc:  # noqa: BLE001 — optional char (e.g. no Status) is fine
                self._log("ble_error", device=dev.label, phase="subscribe",
                          char=char_label, error=str(exc))

    def _make_cb(self, label: str, char_label: str, char_uuid: str, svc_uuid: str | None):
        def cb(_ch, data: bytearray) -> None:
            self._on_notify(label, char_label, char_uuid, svc_uuid, bytes(data))
        return cb

    async def _run_device(self, dev: DeviceSpec, deadline: float) -> None:
        """Connect + subscribe + hold one device, reconnecting until the deadline."""
        def on_disc(_c) -> None:
            self._log("ble_disconnect", device=dev.label, address=dev.address)

        while time.monotonic() < deadline:
            try:
                async with BleakClient(dev.address, disconnected_callback=on_disc,
                                       timeout=20.0) as client:
                    self._log("ble_connect", device=dev.label, address=dev.address, kind=dev.kind)
                    print(f"  connected: {dev.label} ({dev.address})")
                    await self._subscribe(client, dev)
                    while client.is_connected and time.monotonic() < deadline:
                        await asyncio.sleep(1.0)
            except asyncio.CancelledError:
                raise
            except Exception as exc:  # noqa: BLE001 — keep retrying until the deadline
                self._log("ble_error", device=dev.label, phase="connect_loop", error=str(exc))
                if time.monotonic() < deadline:
                    await asyncio.sleep(3.0)

    async def run(self, duration_s: float) -> None:
        self._log("session_start", protocol="ble+multi", duration_s=duration_s,
                  devices=[{"label": d.label, "kind": d.kind, "address": d.address}
                           for d in self.devices],
                  output=str(self.output_path))
        deadline = time.monotonic() + duration_s
        try:
            await asyncio.gather(*(self._run_device(d, deadline) for d in self.devices))
        except asyncio.CancelledError:
            self._log("interrupted", reason="cancelled")
        finally:
            self._log("session_end", notifications=dict(self._counts))
            self._fp.close()


def main() -> int:
    p = argparse.ArgumentParser(description="Multi-device BLE capture (one clock, one JSONL)")
    p.add_argument("--device", action="append", default=[], metavar="LABEL:KIND:ADDRESS",
                   help="a device to watch (repeatable); KIND = ftms|cps|all")
    p.add_argument("--duration", type=float, default=600.0, help="capture seconds (default 600)")
    p.add_argument("--output", type=Path, required=True, help="JSONL output path")
    args = p.parse_args()

    if not args.device:
        p.error("at least one --device LABEL:KIND:ADDRESS is required")
    try:
        devices = [parse_device_spec(s) for s in args.device]
    except ValueError as exc:
        p.error(str(exc))

    print(f"Multi-BLE capture: {len(devices)} device(s), {args.duration:.0f}s -> {args.output}")
    for d in devices:
        print(f"  - {d.label:<10} {d.kind:<5} {d.address}")
    print("Wake the meters (pedal). Ctrl-C stops early and finalises the file.\n")

    cap = MultiBleCapture(devices, args.output)
    try:
        asyncio.run(cap.run(args.duration))
    except KeyboardInterrupt:
        print("\nStopped by Ctrl-C.")
    print(f"Done -> {args.output}  (notifications: {cap._counts})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
