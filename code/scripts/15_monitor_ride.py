#!/usr/bin/env python3
"""Ride-day passive monitor — launch + supervise the nRF BLE sniff and the ANT+
multi-capture together, health-check both, stop cleanly, write a manifest.

One command starts both radios. It prints a heartbeat (bytes captured + whether each
output is still growing) so the operator can CONFIRM logging before leaving the rider,
then runs to ``--duration`` and shuts both down. Time-bounded; never hangs; a dead or
stale capture is reported, not silently tolerated.

Sequencing note (the session-6 lesson): the nRF can only *follow* a connection if it
catches the CONNECT_IND, so START THIS BEFORE qdomyos/the app connect to the SB20.

    # real ride (nRF follows the SB20; ANT+ grabs Assioma + cranks + HR + the bike FE-C)
    python 15_monitor_ride.py --sb20 E4:AA:5A:D6:0E:D4 \
        --ant assioma:17039,stagesL:62144,stagesR:4963 --hr hrm --fec \
        --duration 3900 --tag ride-20260622

    # harden the launch/monitor/stop logic with no hardware:
    python 15_monitor_ride.py --self-test --duration 6 --interval 2 --out C:/tmp/montest
"""

from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_PY = sys.executable
_DEFAULT_EXTCAP = r"C:\repos\nrf52840-mdk-usb-dongle\tools\ble_sniffer\extcap"


def _iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime())


def _run_producer(path: str, duration: float) -> int:
    """Self-test fake capture: append data to ``path`` for ``duration`` s (no hardware)."""
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    binary = path.endswith(".pcap")
    end = time.monotonic() + duration
    with open(p, "ab" if binary else "a", buffering=(0 if binary else 1)) as f:
        while time.monotonic() < end:
            f.write(b"\x00" * 64 if binary else (json.dumps({"t": _iso()}) + "\n"))
            time.sleep(0.4)
    return 0


class Child:
    """A supervised capture subprocess + its growing output file."""

    def __init__(self, name: str, cmd: list[str], output: Path, env: dict | None = None):
        self.name, self.cmd, self.output, self.env = name, cmd, output, env
        self.proc: subprocess.Popen | None = None
        self._last = -1

    def start(self) -> None:
        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.proc = subprocess.Popen(self.cmd, cwd=str(_HERE), env=self.env,
                                     stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)

    def size(self) -> int:
        try:
            return self.output.stat().st_size
        except OSError:
            return -1

    def alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def tick(self) -> tuple[int, bool]:
        sz = self.size()
        grew = sz > self._last
        self._last = sz
        return sz, grew

    def stop(self) -> None:
        if self.proc and self.proc.poll() is None:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=12)
            except Exception:
                try:
                    self.proc.kill()
                except Exception:
                    pass


def build_children(args) -> list[Child]:
    out = Path(args.out)
    env = dict(os.environ, PYTHONIOENCODING="utf-8")
    if args.self_test:
        pcap = out / f"SELFTEST-ble-{args.tag}.pcap"
        jsonl = out / f"SELFTEST-ant-{args.tag}.jsonl"
        return [
            Child("ble(fake)", [_PY, str(Path(__file__)), "--_produce", str(pcap),
                                str(args.duration)], pcap),
            Child("ant(fake)", [_PY, str(Path(__file__)), "--_produce", str(jsonl),
                                str(args.duration)], jsonl),
        ]
    children: list[Child] = []
    pcap = out / f"RIDE-ble-sb20-{args.tag}.pcap"
    sniff = [_PY, "sniff_ble.py", "--device", args.sb20, "--duration", str(int(args.duration)),
             "--max-duration", str(int(args.duration) + 120), "--output", str(pcap)]
    if args.extcap_dir:
        sniff += ["--extcap-dir", args.extcap_dir]
    children.append(Child("ble-nrf", sniff, pcap, env=env))

    jsonl = out / f"RIDE-ant-{args.tag}.jsonl"
    ant = [_PY, "07_capture_multi.py", "--duration", str(int(args.duration)), "--output", str(jsonl)]
    for spec in filter(None, (s.strip() for s in args.ant.split(","))):
        ant += ["--meter", spec]
    for hr in args.hr:
        ant += ["--hr", hr]
    if args.fec:
        ant += ["--fec-id", "0"]
    children.append(Child("ant", ant, jsonl, env=env))
    return children


