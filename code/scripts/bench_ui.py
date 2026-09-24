#!/usr/bin/env python3
"""bench_ui.py — drive the LVGL UI on ANY head-unit board over the serial console.

`bench_s3.py` proved the walk on the Waveshare S3, but its `run()` hard-codes that panel's
coordinates, so the bench run-sheet records "no board-agnostic walker exists" as a GAP and tells
you to hand-type taps from a table. This is that walker: the same primitives, with the tap targets
computed from the panel size exactly as `sessions/bench-ui-pass.md` §0c derives them from
`firmware/src/ui/LvglUi.cpp`.

The raw dump is always saved as evidence. `--decode` additionally renders it to a PNG: the two
boards frame `SCREEN` differently (the S3 emits `<BMP…BMP>`, which `bench_s3.py` already handles;
the LCD boards emit `<AREA x1 y1 x2 y2 b64>` blocks then `<DUMPDONE>`) and no decoder for the
latter was committed, so a framebuffer dump could only ever be read by eye off the camera. The
decoded frame is what LVGL *composed*, which is the right instrument for judging layout and
clipping — independent of anything the panel does to the pixels afterwards. The camera frame stays
the record of what the physical panel showed.

    python code/scripts/bench_ui.py --port COM14 --board guition --walk --out sessions/bench-out/x
    python code/scripts/bench_ui.py --port COM17 --board cyd --tap 120 305 --state

Every step prints the STATE the board reported, because a tap landing on the wrong screen is a
finding, not a typo (run-sheet §0c).
"""

from __future__ import annotations

import argparse
import base64
import importlib.util
import json
import re
import subprocess
import sys
import time
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent


