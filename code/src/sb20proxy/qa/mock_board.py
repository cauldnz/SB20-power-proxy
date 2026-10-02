"""A desk stand-in for an ESP32 board's HTTP surface, so the shared SPA (`GET /app`) can be driven
in a browser with no hardware: the ride view, the reconnect path and the #347 routes.

    python -m sb20proxy.qa.mock_board --port 8320          # then open http://127.0.0.1:8320/app
    curl -X POST "http://127.0.0.1:8320/_mock/reboot?down_s=6"   # simulate a board reboot

What it is NOT: a firmware emulator. It serves

* ``GET /app`` — the exact bytes the board streams: the raw string inside
  ``firmware/lib/proxy/WebSpa.h`` (the generated, module-inlined SPA), not ``web/index.html``;
* the JSON routes the SPA reads, with the field names the firmware serializers emit
  (``Status.h`` ``renderStatusJson``, ``WorkoutEngine.h`` ``renderWorkoutJson``, ``WebJson.h``).
  ``code/tests/test_mock_board.py`` parses those C++ sources and fails if a key set here drifts
  from them, so the mock cannot invent a field the board does not send;
* the workout presets parsed out of ``WorkoutPresets.h`` (the board's own canonical JSON), stepped
  with the same target precedence as ``segmentTargetW`` (power_w, then pct_ftp x ftp_w);
* the commands the SPA posts, with the board's reboot semantics (``/config``, a changed
  ``/workout/trainer``, ``/calibrate/start|save|cancel`` restart it): during a simulated restart
  every request is dropped without a response (what a phone sees), then ``ms`` restarts near 0
  and a running workout is lost (the board keeps only the loaded workout in NVS).

Power is a slow synthetic ramp (100-300 W), clearly not a measurement.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parents[4]
WEBSPA_H = ROOT / "firmware" / "lib" / "proxy" / "WebSpa.h"
PRESETS_H = ROOT / "firmware" / "lib" / "proxy" / "WorkoutPresets.h"
PRESET_KEYS = ("4x8", "ss3x12", "vo25x3", "endur45")  # HttpTransport.wkLoad's key order


def spa_bytes() -> bytes:
    """The SPA exactly as the board serves it: the raw-string body of webSpaHtml() in WebSpa.h."""
    text = WEBSPA_H.read_text(encoding="utf-8")
    m = re.search(r'R"SB20SPA\((.*)\)SB20SPA"', text, re.S)
    if not m:
        raise SystemExit(f"no SB20SPA raw string in {WEBSPA_H}")
    return m.group(1).encode("utf-8")


def load_presets() -> dict[str, dict]:
    """The board's built-in workouts, parsed from WorkoutPresets.h's raw-string literals."""
    text = PRESETS_H.read_text(encoding="utf-8")
    out = {}
    for key, body in re.findall(r'\{"(\w+)",\s*"[^"]*",\s*((?:R"\(.*?\)"\s*)+)\}', text, re.S):
        out[key] = json.loads("".join(re.findall(r'R"\((.*?)\)"', body, re.S)))
    return out


def segment_target_w(seg: dict, ftp_w: int) -> int:
    """WorkoutEngine.h segmentTargetW: power_w wins, then pct_ftp x ftp (presets use no zones)."""
    if "power_w" in seg:
        return int(seg["power_w"])
    if "pct_ftp" in seg and ftp_w > 0:
        return int(
            math.floor(seg["pct_ftp"] * ftp_w + 0.5)
        )  # std::lround (half away from zero), not round()
    return -1


class MockBoard:
    """The board's state. Every public method is called under self.lock."""

    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.presets = load_presets()
        self.boot_t = time.monotonic()
        self.down_until = 0.0
        self.trainer = ""
        self.identity = "Stages 92729"
        self.mode = "spoof"
        self.single = False
        self.src_filter = ""
        self.curve: list[list[float]] = []
        self.buttons = {"enabled": False, "actions": [0, 0, 0, 0, 0, 0]}
        self.workout: dict | None = None
        self.run_start = None  # monotonic time the run began (None = not running)
        self.paused_at = None
        self.paused_total = 0.0
        self.bias = 0
        self.cal_state = 0  # CalState: 0 idle, 1 collecting, 2 fitted
        self.cal_pairs = 0
        self.devices = [  # what /scan + the calibration picker list
            {
                "name": "ASSIOMA17039L",
                "addr": "e6:20:90:8c:f3:fe",
                "rssi": -61,
                "cps": True,
                "ftms": False,
                "crank": False,
            },
            {
                "name": "XCADEY-POWER",
                "addr": "c4:11:22:33:44:55",
                "rssi": -70,
                "cps": True,
                "ftms": False,
                "crank": False,
            },
            {
                "name": "SB20-FTMS-Server",
                "addr": "a0:76:4e:00:00:01",
                "rssi": -48,
                "cps": False,
                "ftms": True,
                "crank": False,
            },
        ]

    # -- clock ---------------------------------------------------------------
    def now(self) -> float:
        return time.monotonic()

    def is_down(self) -> bool:
        return self.now() < self.down_until

    def reboot(self, down_s: float = 4.0) -> None:
        """Drop off the network for down_s, then return: fresh uptime, no running workout."""
        self.down_until = self.now() + down_s
        self.boot_t = self.down_until
        self.run_start = None
        self.paused_at = None
        self.paused_total = 0.0
        self.bias = 0

    def uptime_ms(self) -> int:
        return max(0, int((self.now() - self.boot_t) * 1000))

    # -- power ---------------------------------------------------------------
    def power(self) -> tuple[int, int]:
        t = self.now()
        watts = int(200 + 100 * math.sin(t / 20.0))
        if self.running() and not self.paused_at:  # follow the erg target loosely when riding one
            tgt = self.workout_state()["seg_target_w"]
            if tgt > 0:
                watts = int(tgt + self.bias + 8 * math.sin(t))
        return watts, int(90 + 4 * math.sin(t / 7.0))

    # -- workout -------------------------------------------------------------
    def running(self) -> bool:
        return self.workout is not None and self.run_start is not None

    def elapsed_s(self) -> int:
        if not self.running():
            return 0
        paused = self.paused_total + ((self.now() - self.paused_at) if self.paused_at else 0.0)
        return int(self.now() - self.run_start - paused)

    def workout_state(self) -> dict:
        """renderWorkoutJson's fields, in its order."""
        w = self.workout or {"name": "", "ftp_w": 0, "segments": []}
        segs, ftp = w["segments"], int(w.get("ftp_w", 0))
        total = sum(s["t"] for s in segs)
        el = min(self.elapsed_s(), total)
        idx, seg_el, acc = len(segs), 0, 0
        for i, s in enumerate(segs):
            if el < acc + s["t"]:
                idx, seg_el = i, el - acc
                break
            acc += s["t"]
        cur = segs[idx] if idx < len(segs) else None
        nxt = segs[idx + 1] if idx + 1 < len(segs) else None
        return {
            "name": w["name"],
            "ftp_w": ftp,
            "loaded": bool(segs),
            "running": self.running(),
            "paused": bool(self.paused_at),
            "finished": bool(segs) and el >= total and self.running(),
            "seg_index": idx,
            "seg_count": len(segs),
            "seg_label": cur["label"] if cur else "",
            "seg_target_w": segment_target_w(cur, ftp) if cur else -1,
            "seg_elapsed_s": seg_el if cur else 0,
            "seg_remaining_s": (cur["t"] - seg_el) if cur else 0,
            "next_label": nxt["label"] if nxt else "",
            "next_target_w": segment_target_w(nxt, ftp) if nxt else -1,
            "total_elapsed_s": el,
            "total_remaining_s": total - el,
            "segments": [
                {"t": s["t"], "w": segment_target_w(s, ftp), "label": s["label"]} for s in segs
            ],
        }

    def workout_state_json(self) -> dict:
        """WebJson.h renderWorkoutStateJson: the engine JSON plus the erg leg."""
        j = self.workout_state()
        linked = (
            bool(self.trainer) and self.uptime_ms() > 3000
        )  # the erg client links a few s after boot
        j.update({"erg_connected": linked, "erg_controlled": linked, "bias_w": self.bias})
        return j

    def control(self, verb: str) -> None:
        t = self.now()
        if verb == "start" and self.workout:
            self.run_start, self.paused_at, self.paused_total = t, None, 0.0
        elif verb == "pause" and self.running() and not self.paused_at:
            self.paused_at = t
        elif verb == "resume" and self.paused_at:
            self.paused_total += t - self.paused_at
            self.paused_at = None
        elif verb == "stop":
            self.run_start, self.paused_at = None, None

    # -- JSON bodies -----------------------------------------------------------
    def status(self) -> dict:
        """Status.h renderStatusJson's fields, in its order."""
        watts, cad = self.power()
        return {
            "fw": "sb20proxy-esp32",
            "version": "mock",
            "build_sha": "mock",
            "build_time": "mock",
            "source": "connected",
            "src_name": "ASSIOMA17039L",
            "identity": self.identity,
            "mode": self.mode,
            "identity_default": True,
            "source_pin": "e6:20:90:8c:f3:fe",
            "source_filter": self.src_filter,
            "trainer": self.trainer,
            "ble_off": False,
            "forwarded": self.uptime_ms() // 500,
            "src_power_w": watts,
            "src_cadence_rpm": cad,
            "src_balance_pct": 50,
            "power_w": watts,
            "cadence_rpm": cad,
            "balance_pct": 50,
            "rssi": -55,
            "heap": 120000,
            "ms": self.uptime_ms(),
        }

    def scan(self) -> dict:
        return {
            "devices": [
                {k: d[k] for k in ("name", "rssi", "cps", "ftms", "crank")} for d in self.devices
            ]
        }

    def cal_state_json(self) -> dict:
        """WebJson.h renderCalStateJson's fields."""
        if self.cal_state == 1:
            self.cal_pairs = min(60, int((self.now() - self.boot_t) / 2))
        cov = [min(self.cal_pairs // 6, 9)] * 6 if self.cal_state else []
        return {
            "state": self.cal_state,
            "pairs": self.cal_pairs if self.cal_state == 1 else 0,
            "min_pairs": 30,
            "residual_w": 2.4 if self.cal_state == 2 else 0.0,
            "enough": self.cal_state == 1 and self.cal_pairs >= 30,
            "dut_connected": self.cal_state == 1,
            "ref_connected": self.cal_state == 1,
            "coverage": cov,
            "devices": [
                {"name": d["name"], "addr": d["addr"], "rssi": d["rssi"]}
                for d in self.devices
                if d["cps"]
            ]
            if self.cal_state == 0
            else [],
        }

    def config(self) -> dict:
        return {
            "scale": 1.0,
            "offset": 0.0,
            "single_sided": self.single,
            "src_filter": self.src_filter,
            "out_name": "",
            "mode": self.mode,
            "has_curve": bool(self.curve),
        }


def make_handler(board: MockBoard):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):  # quiet; the SPA's own log card is the record
            pass

        def _send(self, status: int, body, ctype: str = "application/json") -> None:
            data = (
                body
                if isinstance(body, bytes)
                else (
                    json.dumps(body, separators=(",", ":")) if ctype == "application/json" else body
                ).encode()
            )
            self.send_response(status)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _dropped(self) -> bool:
            """While 'rebooting', close the socket with no reply — a fetch() then rejects."""
            with board.lock:
                down = board.is_down()
            if down:
                self.close_connection = True
            return down

        def do_GET(self):  # noqa: N802 (http.server API)
            if self._dropped():
                return
            path = urlsplit(self.path).path
            with board.lock:
                routes = {
                    "/status": board.status,
                    "/workout/state": board.workout_state_json,
                    "/scan": board.scan,
                    "/calibrate/state": board.cal_state_json,
                    "/config": board.config,
                    "/curve": lambda: {"has_curve": bool(board.curve), "curve": board.curve},
                    "/obc/buttons.json": lambda: board.buttons,
                    "/compare": lambda: {"valid": False},
                }
                if path == "/app":
                    return self._send(200, spa_bytes(), "text/html")
                if path in routes:
                    return self._send(200, routes[path]())
            self._send(404, "Not found\n", "text/plain")

        def do_POST(self):  # noqa: N802
            if self._dropped():
                return
            url = urlsplit(self.path)
            q = {k: v[-1] for k, v in parse_qs(url.query).items()}
            n = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(n).decode("utf-8") if n else ""
            form = {k: v[-1] for k, v in parse_qs(raw, keep_blank_values=True).items()}
            arg = {**form, **q}
            p = url.path
            with board.lock:
                if p == "/_mock/reboot":
                    board.reboot(float(arg.get("down_s", 4)))
                    return self._send(200, {"ok": True})
                if p == "/config":
                    board.single = arg.get("single") == "1"
                    board.src_filter = arg.get("src_filter", board.src_filter)
                    board.mode = arg.get("mode", board.mode)
                    self._send(200, {"ok": True, "reboot": True})
                    return board.reboot()
                if p == "/curve":
                    pts = [x.split(":") for x in raw.split(",") if ":" in x]
                    board.curve = [[float(a), float(b)] for a, b in pts]
                    return self._send(200, {"has_curve": bool(board.curve), "curve": board.curve})
                if p == "/obc/buttons.json":
                    board.buttons = json.loads(raw or "{}")
                    return self._send(200, board.buttons)
                if p == "/setup/scan":
                    return self._send(200, {"ok": True, "scanning": True})
                if p == "/workout/preset":
                    w = board.presets.get(arg.get("key", ""))
                    if not w:
                        return self._send(400, "unknown preset\n", "text/plain")
                    board.workout, board.run_start, board.paused_at = w, None, None
                    return self._send(200, "loaded\n", "text/plain")
                if p in (
                    "/workout/start",
                    "/workout/pause",
                    "/workout/resume",
                    "/workout/stop",
                    "/workout/skip",
                ):
                    board.control(p.rsplit("/", 1)[1])
                    return self._send(200, "ok\n", "text/plain")
                if p == "/workout/bias":
                    try:
                        d = int(arg.get("d", ""))
                    except ValueError:
                        d = 999
                    if not -50 <= d <= 50:
                        return self._send(400, {"error": "expected d=<-50..50>"})
                    board.bias = max(-200, min(200, board.bias + d))
                    return self._send(200, {"bias_w": board.bias})
                if p == "/workout/trainer":
                    name = arg.get("name", "")
                    if name == board.trainer:
                        return self._send(200, {"ok": True, "reboot": False})
                    board.trainer = name
                    self._send(200, {"ok": True, "reboot": True})
                    return board.reboot()
                if p == "/calibrate/start":
                    if not arg.get("dut") or not arg.get("ref") or arg["dut"] == arg["ref"]:
                        return self._send(
                            200,
                            "<p class='msg'>Pick BOTH a DUT and a reference meter.</p>",
                            "text/html",
                        )
                    board.cal_state, board.cal_pairs = 1, 0
                    self._send(200, "<h1>Starting calibration&hellip;</h1>", "text/html")
                    return board.reboot()
                if p == "/calibrate/finish":
                    if board.cal_state == 1 and board.cal_pairs >= 30:
                        board.cal_state = 2
                    return self._send(303, "fitting\n", "text/plain")
                if p in ("/calibrate/save", "/calibrate/cancel"):
                    if p.endswith("save") and board.cal_state != 2:
                        return self._send(
                            200, "<p class='msg'>Finish the calibration first</p>", "text/html"
                        )
                    if p.endswith("save"):
                        board.mode, board.curve = "corrector", [[150.0, 1.02], [250.0, 0.99]]
                    board.cal_state = 0
                    self._send(200, "<h1>Restarting</h1>", "text/html")
                    return board.reboot()
            self._send(404, "Not found\n", "text/plain")

    return Handler


def serve(port: int = 8320, host: str = "127.0.0.1") -> tuple[ThreadingHTTPServer, MockBoard]:
    board = MockBoard()
    srv = ThreadingHTTPServer((host, port), make_handler(board))
    return srv, board


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--port", type=int, default=8320)
    ap.add_argument("--host", default="127.0.0.1")
    args = ap.parse_args()
    srv, _ = serve(args.port, args.host)
    print(
        f"mock board on http://{args.host}:{srv.server_address[1]}/app"
        "  (POST /_mock/reboot?down_s=6 to restart it)"
    )
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
