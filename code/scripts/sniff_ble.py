#!/usr/bin/env python3
"""Headless nRF Sniffer capture — follow a BLE device, write a .pcap. No Wireshark.

Drives Nordic's ``SnifferAPI`` directly (the same library Wireshark's nRF Sniffer
extcap uses) so a bike session can capture the **Stages-app ↔ SB20** conversation
hands-free:

    scan  →  match the target by MAC  →  follow()  →  stream packets to a .pcap

The output is a classic pcap with link-type ``LINKTYPE_NORDIC_BLE`` (272) — open it
straight in Wireshark, or commit it to ``findings/captures/`` as the canonical record.
Time-bounded with a hard ceiling and a clean shutdown (it will not hang).

Prereqs (see ``findings/nrf-sniffer.md``): the dongle runs the **sniffer firmware**
(USB PID ``522A``), ``pyserial`` is installed, and the nRF Sniffer **extcap is staged**
(we borrow its bundled ``SnifferAPI`` — the version must match the firmware). We do
*not* vendor SnifferAPI (it's Nordic-licensed); we add the extcap dir to ``sys.path``.

Examples
--------
    # follow the SB20 for 5 min, auto-detect the dongle's COM port
    python scripts/sniff_ble.py --device E4:AA:5A:D6:0E:D4 --duration 300 \
        --output findings/captures/SNIFF-sb20-app-$(date +%Y%m%d-%H%M).pcap

    # just list what the sniffer can see (which COM port, which advertisers), then exit
    python scripts/sniff_ble.py --scan-only --duration 12
"""

from __future__ import annotations

import argparse
import os
import sys
import time

# the pure address helpers live in the package (host-tested); the SnifferAPI I/O is here
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))
from sb20proxy.ble.sniffer import address_matches, format_mac  # noqa: E402

SNIFFER_PID = 0x522A  # USB PID of the nRF52840 running the sniffer app
NORDIC_VID = 0x1915


def add_snifferapi_to_path(extcap_dir: str | None) -> str:
    """Find the staged ``SnifferAPI`` and put its parent on ``sys.path``. Exit if absent."""
    candidates: list[str] = []
    if extcap_dir:
        candidates.append(extcap_dir)
    appdata = os.environ.get("APPDATA")
    if appdata:
        candidates.append(os.path.join(appdata, "Wireshark", "extcap"))
    candidates.append(os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"),
                                   "Wireshark", "extcap"))
    for cand in candidates:
        if cand and os.path.isdir(os.path.join(cand, "SnifferAPI")):
            sys.path.insert(0, cand)
            return cand
    sys.exit(
        "ERROR: could not find the nRF Sniffer extcap (SnifferAPI). Looked in:\n  "
        + "\n  ".join(candidates)
        + "\nStage the v4.1.1 extcap (see findings/nrf-sniffer.md) or pass --extcap-dir."
    )


