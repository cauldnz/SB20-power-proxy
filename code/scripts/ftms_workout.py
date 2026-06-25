#!/usr/bin/env python3
"""ftms_workout.py — drive the Stages SB20 through a structured interval workout over FTMS erg.

The on-bike companion to the erg work: instead of a single Set-Target-Power (capture_ftms.py
--erg) or a Ride-Director-fed setpoint (ftms_erg.FtmsErgSession), this walks a fixed structured
plan — warmup → [interval, recovery] xN → cooldown — commanding the SB20's resistance segment by
segment and printing live power/cadence each second.

REUSES the validated FTMS codec (`sb20proxy.ble.ftms`) and the pure `ErgController` state machine
(`sb20proxy.ble.ftms_erg`) — it does NOT re-encode control-point bytes by hand. The connect /
subscribe / control-point pattern mirrors `ftms_hw_loop.py`; the handshake (Request Control 0x00 →
Start 0x07 → Set Target Power 0x05) is byte-confirmed against the real capture
`code/findings/captures/G-sb20-ftms-erg-20260621-0949.jsonl`.

SAFETY (the rider is physically on the bike): the whole drive is wrapped in try/finally so that on
normal completion AND on Ctrl-C / ANY exception we ALWAYS send Reset (0x01) — the capture's own
teardown op — to release control and return resistance to neutral. The bike is NEVER left stuck at
the interval target.

Usage (PowerShell / native, the bike machine; WSL has no Bluetooth):
    # the default 6 x 90 s @ 430 W workout
    python code/scripts/ftms_workout.py

    # preview the plan only — no BLE
    python code/scripts/ftms_workout.py --dry-run

    # a custom session
    python code/scripts/ftms_workout.py --reps 4 --interval-secs 60 --interval-watts 380

References: FTMS service 0x1826; Control Point 0x2AD9 (Request Control 0x00, Reset 0x01,
Set Target Power 0x05 + sint16 LE watts, Start/Resume 0x07, Stop/Pause 0x08); responses are
0x80 <req-op> <result> indications. Indoor Bike Data 0x2AD2 carries live power/cadence.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

# Make the package importable when run straight from the repo (mirrors ftms_hw_loop.py).
HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "src"
if str(SRC) not in sys.path:
    sys.path.insert(0, str(SRC))

# Codec only — pure, no hardware. bleak is imported lazily inside the async driver so this module
# (and the segment-plan builder the tests exercise) imports clean without it.
from sb20proxy.ble import ftms  # noqa: E402

# The SB20's FTMS identity (G-sb20-ftms-erg-20260621-0949.jsonl recon).
DEFAULT_DEVICE_NAME = "Stages Bike 0105"
DEFAULT_ADDRESS = "E4:AA:5A:D6:0E:D4"
DEFAULT_STATUS_URL = "http://192.168.1.165/status"


# ============================ the pure plan (host-tested) ============================

@dataclass(frozen=True)
class Segment:
    """One block of the workout: a label, a target power, and how long to hold it."""

    label: str
    watts: int
    secs: int


def build_segments(
    *,
    reps: int = 6,
    interval_secs: int = 90,
    interval_watts: int = 430,
    recovery_secs: int = 180,
    recovery_watts: int = 100,
    warmup_secs: int = 180,
    warmup_watts: int = 130,
    cooldown_secs: int = 120,
    cooldown_watts: int = 100,
) -> list[Segment]:
    """Build the ordered segment list for a structured interval workout.

    Shape: WARMUP, then for each of `reps`: INTERVAL i/reps, then RECOVERY i/reps — EXCEPT the
    final recovery is replaced by the COOLDOWN (so the last hard effort is followed by the
    cooldown, not a recovery-then-cooldown). Segments with non-positive duration are dropped, so
    e.g. `--warmup-secs 0` cleanly omits the warmup.

    Pure: inputs -> ordered list of Segment(label, watts, secs); no I/O. Total ride time is
    `sum(s.secs for s in build_segments(...))`.
    """
    segs: list[Segment] = []
    if warmup_secs > 0:
        segs.append(Segment("WARMUP", warmup_watts, warmup_secs))
    for i in range(1, reps + 1):
        if interval_secs > 0:
            segs.append(Segment(f"INTERVAL {i}/{reps}", interval_watts, interval_secs))
        is_last = i == reps
        if is_last:
            if cooldown_secs > 0:
                segs.append(Segment("COOLDOWN", cooldown_watts, cooldown_secs))
        elif recovery_secs > 0:
            segs.append(Segment(f"RECOVERY {i}/{reps}", recovery_watts, recovery_secs))
    return segs


def total_secs(segs: list[Segment]) -> int:
    return sum(s.secs for s in segs)


def fmt_mmss(secs: int) -> str:
    secs = max(0, int(secs))
    return f"{secs // 60:d}:{secs % 60:02d}"


def format_plan(segs: list[Segment]) -> str:
    """A human-readable plan table — used by --dry-run and printed before the drive starts."""
    lines = ["", "  #  segment        target    duration", "  " + "-" * 42]
    elapsed = 0
    for idx, s in enumerate(segs, 1):
        elapsed += s.secs
        lines.append(
            f"  {idx:>2} {s.label:<14} {s.watts:>4} W   "
            f"{fmt_mmss(s.secs):>5} s   (@{fmt_mmss(elapsed)})"
        )
    lines.append("  " + "-" * 42)
    lines.append(f"  total: {len(segs)} segments, {fmt_mmss(total_secs(segs))} "
                 f"({total_secs(segs)} s)")
    lines.append("")
    return "\n".join(lines)


# ============================ live data: FTMS IBD + /status fallback ============================

class LiveData:
    """Last-known live power/cadence. Indoor Bike Data interleaves frame shapes (the real SB20
    emits flags 0x00c5 power+cadence frames, 0x0011 distance-only frames, 0x0000 speed-only
    frames — see the capture), so we only overwrite a field when a frame actually carries it,
    holding the previous value otherwise."""

    def __init__(self) -> None:
        self.power_w: int | None = None
        self.cadence_rpm: float | None = None
        self.source = "—"

    def on_ibd(self, data: bytes) -> None:
        try:
            d = ftms.decode_indoor_bike_data(data)
        except ValueError:
            return
        if d.power_w is not None:
            self.power_w = d.power_w
        if d.cadence_rpm is not None:
            self.cadence_rpm = d.cadence_rpm
        self.source = "ibd"

    def poll_status(self, url: str) -> None:
        """Best-effort fallback: pull power/cadence from the board's /status JSON
        (src_power_w / power_w / cadence_rpm)."""
        import json
        import urllib.request

        try:
            with urllib.request.urlopen(url, timeout=1.0) as r:  # noqa: S310 — LAN device
                j = json.loads(r.read() or b"{}")
        except Exception:  # noqa: BLE001 — fallback is best-effort, never fatal
            return
        p = j.get("src_power_w", j.get("power_w"))
        if p is not None:
            self.power_w = p
        c = j.get("cadence_rpm", j.get("src_cadence_rpm"))
        if c is not None:
            self.cadence_rpm = c
        self.source = "http"


# ============================ the on-air driver (needs the bike) ============================

def _sig(short: int) -> str:
    return f"0000{short:04x}-0000-1000-8000-00805f9b34fb"


async def _pump(controller, transport, *, max_steps: int = 12) -> int | None:
    """Async equivalent of ftms_erg.drive(): pump the controller's commands against an
    ASYNC transport until it converges (or max_steps), awaiting each write and feeding the
    machine's response back in. Returns the controller's last result code, or None.

    ftms_erg.drive() is synchronous — it calls transport(cmd) without awaiting, which can't
    work with a bleak write. This mirrors the proven on-bike pump in FtmsErgSession.run()."""
    for _ in range(max_steps):
        cmd = controller.next_command()
        if cmd is None:
            break
        reply = await transport(cmd)
        if reply:
            msg = ftms.decode_control_point(reply)
            if isinstance(msg, ftms.ControlPointResponse):
                controller.on_response(msg)
    return controller.last_result


async def run_workout(args: argparse.Namespace, segs: list[Segment]) -> int:
    """Connect to the SB20, claim erg control, walk the segments, and ALWAYS release control
    (Reset) on the way out. Returns a process exit code (0 ok)."""
    import asyncio

    from bleak import BleakClient, BleakScanner  # lazy: only needed on the bike

    from sb20proxy.ble.ftms_erg import ErgController

    u_ibd = _sig(ftms.UUID_INDOOR_BIKE_DATA)
    u_cp = _sig(ftms.UUID_FTMS_CONTROL_POINT)
    u_power_range = _sig(ftms.UUID_SUPPORTED_POWER_RANGE)

    # Resolve the target: explicit address wins; otherwise scan by name (fall back to the
    # known default address if the scan finds nothing).
    target: str = args.address
    if not target and args.device:
        print(f"scanning for FTMS device {args.device!r} ...")
        dev = await BleakScanner.find_device_by_filter(
            lambda d, adv: args.device.lower() in ((adv.local_name or d.name or "").lower()),
            timeout=args.scan_time)
        if dev is not None:
            target = dev.address
            print(f"  found {dev.address}")
    if not target:
        target = DEFAULT_ADDRESS
        print(f"  no scan match — trying default address {target}")

    live = LiveData()
    last_indication: dict[str, bytes] = {"v": b""}
    controller = ErgController()

    def on_ibd(_c, data: bytearray) -> None:
        live.on_ibd(bytes(data))

    def on_cp(_c, data: bytearray) -> None:
        last_indication["v"] = bytes(data)

    print(f"connecting to {target} ...")
    async with BleakClient(target, timeout=20.0) as client:
        print("connected.")

        # Read the Supported Power Range so the controller clamps correctly (SB20: 0..4000 W).
        try:
            raw = await client.read_gatt_char(u_power_range)
            pr = ftms.decode_supported_power_range(raw)
            controller.power_range = pr
            print(f"  supported power range: {pr.minimum}..{pr.maximum} W (step {pr.increment})")
        except Exception as e:  # noqa: BLE001 — range read is best-effort
            print(f"  (power-range read failed: {e}; not clamping)")

        await client.start_notify(u_cp, on_cp)        # control-point indications
        try:
            await client.start_notify(u_ibd, on_ibd)  # live power/cadence
        except Exception as e:  # noqa: BLE001 — fall back to /status polling
            print(f"  (Indoor Bike Data subscribe failed: {e}; will poll {args.status_url})")

        async def transport(cmd: bytes) -> bytes:
            last_indication["v"] = b""
            await client.write_gatt_char(u_cp, cmd, response=True)
            await asyncio.sleep(0.3)  # let the indication land
            return last_indication["v"]

        overall_left = total_secs(segs)

        try:
            # Claim control + start before the first target (Request Control 0x00 -> Start 0x07).
            print("requesting control + starting (Request Control -> Start) ...")
            controller.set_desired(segs[0].watts if segs else 0)
            await _pump(controller, transport)
            if not controller.controlled:
                print("WARNING: SB20 did not grant control "
                      f"(last result {controller.last_result}); aborting.", file=sys.stderr)
                return 1

            for seg in segs:
                controller.set_desired(seg.watts)
                res = await _pump(controller, transport)
                ok = res == ftms.CP_SUCCESS
                print(f"\n>>> {seg.label}: target {seg.watts} W for {fmt_mmss(seg.secs)} "
                      f"(set {'OK' if ok else 'result ' + str(res)})")
                for remaining in range(seg.secs, 0, -1):
                    # refresh live data from /status only if IBD isn't flowing
                    if live.source != "ibd" and args.status_url:
                        live.poll_status(args.status_url)
                    pw = "—" if live.power_w is None else f"{live.power_w} W"
                    cad = "—" if live.cadence_rpm is None else f"{live.cadence_rpm:.0f} rpm"
                    sys.stdout.write(
                        f"\r  {seg.label:<14} tgt {seg.watts:>4} W | "
                        f"seg {fmt_mmss(remaining):>5} | total {fmt_mmss(overall_left):>5} | "
                        f"live {pw:>6} {cad:>8} [{live.source}]   "
                    )
                    sys.stdout.flush()
                    await asyncio.sleep(1.0)
                    overall_left -= 1
                print()  # end the carriage-return line for this segment
            print("\nworkout complete.")
            return 0
        finally:
            # ALWAYS hand the bike back, however we got here (done / Ctrl-C / exception).
            print("releasing control (Reset) + returning resistance to neutral ...")
            try:
                await client.write_gatt_char(u_cp, ftms.encode_reset(), response=True)
                await asyncio.sleep(0.3)
                # Belt-and-braces: also Stop, in case a machine ignores Reset.
                await client.write_gatt_char(u_cp, ftms.encode_stop(), response=True)
                await asyncio.sleep(0.2)
                print("  control released.")
            except Exception as e:  # noqa: BLE001 — we're already tearing down
                print(f"  WARNING: failed to send Reset/Stop on teardown: {e}", file=sys.stderr)


# ============================ CLI ============================

def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Drive the Stages SB20 through a structured FTMS erg interval workout.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--reps", type=int, default=6, help="number of work intervals")
    p.add_argument("--interval-secs", type=int, default=90, help="seconds per work interval")
    p.add_argument("--interval-watts", type=int, default=430, help="target W during intervals")
    p.add_argument("--recovery-secs", type=int, default=180, help="seconds per recovery")
    p.add_argument("--recovery-watts", type=int, default=100, help="target W during recovery")
    p.add_argument("--warmup-secs", type=int, default=180, help="warmup seconds (0 = none)")
    p.add_argument("--warmup-watts", type=int, default=130, help="warmup target W")
    p.add_argument("--cooldown-secs", type=int, default=120, help="cooldown seconds (0 = none)")
    p.add_argument("--cooldown-watts", type=int, default=100, help="cooldown target W")
    p.add_argument("--device", default=DEFAULT_DEVICE_NAME,
                   help="advertised-name substring to scan for")
    p.add_argument("--address", default=None,
                   help=f"connect to a specific BLE address (default {DEFAULT_ADDRESS} if scan "
                        "finds nothing)")
    p.add_argument("--scan-time", type=float, default=12.0, help="BLE scan seconds")
    p.add_argument("--status-url", default=DEFAULT_STATUS_URL,
                   help="board /status URL for live power/cadence when Indoor Bike Data is absent "
                        "(pass '' to disable)")
    p.add_argument("--dry-run", action="store_true",
                   help="print the segment plan + total duration and exit (no BLE)")
    return p.parse_args(argv)


def segments_from_args(args: argparse.Namespace) -> list[Segment]:
    return build_segments(
        reps=args.reps,
        interval_secs=args.interval_secs,
        interval_watts=args.interval_watts,
        recovery_secs=args.recovery_secs,
        recovery_watts=args.recovery_watts,
        warmup_secs=args.warmup_secs,
        warmup_watts=args.warmup_watts,
        cooldown_secs=args.cooldown_secs,
        cooldown_watts=args.cooldown_watts,
    )


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    segs = segments_from_args(args)
    if not segs:
        print("empty workout (all segments have non-positive duration).", file=sys.stderr)
        return 2

    print(f"Workout: {args.reps} x {args.interval_secs}s @ {args.interval_watts} W "
          f"(recovery {args.recovery_secs}s @ {args.recovery_watts} W)")
    print(format_plan(segs))

    if args.dry_run:
        return 0

    import asyncio
    try:
        return asyncio.run(run_workout(args, segs))
    except KeyboardInterrupt:
        # asyncio.run propagates the KeyboardInterrupt AFTER run_workout's finally has already
        # sent Reset, so the bike is safe by the time we land here.
        print("\ninterrupted — bike released.", file=sys.stderr)
        return 130
    except Exception as e:  # noqa: BLE001 — surface, don't hang an on-bike run
        print(f"\nERROR: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
