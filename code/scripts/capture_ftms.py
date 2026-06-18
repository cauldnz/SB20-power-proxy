#!/usr/bin/env python3
"""Phase 0 capture — the SB20's FTMS (Fitness Machine Service, 0x1826) surface.

REAL-DATA-FIRST: this only CAPTURES (logs raw bytes); it does NOT decode Indoor Bike Data or build a
codec — that's gated on the frames this produces (see the FTMS plan / capture-before-code). The SB20
is a full FTMS machine (G-stagesL-ble-recon-20260615-064641.jsonl): Indoor Bike Data (0x2AD2),
Fitness Machine Control Point (0x2AD9, write+indicate), Feature (0x2ACC), Supported Power Range
(0x2AD8). This script dumps the GATT table, reads the static chars, subscribes to Indoor Bike Data +
Fitness Machine Status, and (only with --erg) runs a GUARDED erg recon (Request Control ->
Set Target Power at a few targets), logging the control-point responses. Logs raw_hex on everything.

PASSIVE BY DEFAULT: without --erg it only reads + subscribes, never writes. --erg is an explicit,
spaced, logged Set-Target-Power sequence that *commands the real bike's resistance* — keep pedalling
through it so the captured Indoor Bike Data shows whether erg actually tracks (this is the on-bike
Session G Part C go/no-go).

Usage (PowerShell / native, the bike machine; WSL has no Bluetooth):
    # passive: capture the bike's Indoor Bike Data + GATT/feature/power-range (subscribe only)
    python code\\scripts\\capture_ftms.py --name SB20 --duration 180 \\
        --output code\\findings\\captures\\G-sb20-ftms-recon-$(Get-Date -Format yyyyMMdd-HHmm).jsonl

    # erg recon (Part C): pedal throughout; sets 150 -> 200 -> 100 W, 25 s each
    python code\\scripts\\capture_ftms.py --name SB20 --duration 240 \\
        --erg --erg-targets 150,200,100 --erg-hold 25 \\
        --output code\\findings\\captures\\G-sb20-ftms-erg-$(Get-Date -Format yyyyMMdd-HHmm).jsonl

References: FTMS GATT spec — Indoor Bike Data 0x2AD2, Control Point 0x2AD9 (Request Control 0x00,
Set Target Power 0x05 + sint16 W, Start 0x07, Stop 0x08; response 0x80 + req-op + result).
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
import time
from pathlib import Path
from typing import Any

try:
    from bleak import BleakClient, BleakScanner
except ImportError as e:
    print(f"bleak not installed: {e}\nRun: pip install -e \"code/.[ble]\"", file=sys.stderr)
    sys.exit(1)


def sig_uuid(short: int) -> str:
    return f"0000{short:04x}-0000-1000-8000-00805f9b34fb"


SVC_FTMS = sig_uuid(0x1826)
CHR_INDOOR_BIKE_DATA = sig_uuid(0x2AD2)     # notify
CHR_FTMS_STATUS = sig_uuid(0x2ADA)          # notify
CHR_TRAINING_STATUS = sig_uuid(0x2AD3)      # read/notify
CHR_FTMS_CONTROL_POINT = sig_uuid(0x2AD9)   # write + indicate
CHR_FTMS_FEATURE = sig_uuid(0x2ACC)         # read
CHR_SUPPORTED_POWER_RANGE = sig_uuid(0x2AD8)        # read
CHR_SUPPORTED_RESISTANCE_RANGE = sig_uuid(0x2AD6)   # read
CHR_SUPPORTED_INCLINATION_RANGE = sig_uuid(0x2AD5)  # read

DIS_STRING_CHARS = {
    sig_uuid(0x2A29): "manufacturer_name", sig_uuid(0x2A24): "model_number",
    sig_uuid(0x2A25): "serial_number", sig_uuid(0x2A26): "firmware_revision",
    sig_uuid(0x2A27): "hardware_revision", sig_uuid(0x2A28): "software_revision",
}
STATIC_READS = {
    CHR_FTMS_FEATURE: "fitness_machine_feature",
    CHR_SUPPORTED_POWER_RANGE: "supported_power_range",
    CHR_SUPPORTED_RESISTANCE_RANGE: "supported_resistance_level_range",
    CHR_SUPPORTED_INCLINATION_RANGE: "supported_inclination_range",
    CHR_TRAINING_STATUS: "training_status",
}

# FTMS Control Point op codes we WRITE (spec-defined; this is not a decoder).
CP_REQUEST_CONTROL = 0x00
CP_RESET = 0x01
CP_SET_TARGET_POWER = 0x05  # + sint16 LE watts
CP_START_RESUME = 0x07
CP_STOP_PAUSE = 0x08        # + 0x01 stop / 0x02 pause
CP_RESPONSE = 0x80
CP_RESULT = {0x01: "success", 0x02: "op_not_supported", 0x03: "invalid_parameter",
             0x04: "operation_failed", 0x05: "control_not_permitted"}


class FtmsCaptureRunner:
    """Scan, connect, dump GATT, read static chars, subscribe (raw), optional guarded erg."""

    def __init__(self, *, output_path: Path, name_filter: str, address: str | None,
                 duration: float, scan_time: float, erg: bool,
                 erg_targets: list[int], erg_hold: float):
        self.output_path = output_path
        self.name_filter = name_filter.lower()
        self.address = address
        self.duration = duration
        self.scan_time = scan_time
        self.erg = erg
        self.erg_targets = erg_targets
        self.erg_hold = erg_hold
        self.output_path.parent.mkdir(parents=True, exist_ok=True)
        self._fp = open(self.output_path, "w", buffering=1, encoding="utf-8")
        self._t0 = time.monotonic()
        self._ibd_count = 0
        self._erg_done = False

    def _log(self, kind: str, **fields: Any) -> None:
        self._fp.write(json.dumps({
            "iso_time": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime()),
            "monotonic_s": round(time.monotonic() - self._t0, 6),
            "kind": kind, **fields,
        }) + "\n")

    async def scan(self) -> str | None:
        self._log("ble_scan_start", scan_time_s=self.scan_time, name_filter=self.name_filter,
                  address_filter=self.address)
        if self.address:
            self._log("ble_scan_done", chosen_target=self.address)
            return self.address
        devs = await BleakScanner.discover(timeout=self.scan_time, return_adv=True)
        target, best = None, -999
        matched = []
        for addr, (d, adv) in devs.items():
            name = (adv.local_name or d.name or "").lower()
            uuids = [u.lower() for u in (adv.service_uuids or [])]
            if (self.name_filter and self.name_filter in name) or SVC_FTMS in uuids:
                matched.append({"address": addr, "name": adv.local_name or d.name,
                                "rssi": adv.rssi})
                if adv.rssi is not None and adv.rssi > best:
                    best, target = adv.rssi, addr
        self._log("ble_scan_done", matched=matched, chosen_target=target)
        return target

    async def capture_from(self, address: str) -> None:
        until = time.monotonic() + self.duration

        def on_disconnect(_c) -> None:
            self._log("ble_disconnect", address=address)

        while time.monotonic() < until:
            try:
                async with BleakClient(address, disconnected_callback=on_disconnect,
                                       timeout=20.0) as client:
                    self._log("ble_connect", address=address)
                    await self._dump_gatt(client)
                    await self._read_static(client)
                    await self._subscribe(client)
                    if self.erg and not self._erg_done:
                        # fire-and-forget so notifications keep flowing during the erg sequence
                        asyncio.ensure_future(self._erg_sequence(client))
                    while client.is_connected and time.monotonic() < until:
                        await asyncio.sleep(1.0)
            except asyncio.CancelledError:
                raise
            except Exception as e:  # noqa: BLE001 — keep retrying until the duration expires
                self._log("ble_error", phase="connect_loop", error=str(e))
                if time.monotonic() < until:
                    await asyncio.sleep(3.0)

    async def _dump_gatt(self, client: BleakClient) -> None:
        services = [{
            "uuid": svc.uuid, "description": svc.description,
            "characteristics": [{"uuid": c.uuid, "description": c.description,
                                 "properties": list(c.properties), "handle": c.handle}
                                for c in svc.characteristics],
        } for svc in client.services]
        self._log("ble_services", services=services)

    async def _read_static(self, client: BleakClient) -> None:
        for uuid, label in DIS_STRING_CHARS.items():
            try:
                val = await client.read_gatt_char(uuid)
                self._log("ble_read", char=label, uuid=uuid,
                          value=val.decode(errors="replace"), raw_hex=val.hex())
            except Exception:
                pass
        for uuid, label in STATIC_READS.items():
            try:
                val = await client.read_gatt_char(uuid)
                self._log("ble_read", char=label, uuid=uuid, raw_hex=val.hex())  # raw only
            except Exception:
                pass

    async def _subscribe(self, client: BleakClient) -> None:
        def on_ibd(_c, data: bytearray) -> None:
            self._ibd_count += 1
            d = bytes(data)
            # raw only — NO field decode (gated codec); flags_hex is just the first 2 bytes.
            self._log("ble_notification", char="indoor_bike_data",
                      data={"raw_hex": d.hex(), "len": len(d),
                            "flags_hex": d[:2].hex() if len(d) >= 2 else ""})

        def on_status(_c, data: bytearray) -> None:
            self._log("ble_notification", char="fitness_machine_status",
                      data={"raw_hex": bytes(data).hex()})

        try:
            await client.start_notify(CHR_INDOOR_BIKE_DATA, on_ibd)
            self._log("ble_notify_subscribed", char="indoor_bike_data")
        except Exception as e:  # noqa: BLE001
            self._log("ble_error", phase="subscribe_ibd", error=str(e))
        try:
            await client.start_notify(CHR_FTMS_STATUS, on_status)
            self._log("ble_notify_subscribed", char="fitness_machine_status")
        except Exception:
            pass

    async def _erg_sequence(self, client: BleakClient) -> None:
        """GUARDED erg recon: Request Control -> Start -> Set Target Power per target -> release.
        Each control-point indication is logged raw (response 0x80 + req-op + result)."""
        self._erg_done = True

        def on_cp(_c, data: bytearray) -> None:
            d = bytes(data)
            rec: dict[str, Any] = {"raw_hex": d.hex()}
            if len(d) >= 3 and d[0] == CP_RESPONSE:  # fixed 3-byte response header (not a codec)
                rec.update(response_opcode=d[0], request_opcode=d[1], result=d[2],
                           result_name=CP_RESULT.get(d[2], f"0x{d[2]:02X}"))
            self._log("ble_notification", char="fitness_machine_control_point", data=rec)

        try:
            await client.start_notify(CHR_FTMS_CONTROL_POINT, on_cp)  # indications
        except Exception as e:  # noqa: BLE001
            self._log("ble_error", phase="cp_indicate", error=str(e))
            return

        async def write(cmd: bytes, note: str) -> None:
            self._log("ble_cp_write", note=note, raw_hex=cmd.hex())
            try:
                await client.write_gatt_char(CHR_FTMS_CONTROL_POINT, cmd, response=True)
            except Exception as e:  # noqa: BLE001
                self._log("ble_error", phase="cp_write", note=note, error=str(e))
            await asyncio.sleep(2.0)  # let the indication land

        await asyncio.sleep(3.0)  # settle after subscribe
        await write(bytes([CP_REQUEST_CONTROL]), "request_control")
        await write(bytes([CP_START_RESUME]), "start_resume")
        for watts in self.erg_targets:
            cmd = bytes([CP_SET_TARGET_POWER]) + int(watts).to_bytes(2, "little", signed=True)
            await write(cmd, f"set_target_power={watts}W")
            self._log("ble_erg_hold", target_w=watts, hold_s=self.erg_hold)
            await asyncio.sleep(self.erg_hold)  # hold so the rider pedals + IBD power can track
        await write(bytes([CP_RESET]), "reset_release_control")

    def close(self) -> None:
        self._log("ble_capture_done", indoor_bike_data_count=self._ibd_count)
        self._fp.close()


async def run(args: argparse.Namespace) -> None:
    runner = FtmsCaptureRunner(
        output_path=Path(args.output), name_filter=args.name, address=args.address,
        duration=args.duration, scan_time=args.scan_time, erg=args.erg,
        erg_targets=[int(x) for x in args.erg_targets.split(",") if x.strip()],
        erg_hold=args.erg_hold)
    try:
        target = await runner.scan()
        if not target:
            print("no FTMS device found (try --name or --address)", file=sys.stderr)
        else:
            print(f"capturing FTMS from {target} for {args.duration:.0f}s "
                  f"{'(ERG: ' + args.erg_targets + 'W)' if args.erg else '(passive)'} ...")
            await runner.capture_from(target)
    finally:
        runner.close()
        print(f"wrote {args.output}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Capture the SB20 FTMS surface (raw; codec gated).")
    ap.add_argument("--name", default="SB20", help="advertised-name substring (default SB20)")
    ap.add_argument("--address", help="connect to a specific BLE address instead of scanning")
    ap.add_argument("--duration", type=float, default=180.0, help="capture seconds")
    ap.add_argument("--scan-time", type=float, default=8.0)
    ap.add_argument("--output", required=True, help="JSONL output path")
    ap.add_argument("--erg", action="store_true",
                    help="GUARDED erg recon (writes Set-Target-Power to the real bike!)")
    ap.add_argument("--erg-targets", default="150,200,100", help="comma-sep target watts for --erg")
    ap.add_argument("--erg-hold", type=float, default=25.0, help="seconds to hold each erg target")
    args = ap.parse_args()
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        print("\nstopped (data kept).")


if __name__ == "__main__":
    main()
