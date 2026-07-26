#!/usr/bin/env python3
"""Phase 0 capture script — BLE Cycling Power traffic (parallel to ANT+ capture).

Connects to a power meter's BLE side as a *second client* (soft sniffing) and
logs advertisements, the GATT table, device-info reads, and Cycling Power
Measurement notifications to JSONL. Runs on native Windows (WSL2 has no
Bluetooth without a custom kernel), Linux, or macOS — bleak abstracts the OS.

Intended use during Phase 0: run this in a SECOND terminal (PowerShell on
Windows) while 01_capture_stages.py runs in WSL. Both machines share the same
wall clock, so iso_time aligns the two JSONL streams.

PASSIVE BY DEFAULT: with no --control-point flag this script only reads and
subscribes; it never writes. The opt-in --control-point mode performs explicit,
single, logged Cycling Power Control Point (0x2A66) writes for calibration recon
(Session G Part A) — e.g. offset-compensation (the BLE analogue of the ANT+
zero-reset) or read-only requests like request-crank-length. Guarded, one op at
a time (mirrors raedian-probe/probe_write.py); never during adv-survey / plain
connect captures.

Usage (PowerShell, from the repo root):
    python code\\scripts\\06_capture_ble.py --name Stages --duration 900 \\
        --output code\\findings\\captures\\A-stagesL-ble-20260610-1830.jsonl

    # Advertisement survey only (no connection):
    python code\\scripts\\06_capture_ble.py --adv-only --duration 60 \\
        --output code\\findings\\captures\\ble-adv-survey.jsonl

    # Session G Part A — characterise the crank + read its config + zero-reset.
    # Read-only first; add offset-compensation only with the cranks held STILL:
    python code\\scripts\\06_capture_ble.py --name Stages --duration 120 \\
        --control-point request-crank-length,request-sensor-locations,offset-compensation \\
        --output code\\findings\\captures\\G-stagesL-ble-recon.jsonl

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

# The CPS flag bits and control-point op/result codes come from the package, so there is exactly
# ONE definition of each protocol byte.
#
# The DECODERS below are deliberately NOT the package's, and should not be collapsed into them:
#   * this script decodes ALL 13 optional CPS fields; `decode_cps_measurement` stops after the
#     four our meters actually set (it says so, and extending it is a runtime decision);
#   * this script is TOLERANT — a truncated or malformed frame yields a partial dict with a
#     `decode_error` key, because a capture must never drop a record ("JSONL captures are the
#     canonical lossless record", CLAUDE.md). The package decoder RAISES, which is right for the
#     runtime path and wrong here;
#   * `decode_cp_response` here INTERPRETS the response params per op (offset, crank length,
#     sensor locations, sampling rate, factory cal date) including the Assioma-vs-Stages
#     trailing-bytes subtlety; `decode_control_point` returns them raw for the proxy to act on.
# Two jobs, two shapes. Sharing the constants removes the drift risk without flattening either.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src"))
from sb20proxy.ble import cps as _cps  # noqa: E402

try:
    from bleak import BleakClient, BleakScanner
except ImportError as e:  # pragma: no cover - dependency guard
    _msg = f"bleak not installed or import failed: {e}\nRun: pip install -e \"code/.[ble]\""
    if __name__ == "__main__":
        print(_msg, file=sys.stderr)
        sys.exit(1)
    raise ImportError(_msg) from e  # importable: let callers skip, don't kill them


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

# Cycling Power Control Point (0x2A66) op codes — for the guarded calibration
# recon (Session G Part A). It's Write + Indicate: enable indications, write one
# op, read the indication. Codes per the BLE Cycling Power Service spec.
#
# The op codes and result codes come from the package so there is ONE definition of each byte;
# the human-readable names below are this script's (a capture log wants labels, the runtime
# codec doesn't). If you add an op here, add its constant to sb20proxy/ble/cps.py first.
CP_OPS = {
    "request-sensor-locations": _cps.CP_REQUEST_SUPPORTED_SENSOR_LOCATIONS,
    "request-crank-length": _cps.CP_REQUEST_CRANK_LENGTH,  # uint16, 1/2 mm
    "request-chain-length": 0x07,
    "request-chain-weight": 0x09,
    "request-span-length": 0x0B,
    "offset-compensation": _cps.CP_START_OFFSET_COMPENSATION,  # zero-reset; offset (sint16)
    "request-sampling-rate": 0x0E,
    "request-factory-cal-date": 0x0F,
    "enhanced-offset-compensation": 0x10,
}
CP_RESPONSE_OPCODE = _cps.CP_RESPONSE_CODE
CP_RESULT = {
    _cps.CP_RESULT_SUCCESS: "success",
    _cps.CP_RESULT_OP_NOT_SUPPORTED: "op_code_not_supported",
    _cps.CP_RESULT_INVALID_PARAMETER: "invalid_parameter",
    _cps.CP_RESULT_OPERATION_FAILED: "operation_failed",
}

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
        if flags & _cps.F_PEDAL_BALANCE:
            b = take(1)
            if b is not None:
                out["pedal_power_balance_pct"] = b[0] / 2.0
                out["balance_reference_left"] = bool(flags & _cps.F_BALANCE_REF_LEFT)
        if flags & _cps.F_ACCUM_TORQUE:  # 1/32 Nm
            b = take(2)
            if b is not None:
                out["accumulated_torque_raw"] = int.from_bytes(b, "little")
                out["accumulated_torque_source_crank"] = bool(flags & _cps.F_TORQUE_SOURCE_CRANK)
        if flags & _cps.F_WHEEL_REV:
            b = take(6)
            if b is not None:
                out["cumulative_wheel_revs"] = int.from_bytes(b[0:4], "little")
                out["last_wheel_event_time_2048s"] = int.from_bytes(b[4:6], "little")
        if flags & _cps.F_CRANK_REV:
            b = take(4)
            if b is not None:
                out["cumulative_crank_revs"] = int.from_bytes(b[0:2], "little")
                out["last_crank_event_time_1024s"] = int.from_bytes(b[2:4], "little")
        if flags & _cps.F_EXTREME_FORCE:
            b = take(4)
            if b is not None:
                out["max_force_n"] = int.from_bytes(b[0:2], "little", signed=True)
                out["min_force_n"] = int.from_bytes(b[2:4], "little", signed=True)
        if flags & _cps.F_EXTREME_TORQUE:  # 1/32 Nm
            b = take(4)
            if b is not None:
                out["max_torque_raw"] = int.from_bytes(b[0:2], "little", signed=True)
                out["min_torque_raw"] = int.from_bytes(b[2:4], "little", signed=True)
        if flags & _cps.F_EXTREME_ANGLES:  # uint24: two 12-bit angles
            b = take(3)
            if b is not None:
                packed = int.from_bytes(b, "little")
                out["max_angle_deg"] = packed & 0xFFF
                out["min_angle_deg"] = (packed >> 12) & 0xFFF
        if flags & _cps.F_TOP_DEAD_SPOT:
            b = take(2)
            if b is not None:
                out["top_dead_spot_angle_deg"] = int.from_bytes(b, "little")
        if flags & _cps.F_BOTTOM_DEAD_SPOT:
            b = take(2)
            if b is not None:
                out["bottom_dead_spot_angle_deg"] = int.from_bytes(b, "little")
        if flags & _cps.F_ACCUM_ENERGY:  # kJ
            b = take(2)
            if b is not None:
                out["accumulated_energy_kj"] = int.from_bytes(b, "little")
        out["offset_compensation_indicator"] = bool(flags & _cps.F_OFFSET_COMP_IND)
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


def decode_cp_response(data: bytes) -> dict[str, Any]:
    """Decode a Cycling Power Control Point indication (response opcode 0x20)."""
    out: dict[str, Any] = {"raw_hex": data.hex()}
    if len(data) < 3:
        out["error"] = "short payload"
        return out
    out["response_opcode"] = data[0]           # expect 0x20
    out["request_opcode"] = data[1]
    out["result"] = data[2]
    out["result_name"] = CP_RESULT.get(data[2], f"0x{data[2]:02X}")
    params = data[3:]
    if params:
        out["params_hex"] = params.hex()
    req = data[1]
    not_supported = (data[2] == 0x02 and len(data) == 3)
    # Value-returning ops carry their 16-bit value in the LAST two bytes. Some
    # meters include the 0x01 "success" result byte (Assioma: 20 05 01 59 01),
    # others omit it (Stages SPM2 crank: 20 05 59 01) — trailing-bytes handles both.
    val16 = data[-2:] if (len(data) >= 4 and not not_supported) else None
    try:
        if req in (0x0C, 0x10) and val16:              # (enhanced) offset compensation
            out["offset"] = int.from_bytes(val16, "little", signed=True)
        elif req == 0x05 and val16:                    # crank length, units of 1/2 mm
            out["crank_length_mm"] = int.from_bytes(val16, "little") / 2.0
        elif req == 0x03 and not not_supported:        # supported sensor locations
            out["sensor_locations"] = [SENSOR_LOCATIONS.get(b, b) for b in params]
        elif req == 0x0E and params:                   # sampling rate (Hz)
            out["sampling_rate_hz"] = params[0]
        elif req == 0x0F and len(params) >= 7:         # factory calibration date
            out["factory_cal_date_hex"] = params[0:7].hex()
    except Exception as e:
        out["decode_error"] = str(e)
    return out


class BleCaptureRunner:
    """Scan, optionally connect, subscribe, and log everything to JSONL."""

    def __init__(self, *, output_path: Path, name_filter: str,
                 address: str | None, adv_only: bool, scan_time: float,
                 control_point_ops: list[str] | None = None,
                 subscribe_all: bool = False):
        self.output_path = output_path
        self.name_filter = name_filter.lower()
        self.address = address
        self.adv_only = adv_only
        self.scan_time = scan_time
        self.control_point_ops = control_point_ops or []
        self.subscribe_all = subscribe_all
        self.output_path.parent.mkdir(parents=True, exist_ok=True)
        self._fp = open(self.output_path, "w", buffering=1, encoding="utf-8")
        self._t0 = time.monotonic()
        self._records = 0
        self._seen_advs: dict[str, int] = {}
        self._notif_count = 0
        self._cp_done = False

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
                    if self.control_point_ops and not self._cp_done:
                        await self._control_point(client)
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

        subscribed: set[str] = set()
        try:
            await client.start_notify(CHR_CP_MEASUREMENT, on_cp)
            subscribed.add(CHR_CP_MEASUREMENT.lower())
            self._log("ble_notify_subscribed", char="cycling_power_measurement")
        except Exception as e:
            self._log("ble_error", phase="subscribe_cp", error=str(e))
        for uuid, cb, label in ((CHR_CSC_MEASUREMENT, on_csc, "csc_measurement"),
                                (CHR_BATTERY_LEVEL, on_batt, "battery_level")):
            try:
                await client.start_notify(uuid, cb)
                subscribed.add(uuid.lower())
                self._log("ble_notify_subscribed", char=label)
            except Exception:
                pass  # optional characteristics
        if self.subscribe_all:
            await self._subscribe_all_other(client, subscribed)

    async def _subscribe_all_other(self, client: BleakClient,
                                   subscribed: set[str]) -> None:
        """Subscribe to EVERY other notify/indicate characteristic.

        For exploratory probes (e.g. "does pressing the SB20 shifter buttons emit
        a BLE packet?") we can't know in advance which characteristic carries the
        signal — it may be vendor-specific or on FTMS, not the power/CSC chars the
        standard path hooks. So walk the GATT table and subscribe to every
        notifiable/indicatable characteristic with a generic logger that prints
        live (so the operator sees a press register) and records raw bytes.
        """
        for svc in client.services:
            for ch in svc.characteristics:
                props = set(ch.properties)
                if not ({"notify", "indicate"} & props):
                    continue
                if ch.uuid.lower() in subscribed:
                    continue

                def make_cb(svc_uuid: str, char_uuid: str, desc: str):
                    def on_any(_char, data: bytearray) -> None:
                        self._notif_count += 1
                        raw = bytes(data)
                        self._log("ble_notification", char=desc or "unknown",
                                  char_uuid=char_uuid, service_uuid=svc_uuid,
                                  data={"raw_hex": raw.hex(),
                                        "ascii": raw.decode("latin-1",
                                                            errors="replace")})
                        print(f"  [notify] {desc or char_uuid}: {raw.hex()}")
                    return on_any

                try:
                    await client.start_notify(
                        ch.uuid, make_cb(svc.uuid, ch.uuid, ch.description))
                    subscribed.add(ch.uuid.lower())
                    self._log("ble_notify_subscribed", char=ch.description,
                              char_uuid=ch.uuid, service_uuid=svc.uuid,
                              via="subscribe_all")
                    print(f"  subscribed: {ch.description or ch.uuid} "
                          f"({','.join(sorted(props))})")
                except Exception as e:
                    self._log("ble_error", phase="subscribe_all",
                              char_uuid=ch.uuid, error=str(e))

    async def _control_point(self, client: BleakClient) -> None:
        """Guarded Cycling Power Control Point recon (Session G Part A).

        Mirrors raedian-probe/probe_write.py: explicit single ops, each logged,
        no blind loops. Enables CP indications, then for each requested op writes
        it once and lets the indication arrive (logged via the callback).
        offset-compensation is the BLE analogue of the ANT+ zero-reset — its
        indication carries the offset, like the 0xAC/903 we saw on ANT+.
        """
        def on_cp_indication(_char, data: bytearray) -> None:
            self._log("ble_cp_indication", char="cycling_power_control_point",
                      data=decode_cp_response(bytes(data)))

        try:
            await client.start_notify(CHR_CP_CONTROL_POINT, on_cp_indication)
            self._log("ble_cp_subscribed", char="cycling_power_control_point")
        except Exception as e:
            self._log("ble_error", phase="cp_subscribe", error=str(e))
            print(f"  control-point indications unavailable: {e}")
            self._cp_done = True
            return

        for op_name in self.control_point_ops:
            opcode = CP_OPS.get(op_name)
            if opcode is None:
                self._log("ble_cp_skip", op=op_name, reason="unknown op-code name")
                print(f"  skipping unknown op '{op_name}'")
                continue
            if op_name in ("offset-compensation", "enhanced-offset-compensation"):
                print("  >>> KEEP THE CRANKS STILL AND UNLOADED for offset compensation <<<")
            self._log("ble_cp_write", op=op_name, opcode=opcode)
            print(f"  control-point write: {op_name} (0x{opcode:02X})")
            try:
                await client.write_gatt_char(
                    CHR_CP_CONTROL_POINT, bytes([opcode]), response=True)
            except Exception as e:
                # An auth/insufficient-encryption error here is itself a finding
                # (the bike may need bonding before control-point writes).
                self._log("ble_error", phase=f"cp_write:{op_name}", error=str(e))
                print(f"    write failed: {e}")
                continue
            await asyncio.sleep(3.0)  # indication arrives via the callback
        self._cp_done = True

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
    p.add_argument("--subscribe-all", action="store_true",
                   help="Subscribe to EVERY notify/indicate characteristic, not just "
                        "power/CSC/battery. Use for exploratory probes — e.g. do the "
                        "SB20 shifter buttons emit BLE packets?")
    p.add_argument("--control-point", default=None,
                   help="Comma-separated Cycling Power Control Point ops to run once "
                        "after connecting (Session G Part A). Read-only requests are "
                        "safe; 'offset-compensation' is the zero-reset (keep cranks "
                        "STILL). Known ops: " + ", ".join(CP_OPS))
    args = p.parse_args()

    cp_ops = None
    if args.control_point:
        cp_ops = [s.strip() for s in args.control_point.split(",") if s.strip()]
        unknown = [o for o in cp_ops if o not in CP_OPS]
        if unknown:
            print(f"Unknown control-point op(s): {unknown}\nKnown: {', '.join(CP_OPS)}",
                  file=sys.stderr)
            return 2

    # ASCII-only console output: this script's primary platform is Windows
    # PowerShell, whose default code page chokes on unicode arrows etc.
    print(f"BLE capture: filter '{args.name}', {args.duration:.0f}s -> {args.output}")
    print("Wake the meter (rotate cranks). Ctrl-C stops early and finalises the file.")

    if cp_ops:
        print(f"control-point ops queued (Session G Part A): {cp_ops}")
    runner = BleCaptureRunner(output_path=args.output, name_filter=args.name,
                              address=args.address, adv_only=args.adv_only,
                              scan_time=args.scan_time, control_point_ops=cp_ops,
                              subscribe_all=args.subscribe_all)
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
