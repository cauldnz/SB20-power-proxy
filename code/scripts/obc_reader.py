#!/usr/bin/env python3
"""obc_reader.py — the turnkey OpenBikeControl (OBC) observer for a bike session.

A BLE central that connects to our ESP32/nRF OBC device, subscribes to its OBC Button-State
characteristic, and prints each decoded button press/release live. This is the *observer* a bike
session needs to verify the OBC output — of a virtual /obc/press (Devmode) or a real SB20 shifter
press (obcSinkShifter). Mirrors the firmware's lib/proxy/Obc.h wire format byte-for-byte.

    # prove the decoder with no hardware (canned real-shape vectors):
    python code/scripts/obc_reader.py --self-test

    # watch a live board (default: find it by the Devmode advert name "OBC-SB20"):
    python code/scripts/obc_reader.py
    python code/scripts/obc_reader.py --name OBC-SB20 --timeout 20
    python code/scripts/obc_reader.py --address AA:BB:CC:DD:EE:FF   # pin it if the name varies

Ctrl-C to stop. Prints one line per state change: `<t> BUTTON <name> (0x30)  PRESSED`.
"""

from __future__ import annotations

import argparse
import asyncio
import sys
import time

# OBC BLE UUIDs (lib/proxy/Obc.h — the same constants the firmware advertises).
OBC_SERVICE_UUID = "d273f680-d548-419d-b9d1-fa0472345229"
OBC_BUTTON_UUID = "d273f681-d548-419d-b9d1-fa0472345229"
OBC_MSG_BUTTON_STATE = 0x01

# OBC standard button ids -> human names (Obc.h). Unknown ids print as "id 0xNN".
OBC_NAMES = {
    0x01: "Shift Up", 0x02: "Shift Down", 0x03: "Gear Set",
    0x10: "Nav Up", 0x11: "Nav Down", 0x12: "Nav Left", 0x13: "Nav Right",
    0x14: "Select", 0x15: "Back", 0x16: "Menu", 0x18: "Steer Left", 0x19: "Steer Right",
    0x20: "Emote", 0x30: "ERG Up", 0x31: "ERG Down", 0x32: "Skip",
    0x33: "Pause", 0x34: "Resume", 0x35: "Lap", 0x38: "Change Mode",
}


def decode_button_state(data: bytes) -> list[tuple[int, str, int]]:
    """Decode an OBC Button-State frame -> [(id, name, state), ...]. state 0x00 released / else pressed.

    Wire form (Obc.h): [0x01, id, state, id, state, ...]. Non-Button-State or short frames -> [].
    Pure — the --self-test exercises this with no radio.
    """
    if len(data) < 1 or data[0] != OBC_MSG_BUTTON_STATE:
        return []
    out: list[tuple[int, str, int]] = []
    i = 1
    while i + 1 < len(data):
        bid, state = data[i], data[i + 1]
        out.append((bid, OBC_NAMES.get(bid, f"id 0x{bid:02X}"), state))
        i += 2
    return out


def _fmt(actions: list[tuple[int, str, int]]) -> str:
    parts = [f"{name} (0x{bid:02X})  {'PRESSED' if state else 'released'}"
             for bid, name, state in actions]
    return " · ".join(parts)


def self_test() -> int:
    """Decode canned frames matching the firmware's golden vectors (encodeSb20ButtonState etc.)."""
    cases: list[tuple[bytes, list[tuple[int, str, int]]]] = [
        # LEFT up default-bound to Shift Up: pressed then released (a momentary click)
        (bytes([0x01, 0x01, 0x01]), [(0x01, "Shift Up", 1)]),
        (bytes([0x01, 0x01, 0x00]), [(0x01, "Shift Up", 0)]),
        # ERG Up via /obc/press?id=0x30
        (bytes([0x01, 0x30, 0x01]), [(0x30, "ERG Up", 1)]),
        # LEFT 3rd -> Lap
        (bytes([0x01, 0x35, 0x01]), [(0x35, "Lap", 1)]),
        # multi-action frame (shift + erg on one button)
        (bytes([0x01, 0x01, 0x01, 0x30, 0x01]), [(0x01, "Shift Up", 1), (0x30, "ERG Up", 1)]),
        # not a Button-State message / too short -> nothing
        (bytes([0x02, 0x01, 0x01]), []),
        (bytes([0x01]), []),
    ]
    ok = True
    for raw, want in cases:
        got = decode_button_state(raw)
        status = "ok " if got == want else "FAIL"
        if got != want:
            ok = False
        print(f"  [{status}] {raw.hex(' '):<20} -> {_fmt(got) or '(none)'}")
    print("SELF-TEST PASS" if ok else "SELF-TEST FAILED")
    return 0 if ok else 1


async def _find(name: str, timeout: float) -> str | None:
    """Scan for the OBC device by advert name substring, or by the advertised OBC service UUID."""
    from bleak import BleakScanner

    print(f"scanning {timeout:.0f}s for '{name}' (or OBC service {OBC_SERVICE_UUID[:8]}…)…")
    devs = await BleakScanner.discover(timeout=timeout, return_adv=True)
    seen = []
    for addr, (d, adv) in devs.items():
        nm = adv.local_name or d.name or ""
        if nm:
            seen.append(nm)
        uuids = [u.lower() for u in (adv.service_uuids or [])]
        if (name and name.lower() in nm.lower()) or OBC_SERVICE_UUID.lower() in uuids:
            print(f"found '{nm or '(no name)'}' @ {addr}")
            return addr
    print(f"  not found. saw: {', '.join(sorted(set(seen))) or '(nothing named)'}")
    return None


async def _watch(address: str) -> None:
    """Connect + subscribe + print each state change until Ctrl-C. De-dups repeat notifications."""
    from bleak import BleakClient

    last: dict[int, int] = {}

    def on_notify(_char, data: bytearray) -> None:
        actions = decode_button_state(bytes(data))
        fresh = [(bid, nm, st) for bid, nm, st in actions if last.get(bid) != st]
        for bid, _nm, st in actions:
            last[bid] = st
        if fresh:
            print(f"{time.strftime('%H:%M:%S')}  BUTTON  {_fmt(fresh)}")

    async with BleakClient(address) as client:
        print(f"connected {address} — subscribing to OBC Button-State. Ctrl-C to stop.\n")
        await client.start_notify(OBC_BUTTON_UUID, on_notify)
        try:
            while client.is_connected:
                await asyncio.sleep(1.0)
        finally:
            try:
                await client.stop_notify(OBC_BUTTON_UUID)
            except Exception:  # noqa: BLE001 — already disconnecting
                pass
    print("\ndisconnected.")


async def _run(args: argparse.Namespace) -> int:
    address = args.address or await _find(args.name, args.timeout)
    if not address:
        return 2
    try:
        await _watch(address)
    except KeyboardInterrupt:
        pass
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description="Live OpenBikeControl button-press observer",
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--name", default="OBC-SB20", help="advert-name substring to find (default OBC-SB20)")
    p.add_argument("--address", help="pin the device by BLE address (skips the scan)")
    p.add_argument("--timeout", type=float, default=15.0, help="scan timeout seconds")
    p.add_argument("--self-test", action="store_true", help="decode canned frames + exit (no radio)")
    args = p.parse_args()
    if args.self_test:
        return self_test()
    try:
        return asyncio.run(_run(args))
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
