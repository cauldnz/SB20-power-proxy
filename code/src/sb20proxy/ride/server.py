"""Stdlib HTTP server for the ride dashboard — no web framework, no extra deps.

GET  /            -> the dashboard page
GET  /api/live    -> live JSON (meters + director state); the browser polls this
GET  /api/workout -> the workout segments (for the timeline + chart target line)
POST /api/start   -> start the ride clock (rider pressed Start)
POST /api/stop    -> stop / reset the ride clock

Runs in a daemon thread; the capture (or replay) runs separately and pushes into
the shared LiveState.
"""

from __future__ import annotations

import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from .state import LiveState
from .webapp import APP_HTML, workout_json


class _RideHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, addr: tuple[str, int], state: LiveState) -> None:
        super().__init__(addr, _Handler)
        self.state = state


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

    def do_GET(self) -> None:  # noqa: N802
        path = self.path.split("?", 1)[0]
        srv = self.server
        if path in ("/", "/index.html"):
            self._send(200, "text/html; charset=utf-8", APP_HTML)
        elif path == "/api/live":
            self._json(srv.state.snapshot())
        elif path == "/api/workout":
            self._json(workout_json(srv.state.plan))
        else:
            self._send(404, "text/plain", "not found\n")

    def do_POST(self) -> None:  # noqa: N802
        path = self.path.split("?", 1)[0]
        srv = self.server
        if path == "/api/start":
            srv.state.start_ride()
            self._json({"ok": True, "ride_started": True})
        elif path == "/api/stop":
            srv.state.stop_ride()
            self._json({"ok": True, "ride_started": False})
        else:
            self._send(404, "text/plain", "not found\n")


class RideServer:
    """Thin lifecycle wrapper around a threaded HTTP server."""

    def __init__(self, state: LiveState,
                 *, host: str = "0.0.0.0", port: int = 8080) -> None:
        self._httpd = _RideHTTPServer((host, port), state)
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
