#!/usr/bin/env python3
"""Phase 0 capture script — BLE Cycling Power traffic (parallel to ANT+ capture).

Connects to a power meter's BLE side as a *second client* (soft sniffing) and
logs advertisements, the GATT table, device-info reads, and Cycling Power
Measurement notifications to JSONL. Runs on native Windows (WSL2 has no
Bluetooth without a custom kernel), Linux, or macOS — bleak abstracts the OS.

Intended use during Phase 0: run this in a SECOND terminal (PowerShell on
Windows) while 01_capture_stages.py runs in WSL. Both machines share the same
wall clock, so iso_time aligns the two JSONL streams.

PASSIVE BY DESIGN: this script never writes to any characteristic. The
Cycling Power Control Point (0x2A66) is logged as present but not touched —
do not add calibration pokes during Session A/D captures.

Usage (PowerShell, from the repo root):
    python code\\scripts\\06_capture_ble.py --name Stages --duration 900 \\
        --output code\\findings\\captures\\A-stagesL-ble-20260610-1830.jsonl

    # Advertisement survey only (no connection):
    python code\\scripts\\06_capture_ble.py --adv-only --duration 60 \\
        --output code\\findings\\captures\\ble-adv-survey.jsonl

References:
- BLE Cycling Power Service 0x1818, Measurement 0x2A63 (GATT spec field order)
- Project notes: 02-technical-context.md §BLE Cycling Power Service primer
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
    print(f"bleak not installed or import failed: {e}", file=sys.stderr)
    print("Run: pip install bleak", file=sys.stderr)
    sys.exit(1)


# 16-bit SIG UUIDs, expanded to full form for bleak comparisons
def sig_uuid(short: int) -> str:
    return f"0000{short:04x}-0000-1000-8000-00805f9b34fb"


SVC_CYCLING_POWER = sig_uuid(0x1818)
SVC_CSC = sig_uuid(0x1816)
SVC_DEVICE_INFO = sig_uuid(0x180A)
SVC_BATTERY = sig_uuid(0x180F)

CHR_CP_MEASUREMENT = sig_uuid(0x2A63)
CHR_CP_FEATURE = sig_uuid(0x2A65)
CHR_CP_CONTROL_POINT = sig_uuid(0x2A66)
CHR_SENSOR_LOCATION = sig_uuid(0x2A5D)
CHR_CSC_MEASUREMENT = sig_uuid(0x2A5B)
CHR_BATTERY_LEVEL = sig_uuid(0x2A19)

# Device Information Service strings worth reading once
DIS_STRING_CHARS = {
    sig_uuid(0x2A29): "manufacturer_name",
    sig_uuid(0x2A24): "model_number",
    sig_uuid(0x2A25): "serial_number",
    sig_uuid(0x2A26): "firmware_revision",
    sig_uuid(0x2A27): "hardware_revision",
    sig_uuid(0x2A28): "software_revision",
}

SENSOR_LOCATIONS = {
    0: "other", 1: "top_of_shoe", 2: "in_shoe", 3: "hip", 4: "front_wheel",
    5: "left_crank", 6: "right_crank", 7: "left_pedal", 8: "right_pedal",
    9: "front_hub", 10: "rear_dropout", 11: "chainstay", 12: "rear_wheel",
    13: "rear_hub", 14: "chest", 15: "spider", 16: "chain_ring",
}


def decode_cp_measurement(data: bytes) -> dict[str, Any]:
    """Decode a Cycling Power Measurement (0x2A63) notification.

    Field order follows the GATT spec: little-endian flags uint16, then
    instantaneous power sint16, then optional fields in flag-bit order.
    Always includes raw_hex; unknown tails are preserved as trailing_hex.
    """
    out: dict[str, Any] = {"raw_hex": data.hex()}
    if len(data) < 4:
        out["error"] = "short payload"
        return out

    flags = int.from_bytes(data[0:2], "little")
    out["flags"] = flags
    out["flags_hex"] = f"0x{flags:04X}"
    out["instantaneous_power_w"] = int.from_bytes(data[2:4], "little", signed=True)
    i = 4

    def take(n: int) -> bytes | None:
        nonlocal i
        if i + n > len(data):
            return None
        chunk = data[i:i + n]
        i += n
        return chunk

    try:
        if flags & 0x0001:  # pedal power balance
            b = take(1)
            if b is not None:
                out["pedal_power_balance_pct"] = b[0] / 2.0
                out["balance_reference_left"] = bool(flags & 0x0002)
        if flags & 0x0004:  # accumulated torque (1/32 Nm)
            b = take(2)
            if b is not None:
                out["accumulated_torque_raw"] = int.from_bytes(b, "little")
                out["accumulated_torque_source_crank"] = bool(flags & 0x0008)
        if flags & 0x0010:  # wheel revolution data
            b = take(6)
            if b is not None:
                out["cumulative_wheel_revs"] = int.from_bytes(b[0:4], "little")
                out["last_wheel_event_time_2048s"] = int.from_bytes(b[4:6], "little")
        if flags & 0x0020:  # crank revolution data
            b = take(4)
            if b is not None:
                out["cumulative_crank_revs"] = int.from_bytes(b[0:2], "little")
                out["last_crank_event_time_1024s"] = int.from_bytes(b[2:4], "little")
        if flags & 0x0040:  # extreme force magnitudes
            b = take(4)
            if b is not None:
                out["max_force_n"] = int.from_bytes(b[0:2], "little", signed=True)
                out["min_force_n"] = int.from_bytes(b[2:4], "little", signed=True)
        if flags & 0x0080:  # extreme torque magnitudes (1/32 Nm)
            b = take(4)
            if b is not None:
                out["max_torque_raw"] = int.from_bytes(b[0:2], "little", signed=True)
                out["min_torque_raw"] = int.from_bytes(b[2:4], "little", signed=True)
        if flags & 0x0100:  # extreme angles (uint24: two 12-bit angles)
            b = take(3)
            if b is not None:
                packed = int.from_bytes(b, "little")
                out["max_angle_deg"] = packed & 0xFFF
                out["min_angle_deg"] = (packed >> 12) & 0xFFF
        if flags & 0x0200:  # top dead spot angle
            b = take(2)
            if b is not None:
                out["top_dead_spot_angle_deg"] = int.from_bytes(b, "little")
        if flags & 0x0400:  # bottom dead spot angle
            b = take(2)
            if b is not None:
                out["bottom_dead_spot_angle_deg"] = int.from_bytes(b, "little")
        if flags & 0x0800:  # accumulated energy (kJ)
            b = take(2)
            if b is not None:
                out["accumulated_energy_kj"] = int.from_bytes(b, "little")
        out["offset_compensation_indicator"] = bool(flags & 0x1000)
    except Exception as e:  # never let a decode bug drop a record
        out["decode_error"] = str(e)

    if i < len(data):
        out["trailing_hex"] = data[i:].hex()
    return out


def decode_csc_measurement(data: bytes) -> dict[str, Any]:
    """Decode CSC Measurement (0x2A5B): flags uint8 + optional wheel/crank revs."""
    out: dict[str, Any] = {"raw_hex": data.hex()}
    if len(data) < 1:
        out["error"] = "short payload"
        return out
    flags = data[0]
    out["flags"] = flags
    i = 1
    if flags & 0x01 and len(data) >= i + 6:
        out["cumulative_wheel_revs"] = int.from_bytes(data[i:i + 4], "little")
        out["last_wheel_event_time_1024s"] = int.from_bytes(data[i + 4:i + 6], "little")
        i += 6
    if flags & 0x02 and len(data) >= i + 4:
        out["cumulative_crank_revs"] = int.from_bytes(data[i:i + 2], "little")
        out["last_crank_event_time_1024s"] = int.from_bytes(data[i + 2:i + 4], "little")
        i += 4
    return out


class BleCaptureRunner:
    """Scan, optionally connect, subscribe, and log everything to JSONL."""

    def __init__(self, *, output_path: Path, name_filter: str,
                 address: str | None, adv_only: bool, scan_time: float):
        self.output_path = output_path
        self.name_filter = name_filter.lower()
        self.address = address
        self.adv_only = adv_only
        self.scan_time = scan_time
        self.output_path.parent.mkdir(parents=True, exist_ok=True)
        self._fp = open(self.output_path, "w", buffering=1, encoding="utf-8")
        self._t0 = time.monotonic()
        self._records = 0
        self._seen_advs: dict[str, int] = {}
        self._notif_count = 0

    def _log(self, kind: str, **fields: Any) -> None:
        record = {
            "iso_time": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime()),
            "monotonic_s": round(time.monotonic() - self._t0, 6),
            "kind": kind,
            **fields,
        }
        self._fp.write(json.dumps(record) + "\n")
        self._records += 1

    # --- scan phase ---------------------------------------------------------

    def _adv_matches(self, device, adv) -> bool:
        name = (adv.local_name or device.name or "").lower()
        if self.name_filter and self.name_filter in name:
            return True
        uuids = [u.lower() for u in (adv.service_uuids or [])]
        return SVC_CYCLING_POWER in uuids or SVC_CSC in uuids

    def _on_advertisement(self, device, adv) -> None:
        if not self._adv_matches(device, adv):
            return
        # Log every matching adv, but throttle identical repeats per address
        count = self._seen_advs.get(device.address, 0)
        self._seen_advs[device.address] = count + 1
        if count < 5 or count % 25 == 0:
            self._log("ble_advertisement",
                      address=device.address,
                      name=adv.local_name or device.name,
                      rssi=adv.rssi,
                      service_uuids=list(adv.service_uuids or []),
                      manufacturer_data={
                          str(k): v.hex() for k, v in
                          (adv.manufacturer_data or {}).items()},
                      seen_count=count + 1)

    async def scan(self) -> str | None:
        """Scan for scan_time seconds; return the address to connect to."""
        self._log("ble_scan_start", scan_time_s=self.scan_time,
                  name_filter=self.name_filter, address_filter=self.address)
        scanner = BleakScanner(detection_callback=self._on_advertisement)
        await scanner.start()
        await asyncio.sleep(self.scan_time)
        await scanner.stop()

        target = None
        if self.address:
            target = self.address
        else:
            # best-RSSI device whose adv matched the name filter
            best_rssi = -999
            for d in scanner.discovered_devices_and_advertisement_data.values():
                device, adv = d
                name = (adv.local_name or device.name or "").lower()
                if self.name_filter in name and adv.rssi is not None \
                        and adv.rssi > best_rssi:
                    best_rssi = adv.rssi
                    target = device.address
        self._log("ble_scan_done",
                  matching_addresses=list(self._seen_advs.keys()),
                  chosen_target=target)
        return target

    # --- connect + subscribe phase -----------------------------------------

    async def capture_from(self, address: str, until_monotonic: float) -> None:
        """Connect, dump GATT, read DIS/feature chars, subscribe, hold."""
        def on_disconnect(_client) -> None:
            self._log("ble_disconnect", address=address)

        while time.monotonic() < until_monotonic:
            try:
                async with BleakClient(
                        address, disconnected_callback=on_disconnect,
                        timeout=20.0) as client:
                    self._log("ble_connect", address=address)
                    await self._dump_gatt(client)
                    await self._read_static_chars(client)
                    await self._subscribe(client)
                    # Hold the connection until the deadline; notifications
                    # arrive via callbacks while we sleep.
                    while client.is_connected and \
                            time.monotonic() < until_monotonic:
                        await asyncio.sleep(1.0)
            except asyncio.CancelledError:
                raise
            except Exception as e:
                self._log("ble_error", phase="connect_loop", error=str(e))
                if time.monotonic() < until_monotonic:
                    await asyncio.sleep(3.0)  # retry until duration expires

    async def _dump_gatt(self, client: BleakClient) -> None:
        services = []
        for svc in client.services:
            services.append({
                "uuid": svc.uuid,
                "description": svc.description,
                "characteristics": [{
                    "uuid": c.uuid,
                    "description": c.description,
                    "properties": list(c.properties),
                    "handle": c.handle,
                } for c in svc.characteristics],
            })
        self._log("ble_services", services=services)

    async def _read_static_chars(self, client: BleakClient) -> None:
        # Device Information strings
        for uuid, label in DIS_STRING_CHARS.items():
            try:
                val = await client.read_gatt_char(uuid)
                self._log("ble_read", char=label, uuid=uuid,
                          value=val.decode(errors="replace"), raw_hex=val.hex())
            except Exception:
                pass  # char not present — normal
        # Cycling Power Feature (uint32 bitfield) and Sensor Location
        try:
            val = await client.read_gatt_char(CHR_CP_FEATURE)
            self._log("ble_read", char="cycling_power_feature",
                      uuid=CHR_CP_FEATURE,
                      value=int.from_bytes(val[:4], "little"), raw_hex=val.hex())
        except Exception:
            pass
        try:
            val = await client.read_gatt_char(CHR_SENSOR_LOCATION)
            self._log("ble_read", char="sensor_location",
                      uuid=CHR_SENSOR_LOCATION,
                      value=SENSOR_LOCATIONS.get(val[0], val[0]),
                      raw_hex=val.hex())
        except Exception:
            pass
        try:
            val = await client.read_gatt_char(CHR_BATTERY_LEVEL)
            self._log("ble_read", char="battery_level_pct",
                      uuid=CHR_BATTERY_LEVEL, value=val[0], raw_hex=val.hex())
        except Exception:
            pass

    async def _subscribe(self, client: BleakClient) -> None:
        def on_cp(_char, data: bytearray) -> None:
            self._notif_count += 1
            self._log("ble_notification", char="cycling_power_measurement",
                      data=decode_cp_measurement(bytes(data)))

        def on_csc(_char, data: bytearray) -> None:
            self._log("ble_notification", char="csc_measurement",
                      data=decode_csc_measurement(bytes(data)))

        def on_batt(_char, data: bytearray) -> None:
            self._log("ble_notification", char="battery_level_pct",
                      data={"value": data[0] if data else None,
                            "raw_hex": bytes(data).hex()})

        try:
            await client.start_notify(CHR_CP_MEASUREMENT, on_cp)
            self._log("ble_notify_subscribed", char="cycling_power_measurement")
        except Exception as e:
            self._log("ble_error", phase="subscribe_cp", error=str(e))
        for uuid, cb, label in ((CHR_CSC_MEASUREMENT, on_csc, "csc_measurement"),
                                (CHR_BATTERY_LEVEL, on_batt, "battery_level")):
            try:
                await client.start_notify(uuid, cb)
                self._log("ble_notify_subscribed", char=label)
            except Exception:
                pass  # optional characteristics

    # --- lifecycle ----------------------------------------------------------

    async def run(self, duration_s: float) -> None:
        self._log("session_start", protocol="ble",
                  name_filter=self.name_filter, address=self.address,
                  adv_only=self.adv_only, duration_s=duration_s,
                  output=str(self.output_path))
        deadline = time.monotonic() + duration_s
        try:
            if self.adv_only:
                # Continuous advertisement survey for the whole duration
                scanner = BleakScanner(
                    detection_callback=self._on_advertisement)
                await scanner.start()
                while time.monotonic() < deadline:
                    await asyncio.sleep(1.0)
                await scanner.stop()
            else:
                target = await self.scan()
                if target is None:
                    self._log("ble_error", phase="scan",
                              error="no matching device found — is the meter "
                                    "awake? (rotate cranks)")
                else:
                    await self.capture_from(target, deadline)
        except asyncio.CancelledError:
            self._log("interrupted", reason="cancelled")
        finally:
            self._log("session_end", records=self._records,
                      cp_notifications=self._notif_count)
            self._fp.close()


def main() -> int:
    p = argparse.ArgumentParser(
        description="Phase 0 BLE Cycling Power capture (passive, parallel to ANT+)")
    p.add_argument("--output", type=Path, required=True, help="Output JSONL path")
    p.add_argument("--duration", type=float, default=900.0,
                   help="Capture duration in seconds (default 900 = 15 min)")
    p.add_argument("--name", default="Stages",
                   help="Device name substring to match (default 'Stages'; "
                        "use 'Assioma' for Session D)")
    p.add_argument("--address", default=None,
                   help="Connect to this exact BLE address (skips name matching)")
    p.add_argument("--adv-only", action="store_true",
                   help="Only log advertisements for the whole duration; never connect. "
                        "Use this for a first survey of what's on the air.")
    p.add_argument("--scan-time", type=float, default=15.0,
                   help="Seconds to scan before connecting (default 15)")
    args = p.parse_args()

    # ASCII-only console output: this script's primary platform is Windows
    # PowerShell, whose default code page chokes on unicode arrows etc.
    print(f"BLE capture: filter '{args.name}', {args.duration:.0f}s -> {args.output}")
    print("Wake the meter (rotate cranks). Ctrl-C stops early and finalises the file.")

    runner = BleCaptureRunner(output_path=args.output, name_filter=args.name,
                              address=args.address, adv_only=args.adv_only,
                              scan_time=args.scan_time)
    try:
        asyncio.run(runner.run(args.duration))
    except KeyboardInterrupt:
        # asyncio.run tears the loop down; the finally in run() already wrote
        # session_end if the loop got that far. Make the exit quiet either way.
        print("\nStopped by Ctrl-C.")
    print(f"Done. {runner._records} records -> {args.output} "
          f"({runner._notif_count} power notifications)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