# ── Clean teardown on a hard stop ───────────────────────────────────────────
# Session-7 bug: a harness TaskStop SIGTERMs this orchestrator; untrapped, the
# finally never ran, so the sniff_ble children orphaned and ran on to their
# --duration (had to be killed by PID). The right defence differs by OS:
#   • POSIX/WSL: SIGTERM IS delivered to Python — trap it and raise KeyboardInterrupt
#     so the existing finally stops the children and finalises the manifest.
#   • Windows: an *external* SIGTERM is an uncatchable TerminateProcess (verified —
#     the handler never fires), so also bind the children to a kill-on-close Job
#     Object; the OS tears them down when this process dies, even on a hard kill.
_STOP_SIGNAL: str | None = None  # set by the trap; recorded in the manifest


def _install_signal_handlers() -> None:
    """Trap SIGTERM (and Windows Ctrl-Break) so a hard stop tears down exactly like
    Ctrl-C. No-op where registration isn't allowed (non-main thread / unsupported)."""
    def _trap(signum, _frame):
        global _STOP_SIGNAL
        _STOP_SIGNAL = signal.Signals(signum).name
        try:  # one-shot: a second signal during teardown should hard-stop, not loop
            signal.signal(signum, signal.SIG_DFL)
        except (ValueError, OSError):
            pass
        raise KeyboardInterrupt

    for name in ("SIGTERM", "SIGBREAK"):
        sig = getattr(signal, name, None)
        if sig is None:
            continue
        try:
            signal.signal(sig, _trap)
        except (ValueError, OSError):
            pass


if sys.platform == "win32":
    import ctypes
    from ctypes import wintypes

    class _JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
        _fields_ = [
            ("PerProcessUserTimeLimit", wintypes.LARGE_INTEGER),
            ("PerJobUserTimeLimit", wintypes.LARGE_INTEGER),
            ("LimitFlags", wintypes.DWORD),
            ("MinimumWorkingSetSize", ctypes.c_size_t),
            ("MaximumWorkingSetSize", ctypes.c_size_t),
            ("ActiveProcessLimit", wintypes.DWORD),
            ("Affinity", ctypes.c_size_t),
            ("PriorityClass", wintypes.DWORD),
            ("SchedulingClass", wintypes.DWORD),
        ]

    class _IO_COUNTERS(ctypes.Structure):
        _fields_ = [(n, ctypes.c_ulonglong) for n in (
            "ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
            "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]

    class _JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
        _fields_ = [
            ("BasicLimitInformation", _JOBOBJECT_BASIC_LIMIT_INFORMATION),
            ("IoInfo", _IO_COUNTERS),
            ("ProcessMemoryLimit", ctypes.c_size_t),
            ("JobMemoryLimit", ctypes.c_size_t),
            ("PeakProcessMemoryUsed", ctypes.c_size_t),
            ("PeakJobMemoryUsed", ctypes.c_size_t),
        ]

    _JOB_INFO_EXTENDED_LIMIT = 9  # JobObjectExtendedLimitInformation
    _JOB_LIMIT_KILL_ON_JOB_CLOSE = 0x2000

    def _bind_children_to_parent_lifetime(children: list[Child]):
        """Put the children in a kill-on-close Job Object so Windows tears them down
        when this process dies — covering the uncatchable external SIGTERM. Returns
        the job handle, which the caller MUST keep referenced for the whole run
        (dropping it closes the handle and kills the children early). None on failure."""
        try:
            k32 = ctypes.WinDLL("kernel32", use_last_error=True)
            k32.CreateJobObjectW.restype = wintypes.HANDLE
            k32.CreateJobObjectW.argtypes = [wintypes.LPVOID, wintypes.LPCWSTR]
            k32.SetInformationJobObject.restype = wintypes.BOOL
            k32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int,
                                                    wintypes.LPVOID, wintypes.DWORD]
            k32.AssignProcessToJobObject.restype = wintypes.BOOL
            k32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]

            job = k32.CreateJobObjectW(None, None)
            if not job:
                raise ctypes.WinError(ctypes.get_last_error())
            info = _JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
            info.BasicLimitInformation.LimitFlags = _JOB_LIMIT_KILL_ON_JOB_CLOSE
            if not k32.SetInformationJobObject(job, _JOB_INFO_EXTENDED_LIMIT,
                                               ctypes.byref(info), ctypes.sizeof(info)):
                raise ctypes.WinError(ctypes.get_last_error())
            for c in children:
                if c.proc is not None:
                    if not k32.AssignProcessToJobObject(job, int(c.proc._handle)):
                        raise ctypes.WinError(ctypes.get_last_error())
            return job
        except Exception as e:  # best-effort safety net; degrade to the signal trap
            print(f"[{_iso()}] WARN: Windows job-object teardown unavailable ({e}); "
                  "relying on the signal trap only")
            return None