def _load_s3bench():
    """Reuse bench_s3.S3Bench's primitives rather than reimplementing serial handling."""
    spec = importlib.util.spec_from_file_location("bench_s3", _SCRIPTS / "bench_s3.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.S3Bench


# Panel geometry per board. W/H only — every target below is derived from them.
PANELS = {
    "guition": (320, 480),
    "cyd": (240, 320),
    "s3": (172, 320),
}

SCREENS = {0: "Ride", 1: "Setup", 2: "More", 3: "Workout", 4: "Calibrate", 5: "Compare"}


# Values actually proven on a panel beat values derived from the layout formula. Only the S3's
# nav row has ever been confirmed on hardware (bench_s3.py), and it does NOT match the formula:
# the formula gives (28,305)/(86,305)/(143,305), the proven row is (20,312)/(90,312)/(150,312).
# Both land inside the 30 px nav bar, so the walk works either way — but the divergence is the
# reason the run-sheet says to read STATE after every tap and treat a wrong screen as a finding.
PROVEN_NAV = {
    "s3": {"ride": (20, 312), "setup": (90, 312), "more": (150, 312)},
}


class Targets:
    """Tap targets derived from the panel size (run-sheet §0c), with proven overrides."""

    def __init__(self, w: int, h: int, board: str = ""):
        self.w, self.h = w, h
        self.proven = PROVEN_NAV.get(board, {})

    # nav bar: 30 px at the bottom of every screen
    def nav_ride(self) -> tuple[int, int]:
        return self.proven.get("ride") or (self.w // 6, self.h - 15)

    def nav_setup(self) -> tuple[int, int]:
        return self.proven.get("setup") or (self.w // 2, self.h - 15)

    def nav_more(self) -> tuple[int, int]:
        # round, not floor: 5*320/6 = 266.67, and §0c's table says 267. Same nav third either
        # way, but reproducing the documented number keeps the tool and the run-sheet comparable.
        return self.proven.get("more") or (round(5 * self.w / 6), self.h - 15)

    def ride_title(self) -> tuple[int, int]:
        return (40, 10)

    def more_row(self, k: int) -> tuple[int, int]:
        """Rows: 0 Workout, 1 Calibrate, 2 Compare, 3 Mode, 4 Identity, 5 Source,
        6 Trainer, 7 Bright (8 Touch cal on the CYD only)."""
        return (self.w // 2, 49 + 29 * k)

    def setup_row(self, i: int) -> tuple[int, int]:
        return (self.w // 2, 71 + 38 * i)

    def setup_rescan(self) -> tuple[int, int]:
        return (10 + (self.w - 30) // 4, self.h - 54)

    def setup_save(self) -> tuple[int, int]:
        return (self.w - 10 - (self.w - 30) // 4, self.h - 54)

    def workout_preset(self, i: int) -> tuple[int, int]:
        return (self.w // 2, 72 + 52 * i)

    def calibrate_start(self) -> tuple[int, int]:
        return (self.w // 2, self.h - 60)


# RGB565 little-endian, as the firmware's SCREEN command emits it (one base64 block per flush area).
_AREA_RE = re.compile(rb"<AREA (\d+) (\d+) (\d+) (\d+) ([A-Za-z0-9+/=]+)>")


def decode_dump(raw: bytes, w: int, h: int, out_png: Path) -> tuple[Path | None, list[str]]:
    """Reassemble the <AREA> blocks of a SCREEN dump into a PNG. Returns (path, notes)."""
    notes: list[str] = []
    areas = _AREA_RE.findall(raw)
    if not areas:
        return None, ["no <AREA> blocks in the dump (an S3 <BMP> frame? use bench_s3.py)"]

    canvas = bytearray(w * h * 3)
    covered = 0
    for x1, y1, x2, y2, b64 in areas:
        x1, y1, x2, y2 = int(x1), int(y1), int(x2), int(y2)
        try:
            px = base64.b64decode(b64, validate=False)
        except Exception as e:  # noqa: BLE001
            notes.append(f"area {x1},{y1},{x2},{y2}: base64 failed ({e})")
            continue
        aw, ah = x2 - x1 + 1, y2 - y1 + 1
        want = aw * ah * 2
        if len(px) != want:
            notes.append(f"area {x1},{y1},{x2},{y2}: {len(px)} B, expected {want}")
        for row in range(ah):
            for col in range(aw):
                i = (row * aw + col) * 2
                if i + 1 >= len(px):
                    break
                v = px[i] | (px[i + 1] << 8)
                r = ((v >> 11) & 0x1F) * 255 // 31
                g = ((v >> 5) & 0x3F) * 255 // 63
                b = (v & 0x1F) * 255 // 31
                gx, gy = x1 + col, y1 + row
                if 0 <= gx < w and 0 <= gy < h:
                    o = (gy * w + gx) * 3
                    canvas[o:o + 3] = bytes((r, g, b))
        covered += aw * ah
    notes.append(f"{len(areas)} area(s), {covered}/{w * h} px covered")

    try:
        from PIL import Image
        Image.frombytes("RGB", (w, h), bytes(canvas)).save(out_png)
        return out_png, notes
    except ImportError:
        # No PIL: write a minimal PPM instead so the evidence still exists.
        ppm = out_png.with_suffix(".ppm")
        ppm.write_bytes(b"P6\n%d %d\n255\n" % (w, h) + bytes(canvas))
        notes.append("PIL absent - wrote PPM")
        return ppm, notes

class BenchUi:
    def __init__(self, port: str, board: str, outdir: Path, decode: bool = False,
                 settle: float = 20.0):
        self.decode = decode
        self.settle = settle
        if board not in PANELS:
            raise SystemExit(f"unknown board {board!r}; expected one of {sorted(PANELS)}")
        self.board = board
        self.w, self.h = PANELS[board]
        self.t = Targets(self.w, self.h, board)
        self.bench = _load_s3bench()(port, outdir)
        self._suppress_reset()
        self.out = outdir
        self.findings: list[str] = []

    def _suppress_reset(self) -> None:
        """Re-open the port with DTR/RTS deasserted so opening it does not reset the board.

        pyserial asserts DTR/RTS on open by default, which on the ESP32's native USB CDC is the
        auto-reset sequence: the board reboots and the UI returns to the Ride screen. Measured
        2026-09-25 -- a fresh invocation that only read STATE came back on screen 0 after the
        previous one had navigated to More, which silently invalidated a whole brightness
        measurement (every tap landed on a freshly-rebooted Ride screen, not the row under test).
        Setting .dtr/.rts BEFORE open applies them as the port opens, with no pulse.
        """
        try:
            ser = self.bench.ser
            port, baud = ser.port, ser.baudrate
            ser.close()
            ser.dtr = False
            ser.rts = False
            ser.port = port
            ser.baudrate = baud
            ser.open()
            # Settle before the caller's first tap. Opening the port REBOOTS the board (the
            # DTR/RTS auto-reset, which could not be suppressed on Windows), and a full reboot here
            # is ~20 s because WiFi, BLE and LVGL all come back. Taps sent into that window are
            # silently dropped.
            #
            # The trap, measured 2026-09-25: STATE answers valid JSON with touch:1 during the
            # window, so the board looks ready when it is not. Two separate test runs were
            # invalidated that way -- taps 'landing' on a screen that never changed, which reads
            # exactly like a broken button. 2.5 s was not enough; 20 s is. --settle overrides.
            time.sleep(self.settle)
            ser.reset_input_buffer()
        except Exception as e:  # noqa: BLE001 - if it fails, say so rather than measure garbage
            print(f"  !! could not suppress the DTR/RTS reset ({e}); "
                  "state will not persist across calls")

    # -- primitives -------------------------------------------------------------------
    def state(self) -> dict:
        return self.bench.state()

    def tap(self, x: int, y: int) -> dict:
        self.bench.tap(x, y)
        time.sleep(0.5)
        return self.state()

    def raw_screen(self, name: str) -> Path:
        """Save the raw SCREEN response. No decode — see the module docstring."""
        self.bench.ser.reset_input_buffer()
        self.bench._send("SCREEN")
        deadline, buf = time.time() + 8, b""
        while time.time() < deadline:
            chunk = self.bench.ser.read(8192)
            if chunk:
                buf += chunk
                if b"DUMPDONE" in buf or b"BMP>" in buf:
                    break
        p = self.out / f"{self.board}-{name}.txt"
        p.write_bytes(buf)
        msg = f"  [dump] {name}: {len(buf)} B -> {p.name}"
        if self.decode:
            png, notes = decode_dump(buf, self.w, self.h, self.out / f"{self.board}-{name}.png")
            msg += f" -> {png.name}" if png else "  (decode failed)"
            for n in notes:
                msg += f"\n           {n}"
        print(msg)
        return p

    def goto(self, label: str, xy: tuple[int, int], want: int) -> bool:
        st = self.tap(*xy)
        got = st.get("screen")
        ok = got == want
        print(f"  tap {xy[0]:>3},{xy[1]:<3} -> {label:<10} expect {want} got {got} "
              f"({SCREENS.get(got, '?')})  {'OK' if ok else 'MISMATCH'}")
        if not ok:
            self.findings.append(f"{label}: expected screen {want}, got {got}")
        return ok

    def sequence(self, steps: list[str]) -> int:
        """Run steps in a single serial session so UI state persists between them.

        Step syntax: `tap:X,Y` · `screen:NAME` · `state` · `shot:NAME` (bench camera) · `wait:SEC`.
        """
        for step in steps:
            kind, _, arg = step.partition(":")
            if kind == "tap":
                x, y = (int(v) for v in arg.split(","))
                st = self.tap(x, y)
                print(f"  tap {x},{y} -> screen {st.get('screen')} "
                      f"({SCREENS.get(st.get('screen'), '?')})")
            elif kind == "screen":
                self.raw_screen(arg)
            elif kind == "state":
                print(f"  STATE {json.dumps(self.state())}")
            elif kind == "shot":
                self.camera_shot(arg)
            elif kind == "wait":
                time.sleep(float(arg))
            else:
                print(f"  ?? unknown step {step!r}")
        return 0

    def camera_shot(self, name: str) -> Path | None:
        """Grab a bench-camera frame without dropping the serial session."""
        p = self.out / f"cam-{name}.jpg"
        cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "dshow",
               "-rtbufsize", "512M", "-video_size", "3840x2160", "-i", "video=UC70",
               "-t", "3", "-fps_mode", "passthrough", "-update", "1", "-q:v", "2", "-y", str(p)]
        try:
            subprocess.run(cmd, capture_output=True, timeout=120)
        except Exception as e:  # noqa: BLE001
            print(f"  [cam] {name}: FAILED ({e})")
            return None
        print(f"  [cam] {name} -> {p.name}")
        return p if p.exists() else None

    # -- the walk ---------------------------------------------------------------------
    def walk(self) -> int:
        """Step 1 of the run-sheet: every screen, a dump at each, STATE checked every time."""
        print(f"walking {self.board} ({self.w}x{self.h}) on {self.bench.ser.port}")
        st = self.state()
        print(f"  start STATE: {json.dumps(st)}")
        if not st:
            print("  !! no STATE — is this an LVGL build with the console?")
            return 2
        self.raw_screen("01-ride")
        self.goto("Setup", self.t.nav_setup(), 1)
        self.raw_screen("02-setup")
        self.goto("More", self.t.nav_more(), 2)
        self.raw_screen("03-more")
        self.goto("Workout", self.t.more_row(0), 3)
        self.raw_screen("04-workout")
        self.goto("More", self.t.nav_more(), 2)
        self.goto("Calibrate", self.t.more_row(1), 4)
        self.raw_screen("05-calibrate")
        self.goto("More", self.t.nav_more(), 2)
        self.goto("Compare", self.t.more_row(2), 5)
        self.raw_screen("06-compare")
        self.goto("Ride", self.t.nav_ride(), 0)
        print()
        if self.findings:
            print("FINDINGS:")
            for f in self.findings:
                print(f"  - {f}")
        else:
            print("walk clean: every tap landed on the expected screen")
        return 1 if self.findings else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--board", required=True, choices=sorted(PANELS))
    ap.add_argument("--out", default="bench_out")
    ap.add_argument("--walk", action="store_true", help="run the full screen walk (step 1)")
    ap.add_argument("--seq", nargs="+", metavar="STEP",
                    help="steps in ONE serial session: tap:X,Y screen:N state shot:N wait:S")
    ap.add_argument("--tap", nargs=2, type=int, metavar=("X", "Y"))
    ap.add_argument("--state", action="store_true")
    ap.add_argument("--screen", metavar="NAME", help="save a raw SCREEN dump under this name")
    ap.add_argument("--targets", action="store_true", help="print the derived tap table and exit")
    ap.add_argument("--settle", type=float, default=20.0,
                    help="seconds to wait after opening the port, for the reset-reboot "
                         "to finish (default 20; taps before that are dropped)")
    ap.add_argument("--decode", action="store_true",
                    help="also render each SCREEN dump to a PNG (LCD <AREA> format)")
    a = ap.parse_args(argv)

    if a.targets:
        w, h = PANELS[a.board]
        t = Targets(w, h, a.board)
        print(f"{a.board} {w}x{h}")
        print(f"  nav Ride/Setup/More : {t.nav_ride()} {t.nav_setup()} {t.nav_more()}")
        print(f"  More rows 0..7      : {[t.more_row(k) for k in range(8)]}")
        print(f"  Setup rescan/save   : {t.setup_rescan()} {t.setup_save()}")
        print(f"  Workout presets 0..3: {[t.workout_preset(i) for i in range(4)]}")
        print(f"  Calibrate start     : {t.calibrate_start()}")
        return 0

    ui = BenchUi(a.port, a.board, Path(a.out), decode=a.decode, settle=a.settle)
    if a.walk:
        return ui.walk()
    if a.seq:
        return ui.sequence(a.seq)
    if a.tap:
        print(json.dumps(ui.tap(*a.tap)))
    if a.screen:
        ui.raw_screen(a.screen)
    if a.state or not (a.tap or a.screen):
        print(json.dumps(ui.state()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
