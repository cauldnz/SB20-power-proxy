#!/usr/bin/env python3
"""ftms_hw_loop.py — real on-air FTMS validation against the ESP32 FTMS firmware.

The F6 hardware tier of the FTMS build: proves the spec-built codec + the firmware
seam talk FTMS over real BLE (no SB20 needed). Two modes:

  --mode server   (default)  the ESP32 runs the FTMS trainer-server (esp32c3-ftms-server);
                  the HOST is the FTMS controller (bleak central): Request Control -> Start
                  -> Set Target Power, and we assert the board ACKs it, emits a "Target Power
                  Changed" status, and streams Indoor Bike Data. The full erg round-trip,
                  ESP <-> host.

Connects by advertised name (default SB20-FTMS-Server). Writes a JSONL record of what
crossed the air to findings/captures/ (the canonical record). Non-blocking: bounded
timeouts + a clean PASS/FAIL exit code so an autonomous run is never stuck.

  python code/scripts/ftms_hw_loop.py --mode server --set 225 \
      --output code/findings/captures/F-ftms-hwloop-server-<ts>.jsonl
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
import time
from pathlib import Path

try:
    from bleak import BleakClient, BleakScanner
except ImportError as e:
    print(f"bleak not installed: {e}", file=sys.stderr)
    sys.exit(2)

# import the codec the firmware mirrors
HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))
from sb20proxy.ble import ftms  # noqa: E402


def sig(short: int) -> str:
    return f"0000{short:04x}-0000-1000-8000-00805f9b34fb"


U_IBD = sig(ftms.UUID_INDOOR_BIKE_DATA)
U_STATUS = sig(ftms.UUID_FTMS_STATUS)
U_CP = sig(ftms.UUID_FTMS_CONTROL_POINT)
U_FEATURE = sig(ftms.UUID_FTMS_FEATURE)
U_POWER_RANGE = sig(ftms.UUID_SUPPORTED_POWER_RANGE)


class Recorder:
    def __init__(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        self._fp = open(path, "w", buffering=1, encoding="utf-8")
        self._t0 = time.monotonic()

    def log(self, kind: str, **fields) -> None:
        self._fp.write(json.dumps({
            "iso_time": time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime()),
            "monotonic_s": round(time.monotonic() - self._t0, 6),
            "kind": kind, **fields,
        }) + "\n")

    def close(self) -> None:
        self._fp.close()


async def server_seam_test(name: str, set_w: int, rec: Recorder) -> bool:
    print(f"scanning for FTMS server {name!r} ...")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, adv: name.lower() in ((adv.local_name or d.name or "").lower()),
        timeout=12.0)
    if dev is None:
        print("FAIL: FTMS server not found", file=sys.stderr)
        rec.log("scan_result", found=False, name=name)
        return False
    rec.log("scan_result", found=True, name=name, address=dev.address)
    print(f"connecting to {dev.address} ...")

    got: dict = {}

    def on_ibd(_c, data: bytearray) -> None:
        d = ftms.decode_indoor_bike_data(bytes(data))
        got["ibd_power"] = d.power_w
        got["ibd_cadence"] = d.cadence_rpm
        rec.log("ble_notification", char="indoor_bike_data", raw_hex=bytes(data).hex(),
                power_w=d.power_w, cadence_rpm=d.cadence_rpm)

    def on_status(_c, data: bytearray) -> None:
        s = ftms.decode_fitness_machine_status(bytes(data))
        if s and s.target_power_w is not None:
            got["status_target"] = s.target_power_w
        rec.log("ble_notification", char="fitness_machine_status", raw_hex=bytes(data).hex())

    def on_cp(_c, data: bytearray) -> None:
        m = ftms.decode_control_point(bytes(data))
        if isinstance(m, ftms.ControlPointResponse):
            got.setdefault("cp_results", []).append((m.request_opcode, m.result))
        rec.log("ble_notification", char="control_point", raw_hex=bytes(data).hex())

    async with BleakClient(dev, timeout=20.0) as client:
        rec.log("ble_connect", address=dev.address)
        # static reads
        try:
            feat = ftms.decode_fitness_machine_feature(await client.read_gatt_char(U_FEATURE))
            got["erg_capable"] = feat.power_target_setting
            rec.log("ble_read", char="feature", power_target_setting=feat.power_target_setting)
        except Exception as e:  # noqa: BLE001
            rec.log("ble_error", phase="read_feature", error=str(e))
        try:
            pr = ftms.decode_supported_power_range(await client.read_gatt_char(U_POWER_RANGE))
            rec.log("ble_read", char="power_range", min=pr.minimum, max=pr.maximum,
                    inc=pr.increment)
        except Exception as e:  # noqa: BLE001
            rec.log("ble_error", phase="read_power_range", error=str(e))

        await client.start_notify(U_IBD, on_ibd)
        await client.start_notify(U_STATUS, on_status)
        await client.start_notify(U_CP, on_cp)
        await asyncio.sleep(1.5)  # collect a few Indoor Bike Data frames

        # drive erg: Request Control -> Start -> Set Target Power
        for cmd, note in [(ftms.encode_request_control(), "request_control"),
                          (ftms.encode_start(), "start"),
                          (ftms.encode_set_target_power(set_w), f"set_target_power={set_w}")]:
            rec.log("ble_cp_write", note=note, raw_hex=cmd.hex())
            await client.write_gatt_char(U_CP, cmd, response=True)
            await asyncio.sleep(0.6)
        await asyncio.sleep(1.0)

    # assertions
    cp_ok = all(r == ftms.CP_SUCCESS for _op, r in got.get("cp_results", [(0, 0xFF)]))
    status_ok = got.get("status_target") == set_w
    ibd_ok = got.get("ibd_power") is not None
    rec.log("result", cp_results=got.get("cp_results"), status_target=got.get("status_target"),
            ibd_power=got.get("ibd_power"), erg_capable=got.get("erg_capable"),
            cp_ok=cp_ok, status_ok=status_ok, ibd_ok=ibd_ok)
    print(f"  control-point ACKs: {got.get('cp_results')}")
    print(f"  status Target Power Changed -> {got.get('status_target')} W (wanted {set_w})")
    print(f"  Indoor Bike Data power: {got.get('ibd_power')} W  cadence: {got.get('ibd_cadence')}")
    print(f"  erg-capable (Feature): {got.get('erg_capable')}")
    ok = cp_ok and status_ok and ibd_ok
    print("PASS" if ok else "FAIL")
    return ok


async def main_async(args: argparse.Namespace) -> int:
    rec = Recorder(Path(args.output))
    try:
        if args.mode == "server":
            ok = await server_seam_test(args.name, args.set, rec)
        else:
            print(f"unknown mode {args.mode}", file=sys.stderr)
            ok = False
    finally:
        rec.close()
        print(f"wrote {args.output}")
    return 0 if ok else 1


def main() -> int:
    p = argparse.ArgumentParser(description="Real on-air FTMS validation against the ESP32")
    p.add_argument("--mode", default="server", choices=["server"])
    p.add_argument("--name", default="SB20-FTMS-Server", help="advertised name to connect to")
    p.add_argument("--set", type=int, default=225, help="target watts to command")
    p.add_argument("--output", required=True, help="JSONL record path")
    args = p.parse_args()
    try:
        return asyncio.run(main_async(args))
    except Exception as e:  # noqa: BLE001 — never hang an autonomous run
        print(f"FAIL: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