else:
    def _bind_children_to_parent_lifetime(children: list[Child]):
        return None  # POSIX relies on the SIGTERM trap + finally


def _finalize_manifest(path: Path, data: dict, children: list[Child], stopped_by: str) -> None:
    """Rewrite the manifest with end-of-run facts so a stopped session is self-describing."""
    data = dict(data, end=_iso(), stopped_by=stopped_by,
                final_sizes={c.name: max(c.size(), 0) for c in children})
    try:
        path.write_text(json.dumps(data, indent=2))
    except OSError:
        pass


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sb20", help="SB20 BLE MAC for the nRF to follow (e.g. E4:AA:5A:D6:0E:D4)")
    ap.add_argument("--ant", default="", help="comma-sep LABEL:ANTID bike-power meters")
    ap.add_argument("--hr", action="append", default=[], help="HR strap label (wildcard). Repeatable.")
    ap.add_argument("--fec", action="store_true", help="also capture the bike's FE-C (wildcard)")
    ap.add_argument("--duration", type=float, default=3900.0, help="seconds (default ~65 min)")
    ap.add_argument("--out", default=str(_HERE.parent / "findings" / "captures"))
    ap.add_argument("--tag", default=time.strftime("%Y%m%d-%H%M"))
    ap.add_argument("--extcap-dir", default=_DEFAULT_EXTCAP)
    ap.add_argument("--interval", type=float, default=15.0, help="heartbeat seconds")
    ap.add_argument("--self-test", action="store_true", help="fake producers, no hardware")
    ap.add_argument("--_produce", nargs=2, help=argparse.SUPPRESS)
    args = ap.parse_args()

    if args._produce:
        return _run_producer(args._produce[0], float(args._produce[1]))
    if not args.self_test and not args.sb20:
        ap.error("--sb20 is required unless --self-test")

    _install_signal_handlers()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    children = build_children(args)
    for c in children:
        c.start()
    job = _bind_children_to_parent_lifetime(children)  # keep referenced until teardown
    print(f"[{_iso()}] MONITOR START  tag={args.tag}  duration={args.duration:.0f}s  -> {args.out}")
    for c in children:
        print(f"   {c.name:<9} -> {c.output.name}")
    if job:
        print(f"[{_iso()}] children bound to a kill-on-close job object (hard-stop safe)")
    manifest = out / f"MANIFEST-{args.tag}.json"
    manifest_data = {
        "tag": args.tag, "start": _iso(), "duration_s": args.duration, "sb20": args.sb20,
        "ant": args.ant, "hr": args.hr, "fec": args.fec,
        "outputs": [str(c.output) for c in children]}
    manifest.write_text(json.dumps(manifest_data, indent=2))

    stopped_by = "duration"
    end = time.monotonic() + args.duration + 20
    try:
        while time.monotonic() < end:
            time.sleep(args.interval)
            parts = [f"[{_iso()}]"]
            alive_any = False
            for c in children:
                sz, grew = c.tick()
                alive = c.alive()
                alive_any = alive_any or alive
                flag = "growing" if grew else ("dead" if not alive else "STALE")
                parts.append(f"{c.name}={max(sz, 0)}B[{flag}]")
            print("  ".join(parts))
            if not alive_any:
                stopped_by = "all-exited"
                print(f"[{_iso()}] all captures exited -> stopping")
                break
    except KeyboardInterrupt:
        # Ctrl-C (SIGINT) or a trapped SIGTERM/Ctrl-Break — same clean teardown.
        stopped_by = _STOP_SIGNAL or "interrupt"
        print(f"\n[{_iso()}] {stopped_by} -> stopping")
    finally:
        for c in children:
            c.stop()
    _finalize_manifest(manifest, manifest_data, children, stopped_by)
    final = ", ".join(f"{c.name}:{max(c.size(), 0)}B" for c in children)
    print(f"[{_iso()}] MONITOR STOP  stopped_by={stopped_by}  final={{{final}}}")
    print(f"   manifest: {manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