def autodetect_port() -> str | None:
    """Return the COM port of the dongle running the sniffer firmware (PID 522A), if any."""
    try:
        from serial.tools import list_ports
    except ImportError:
        return None
    for p in list_ports.comports():
        if p.vid == NORDIC_VID and p.pid == SNIFFER_PID:
            return p.device
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--device", help="target BLE MAC to follow, e.g. E4:AA:5A:D6:0E:D4 "
                    "(omit to capture advertising from everything)")
    ap.add_argument("--output", help="output .pcap path (required unless --scan-only)")
    ap.add_argument("--duration", type=float, default=300.0,
                    help="seconds to capture (default 300)")
    ap.add_argument("--port", help="dongle COM port (default: auto-detect PID 522A)")
    ap.add_argument("--extcap-dir", help="dir containing SnifferAPI (default: Wireshark extcap)")
    ap.add_argument("--follow-timeout", type=float, default=60.0,
                    help="seconds to wait for the target to appear before giving up the "
                    "follow and just logging adverts (default 60)")
    ap.add_argument("--scan-only", action="store_true",
                    help="list the sniffer port + advertisers seen, then exit (no .pcap)")
    ap.add_argument("--max-duration", type=float, default=3600.0,
                    help="hard ceiling on --duration, anti-hang (default 3600)")
    args = ap.parse_args()

    duration = min(args.duration, args.max_duration)
    if not args.scan_only and not args.output:
        ap.error("--output is required unless --scan-only")

    add_snifferapi_to_path(args.extcap_dir)
    from SnifferAPI import Pcap, Sniffer, UART  # noqa: E402

    port = args.port or autodetect_port()
    if not port:
        print("ERROR: no sniffer dongle found (USB PID 522A). Is it plugged in and "
              "flashed with the sniffer firmware? Pass --port COMxx to override.",
              file=sys.stderr)
        return 2
    try:
        baud = UART.find_sniffer_baudrates(port)["default"]
    except Exception:  # noqa: BLE001 — fall back to the v4 default if probing fails
        baud = 1000000
    print(f"sniffer on {port} @ {baud} baud", file=sys.stderr)

    out = None
    written = [0]
    if not args.scan_only:
        out = open(args.output, "wb", 0)
        out.write(Pcap.get_global_header())

    def on_packet(notification) -> None:
        pkt = notification.msg["packet"]
        if out is not None:
            payload = bytes([pkt.boardId] + pkt.getList())
            out.write(Pcap.create_packet(payload, pkt.time))
            written[0] += 1

    sniffer = Sniffer.Sniffer(port, baud)
    sniffer.subscribe("NEW_BLE_PACKET", on_packet)
    sniffer.setAdvHopSequence([37, 38, 39])
    sniffer.setSupportedProtocolVersion(2)  # 'None' extcap version → protocol v2
    sniffer.start()
    sniffer.scan()

    followed = False
    try:
        if args.scan_only:
            print(f"scanning for {duration:.0f}s …", file=sys.stderr)
            end = time.monotonic() + duration
            seen: dict[str, str] = {}
            while time.monotonic() < end:
                for dev in sniffer.getDevices().asList():
                    try:
                        mac = format_mac(dev.address)
                    except Exception:  # noqa: BLE001
                        continue
                    seen[mac] = f"{dev.name!r:24}  {dev.RSSI} dBm"
                time.sleep(0.5)
            print(f"\n{len(seen)} advertiser(s) seen:")
            for mac, info in sorted(seen.items()):
                print(f"  {mac}  {info}")
            return 0

        if args.device:
            print(f"waiting up to {args.follow_timeout:.0f}s for {args.device} …",
                  file=sys.stderr)
            deadline = time.monotonic() + args.follow_timeout
            while time.monotonic() < deadline and not followed:
                for dev in sniffer.getDevices().asList():
                    if address_matches(dev.address, args.device):
                        sniffer.follow(dev)
                        followed = True
                        print(f"FOLLOWING {args.device} ({dev.name!r})", file=sys.stderr)
                        break
                time.sleep(0.2)
            if not followed:
                print(f"WARNING: {args.device} not seen in {args.follow_timeout:.0f}s — "
                      "capturing advertising only (connection data will be missed). Is the "
                      "device advertising / in range?", file=sys.stderr)

        end = time.monotonic() + duration
        next_tick = time.monotonic() + 10
        while time.monotonic() < end:
            time.sleep(0.5)
            if time.monotonic() >= next_tick:
                print(f"  …{written[0]} packets, {int(end - time.monotonic())}s left",
                      file=sys.stderr)
                next_tick += 10
    except KeyboardInterrupt:
        print("interrupted — closing capture cleanly", file=sys.stderr)
    finally:
        sniffer.doExit()
        if out is not None:
            out.close()

    if out is not None:
        print(f"\nwrote {written[0]} packets -> {args.output}"
              + ("" if followed else "  (advertising only - target not followed)"))
        if written[0] == 0:
            print("NOTE: 0 packets - check the dongle, range, and that BLE traffic exists.",
                  file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
