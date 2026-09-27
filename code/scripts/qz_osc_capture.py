#!/usr/bin/env python3
"""qz_osc_capture.py — record a live ride from qdomyos-zwift's OSC feed. Zero impact on the ride.

**Why this exists.** On ride day the rider's phone owns every BLE connection to the bike: QZ is the
one controlling the trainer, and anything of ours that subscribes to the crank or the bike's FTMS is
competing for a link QZ needs. This reads what QZ *already knows* instead, over UDP, one-way — QZ
never waits on us, and a dead listener cannot affect the session. It was the only instrument used on
the 2026-09-27 Peloton ride for exactly that reason (see `sessions/ride-20260927-peloton-qz.md`).

**Enabling it takes one field, and it is reachable on iOS.** QZ Settings -> *OSC Settings* ->
`OSC IP:` = this machine's LAN address, port 9000. QZ turns OSC on purely from the IP being
non-empty (`main.cpp`), and reads it at startup, so restart QZ afterwards. Note that QZ's
*Template Settings* section is **not** an alternative on iOS: it only lists user-sideloaded `.qzt`
files, so it is empty there.

**What you get, and what you don't.** 41 fields at 1 Hz: `/QZ/Watts`, `/QZ/Cadence`, `/QZ/Heart`,
`/QZ/Resistance`, `/QZ/Speed`, `/QZ/PowerZone`, `/QZ/TargetPowerZone`, distance, calories, elapsed.
You do **not** get QZ's Peloton class fields (Peloton R%, T.Power, Peloton Offset, time-remaining) —
those are computed in QZ and never reach its OSC surface. And 1 Hz sampling misses sub-second peaks:
on the 2026-09-27 ride this logged a 621 W max where QZ's own tile said 746 W. Fine for zone and
target work; not a sprint-peak instrument.

    python scripts/qz_osc_capture.py --out findings/captures/QZ-osc-<tag>-<date>.jsonl
    python scripts/qz_osc_capture.py --self-test      # no network: parse a synthetic QZ bundle

Only enough OSC 1.0 to read QZ's bundle is implemented here, deliberately: adding a dependency to
the ride machine minutes before a class is exactly the fiddling this is meant to avoid.
"""

from __future__ import annotations

import argparse
import json
import socket
import struct
import sys
import time

OSC_BUNDLE_HEADER = b"#bundle\0"
_BUNDLE_PREFIX_LEN = 16  # "#bundle\0" + a 64-bit timetag


def _pad4(n: int) -> int:
    """OSC pads every string and blob to a 4-byte boundary."""
    return (n + 3) & ~3


def read_osc_string(buf: bytes, i: int) -> tuple[str, int]:
    """Read a null-terminated, 4-byte-padded OSC string; return it and the next offset."""
    end = buf.index(b"\0", i)
    return buf[i:end].decode("ascii", "replace"), i + _pad4(end - i + 1)


def parse_message(buf: bytes) -> tuple[str, list]:
    """One OSC message -> (address, args). Unknown type tags stop the scan rather than mis-read."""
    addr, i = read_osc_string(buf, 0)
    if i >= len(buf) or buf[i:i + 1] != b",":
        return addr, []
    tags, i = read_osc_string(buf, i)
    args: list = []
    for t in tags[1:]:
        if t == "i":
            args.append(struct.unpack_from(">i", buf, i)[0])
            i += 4
        elif t == "f":
            args.append(round(struct.unpack_from(">f", buf, i)[0], 4))
            i += 4
        elif t == "s":
            s, i = read_osc_string(buf, i)
            args.append(s)
        elif t in "TF":
            args.append(t == "T")
        else:
            break
    return addr, args


def parse_packet(buf: bytes) -> dict:
    """Flatten an OSC bundle (QZ sends one per update) or a bare message into {address: value}."""
    out: dict = {}
    if buf.startswith(OSC_BUNDLE_HEADER):
        i = _BUNDLE_PREFIX_LEN
        while i + 4 <= len(buf):
            (size,) = struct.unpack_from(">i", buf, i)
            i += 4
            if size <= 0 or i + size > len(buf):
                break  # truncated datagram: keep what parsed rather than raising mid-ride
            out.update(parse_packet(buf[i:i + size]))
            i += size
    else:
        addr, args = parse_message(buf)
        out[addr] = args[0] if len(args) == 1 else args
    return out


def build_bundle(fields: list[tuple[str, str, object]]) -> bytes:
    """Build a QZ-shaped bundle. Used by --self-test and the unit tests; not on the capture path."""
    def msg(addr: str, tag: str, val) -> bytes:
        a = addr.encode() + b"\0"
        a += b"\0" * ((4 - len(a) % 4) % 4)
        t = ("," + tag).encode() + b"\0"
        t += b"\0" * ((4 - len(t) % 4) % 4)
        if tag == "f":
            v = struct.pack(">f", float(val))
        elif tag == "i":
            v = struct.pack(">i", int(val))
        else:
            v = str(val).encode() + b"\0"
            v += b"\0" * ((4 - len(v) % 4) % 4)
        return a + t + v

    out = OSC_BUNDLE_HEADER + struct.pack(">Q", 1)
    for addr, tag, val in fields:
        m = msg(addr, tag, val)
        out += struct.pack(">i", len(m)) + m
    return out


def self_test() -> int:
    got = parse_packet(build_bundle([
        ("/QZ/Watts", "f", 231.5), ("/QZ/Cadence", "f", 88.0),
        ("/QZ/PowerZone", "i", 4), ("/QZ/TargetPowerZone", "i", 5),
        ("/QZ/ElapsedTime", "s", "0:12:34"),
    ]))
    want = {"/QZ/Watts": 231.5, "/QZ/Cadence": 88.0, "/QZ/PowerZone": 4,
            "/QZ/TargetPowerZone": 5, "/QZ/ElapsedTime": "0:12:34"}
    ok = got == want
    print(("PASS " if ok else "FAIL ") + json.dumps(got))
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=9000, help="must match QZ's OSC port")
    ap.add_argument("--out", help="JSONL path (required unless --self-test)")
    ap.add_argument("--self-test", action="store_true", help="parse a synthetic bundle, no network")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if not a.out:
        ap.error("--out is required (or use --self-test)")

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", a.port))
    print(f"OSC listening on 0.0.0.0:{a.port} -> {a.out}", flush=True)

    n = 0
    seen_from = None
    with open(a.out, "a", encoding="utf-8") as f:
        while True:
            data, peer = s.recvfrom(65535)
            if seen_from != peer[0]:
                seen_from = peer[0]
                print(f"[{time.strftime('%H:%M:%S')}] QZ OSC from {peer[0]}", flush=True)
            try:
                rec = parse_packet(data)
            except Exception as e:  # noqa: BLE001 — one bad datagram must never end a ride log
                rec = {"_parse_error": str(e)[:80], "_len": len(data)}
            f.write(json.dumps({"t": round(time.time(), 2), "from": peer[0], **rec}) + "\n")
            f.flush()  # the file stays readable mid-ride, and survives a kill
            n += 1
            if n % 60 == 0:
                print(f"  {n} pkts  W={rec.get('/QZ/Watts')} cad={rec.get('/QZ/Cadence')} "
                      f"res={rec.get('/QZ/Resistance')} zone={rec.get('/QZ/PowerZone')}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
