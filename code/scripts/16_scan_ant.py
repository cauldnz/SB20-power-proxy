#!/usr/bin/env python3
"""ANT+ scan diagnostic — list EVERY ANT+ device on air (id, type, transmission),
with no device IDs known up front.

Continuous RX-scan mode on one stick (openant's ``Scanner``). Two uses:
  1. **Pre-ride smoke test / ID confirmation** — run ~30 s with the bike + meters on
     to confirm the Assioma (17039), the crank IDs, the HR id, and catch *anything
     else blowing around* before launching the targeted ``07_capture_multi`` capture.
  2. A raw catch-all snapshot of the ANT+ space.

Prints a live "FOUND" line per new device + a final table; optionally logs to JSONL.
Windows: the libusb backend is wired the same way as ``07_capture_multi.py``.

    python 16_scan_ant.py --duration 30
    python 16_scan_ant.py --duration 30 --output ../findings/captures/SCAN-ant-20260622.jsonl
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import threading
import time
from pathlib import Path

# openant's USB driver needs pyusb's libusb backend; on Windows wire up libusb-package.
try:
    import libusb_package
    libusb_package.get_libusb1_backend()
except Exception:  # noqa: BLE001 — Linux/WSL use system libusb
    pass

try:
    from openant.devices import ANTPLUS_NETWORK_KEY
    from openant.devices.common import DeviceType
    from openant.devices.scanner import Scanner
    from openant.easy.node import Node
except ImportError as e:
    print(f"openant import failed: {e}", file=sys.stderr)
    sys.exit(1)


def _type_name(dtype: int) -> str:
    try:
        return DeviceType(dtype).name
    except Exception:  # noqa: BLE001 — unknown / non-standard device type
        return f"type{dtype}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--duration", type=float, default=30.0, help="seconds to scan (default 30)")
    ap.add_argument("--output", type=Path, default=None, help="optional JSONL log of found/updates")
    args = ap.parse_args()

    fp = open(args.output, "w", buffering=1) if args.output else None

    def log(kind: str, **fields) -> None:
        if fp:
            fp.write(json.dumps({"iso_time": time.strftime("%Y-%m-%dT%H:%M:%S"),
                                 "kind": kind, **fields}) + "\n")

    node = Node()
    node.set_network_key(0x00, ANTPLUS_NETWORK_KEY)
    scanner = Scanner(node, device_id=0, device_type=0)  # 0/0 = every device
    found: dict[int, tuple] = {}
    log("scan_start", duration_s=args.duration)

    def on_found(device_tuple) -> None:
        did, dtype, dtrans = device_tuple
        found[did] = (dtype, _type_name(dtype), dtrans)
        print(f"  FOUND  #{did:<7} type={dtype:<3} ({_type_name(dtype)})  trans={dtrans}", flush=True)
        log("found", device_id=did, device_type=dtype, type_name=_type_name(dtype), trans_type=dtrans)

    def on_update(device_tuple, common) -> None:
        log("update", device_id=device_tuple[0], common=str(common))

    scanner.on_found = on_found
    scanner.on_update = on_update

    print(f"Scanning ALL ANT+ for {args.duration:.0f}s (Ctrl-C to stop early)...", flush=True)

    def _expire() -> None:
        try:
            node.stop()  # unblocks node.start()
        except Exception:  # noqa: BLE001
            pass
    timer = threading.Timer(args.duration, _expire)
    timer.daemon = True
    timer.start()
    try:
        node.start()
    except KeyboardInterrupt:
        pass
    finally:
        timer.cancel()
        try:
            scanner.close_channel()
        except Exception:  # noqa: BLE001
            pass
        try:
            node.stop()
        except Exception:  # noqa: BLE001
            pass

    summary = ", ".join(f"#{k}({v[1]})" for k, v in sorted(found.items())) or "(none)"
    print(f"\n{len(found)} ANT+ device(s) seen: {summary}", flush=True)
    log("scan_end", n_devices=len(found), devices=[
        {"device_id": k, "device_type": v[0], "type_name": v[1], "trans_type": v[2]}
        for k, v in sorted(found.items())])
    if fp:
        fp.close()
    sys.stdout.flush()
    os._exit(0)  # avoid hanging on lingering openant threads


if __name__ == "__main__":
    main()
