#!/usr/bin/env python3
"""Headless bench for the ESP32-S3-Touch head-unit over USB serial (no WiFi/creds needed).

The S3 firmware (USE_LCD builds) exposes a tiny serial console — ``SCREEN`` dumps the current LCD
framebuffer as base64 BMP, ``TAP x y`` injects a synthetic touch, ``STATE`` reports live values.
This drives a scripted walkthrough (nav every screen, load + run a workout, drive it), grabs a PNG
of each step, and asserts the state transitions — so the whole UI/data chain is verified end-to-end
against the real board before anyone touches the panel.

    python code/scripts/bench_s3.py --port COM16 --out bench_out
"""

from __future__ import annotations

import argparse
import base64
import io
import sys
import time
from pathlib import Path

try:
    import serial  # pyserial
except ImportError as e:  # pragma: no cover - dependency guard
    _msg = f"needs pyserial: {e}\nRun: pip install pyserial"
    if __name__ == "__main__":
        print(_msg, file=sys.stderr)
        sys.exit(1)
    raise ImportError(_msg) from e  # importable: let callers skip, don't kill them


class S3Bench:
    def __init__(self, port: str, outdir: Path):
        self.ser = serial.Serial(port, 115200, timeout=2)
        self.out = outdir
        self.out.mkdir(parents=True, exist_ok=True)
        self.failures: list[str] = []
        time.sleep(0.5)
        self.ser.reset_input_buffer()

    def _send(self, cmd: str) -> None:
        self.ser.write((cmd + "\n").encode())
        self.ser.flush()

    def screenshot(self, name: str) -> Path | None:
        """Grab the LCD framebuffer, save <name>.png (and .bmp)."""
        self.ser.reset_input_buffer()
        self._send("SCREEN")
        deadline = time.time() + 5
        buf = b""
        while time.time() < deadline:
            chunk = self.ser.read(4096)
            if chunk:
                buf += chunk
                if b"BMP>" in buf:
                    break
        try:
            start = buf.index(b"<BMP") + 4
            end = buf.index(b"BMP>", start)
        except ValueError:
            self.failures.append(f"{name}: no BMP frame in serial output")
            return None
        b64 = buf[start:end].strip()
        try:
            bmp = base64.b64decode(b64, validate=False)
        except Exception as e:  # noqa: BLE001
            self.failures.append(f"{name}: base64 decode failed: {e}")
            return None
        bmp_path = self.out / f"{name}.bmp"
        bmp_path.write_bytes(bmp)
        png_path = self.out / f"{name}.png"
        try:
            from PIL import Image
            im = Image.open(io.BytesIO(bmp))
            im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
            im.save(png_path)
        except Exception:  # noqa: BLE001 - PIL optional; BMP is enough
            png_path = bmp_path
        print(f"  [shot] {name} ({len(bmp)} B) -> {png_path.name}")
        return png_path

    def tap(self, x: int, y: int) -> None:
        self._send(f"TAP {x} {y}")
        time.sleep(0.4)  # let the render task repaint

    def state(self) -> dict:
        self.ser.reset_input_buffer()
        self._send("STATE")
        deadline = time.time() + 3
        line = b""
        while time.time() < deadline:
            line = self.ser.readline()
            if line.strip().startswith(b"{"):
                break
        try:
            import json
            return json.loads(line.decode(errors="replace"))
        except Exception:  # noqa: BLE001
            return {}

    def expect(self, label: str, cond: bool) -> None:
        print(f"  [{'PASS' if cond else 'FAIL'}] {label}")
        if not cond:
            self.failures.append(label)

    def run(self) -> int:
        print("== S3 head-unit bench ==")
        st = self.state()
        self.expect("board responds to STATE", bool(st))
        self.expect("touch controller alive (AXS5106 ACK)", st.get("touch") == 1)

        # --- walk the nav ---
        self.screenshot("01_ride")
        self.tap(90, 312)  # Setup tab
        self.expect("nav -> Setup (screen 2)", self.state().get("screen") == 2)
        self.screenshot("02_setup")
        self.tap(150, 312)  # More tab
        self.expect("nav -> More (screen 3)", self.state().get("screen") == 3)
        self.screenshot("03_more")

        # --- workout: More -> Workout row, load a preset, run it ---
        self.tap(86, 40)  # "Workout" row on More
        self.expect("More -> Workout (screen 3=Workout enum)", self.state().get("screen") == 3)
        self.screenshot("04_workout_picker")
        self.tap(86, 70)  # first preset row
        s = self.state()
        self.expect("preset loaded", s.get("wk_loaded") == 1)
        self.screenshot("05_workout_loaded")
        self.tap(86, 262)  # Start (single full-width button)
        s = self.state()
        self.expect("workout running", s.get("wk_running") == 1)
        self.expect("target resolved (>0 W)", (s.get("wk_target") or 0) > 0)
        self.screenshot("06_workout_running")
        self.tap(28, 262)  # Pause (left of 3 buttons)
        self.expect("workout paused", self.state().get("wk_running") == 1)  # still loaded/running-but-paused
        self.screenshot("07_workout_paused")
        self.tap(150, 262)  # Stop (right button)
        self.expect("workout stopped", self.state().get("wk_running") == 0)

        # --- back to Ride, confirm live power is flowing (mock ramp) ---
        self.tap(20, 312)  # Ride tab
        self.expect("nav -> Ride", self.state().get("screen") == 0)
        p1 = self.state().get("power")
        time.sleep(1.5)
        p2 = self.state().get("power")
        self.expect("power telemetry present", isinstance(p1, int))
        self.tap(40, 10)  # title -> details
        self.expect("ride details toggled", self.state().get("details") == 1)
        self.screenshot("08_ride_details")

        print(f"\n== {'ALL PASSED' if not self.failures else str(len(self.failures)) + ' FAILURES'} ==")
        for f in self.failures:
            print("  FAIL:", f)
        return 1 if self.failures else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM16")
    ap.add_argument("--out", default="bench_out")
    args = ap.parse_args(argv)
    bench = S3Bench(args.port, Path(args.out))
    return bench.run()


if __name__ == "__main__":
    raise SystemExit(main())
