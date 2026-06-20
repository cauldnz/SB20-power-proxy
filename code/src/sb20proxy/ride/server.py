"""Stdlib HTTP server for the ride dashboard — no web framework, no extra deps.

Rider/phone surface (always open):
  GET  /            -> the dashboard page
  GET  /api/live    -> live JSON (meters + director + message/hold/erg); browser polls
  GET  /api/workout -> the workout segments (timeline + chart target line)
  POST /api/start   -> start the ride clock (rider pressed Start)
  POST /api/stop    -> stop / reset the ride clock

Agent control surface (gated by `control_token` if one is set — header
`X-Control-Token` or `?token=`):
  GET  /api/control/state    -> rich snapshot for the agent to monitor
  POST /api/control/<op>     -> steer the plan (plan|segments|skip|goto|extend|
                                message|target); see control.apply_control

Runs in a daemon thread; the capture (or replay) runs separately and pushes into
the shared LiveState.
"""

from __future__ import annotations

import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

from .control import ControlError, apply_control, control_state
from .state import LiveState
from .webapp import APP_HTML, workout_json


class _RideHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, addr: tuple[str, int], state: LiveState,
                 control_token: str | None = None) -> None:
        super().__init__(addr, _Handler)
        self.state = state
        self.control_token = control_token


class _Handler(BaseHTTPRequestHandler):
    server: _RideHTTPServer  # type: ignore[assignment]

    def log_message(self, *args) -> None:  # keep the console clean
        pass

    def _send(self, code: int, ctype: str, body: str | bytes) -> None:
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _json(self, obj: object, code: int = 200) -> None:
        self._send(code, "application/json", json.dumps(obj))

    def _read_json(self) -> dict:
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0:
            return {}
        try:
            obj = json.loads(self.rfile.read(length) or b"{}")
        except (ValueError, TypeError):
            return {}
        return obj if isinstance(obj, dict) else {}

    def _control_authed(self) -> bool:
        """Control endpoints require the token (if the server was given one). The
        rider/phone endpoints stay open. Token via X-Control-Token or ?token=."""
        token = self.server.control_token
        if not token:
            return True
        given = self.headers.get("X-Control-Token")
        if given is None:
            given = (parse_qs(urlparse(self.path).query).get("token") or [None])[0]
        return given == token

    def do_GET(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        srv = self.server
        if path in ("/", "/index.html"):
            self._send(200, "text/html; charset=utf-8", APP_HTML)
        elif path == "/api/live":
            self._json(srv.state.snapshot())
        elif path == "/api/workout":
            self._json(workout_json(srv.state.plan, srv.state.profile))
        elif path == "/api/control/state":
            if not self._control_authed():
                return self._json({"ok": False, "error": "unauthorized"}, 401)
            self._json(control_state(srv.state))
        else:
            self._send(404, "text/plain", "not found\n")

    def do_POST(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        srv = self.server
        if path == "/api/start":
            srv.state.start_ride()
            self._json({"ok": True, "ride_started": True})
        elif path == "/api/stop":
            srv.state.stop_ride()
            self._json({"ok": True, "ride_started": False})
        elif path.startswith("/api/control/"):
            if not self._control_authed():
                return self._json({"ok": False, "error": "unauthorized"}, 401)
            op = path[len("/api/control/"):]
            try:
                self._json(apply_control(srv.state, op, self._read_json()))
            except ControlError as e:
                self._json({"ok": False, "error": str(e)}, 400)
        else:
            self._send(404, "text/plain", "not found\n")


class RideServer:
    """Thin lifecycle wrapper around a threaded HTTP server."""

    def __init__(self, state: LiveState,
                 *, host: str = "0.0.0.0", port: int = 8080,
                 control_token: str | None = None) -> None:
        self._httpd = _RideHTTPServer((host, port), state, control_token)
        self.host = host
        self.port = self._httpd.server_address[1]  # resolves port 0 to the real one
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        self._thread = threading.Thread(
            target=self._httpd.serve_forever, name="ride-http", daemon=True
        )
        self._thread.start()

    def stop(self) -> None:
        self._httpd.shutdown()
        self._httpd.server_close()
