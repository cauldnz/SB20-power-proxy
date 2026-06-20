"""Integration smoke: a real RideServer on an ephemeral port, exercised over HTTP.

Proves the routing + auth + the agent->director->phone loop end to end (no bike):
a control POST mutates the plan, and the phone's GET /api/live reflects it. Hermetic
(loopback socket only). The pure dispatch logic is covered in test_ride_control.py;
this is the wiring.
"""

from __future__ import annotations

import json
import urllib.error
import urllib.request

import pytest

from sb20proxy.ride import LiveState, RidePlan, RiderProfile
from sb20proxy.ride.director import Segment
from sb20proxy.ride.server import RideServer


def _plan() -> RidePlan:
    return RidePlan("t", [Segment(60, "A", 100), Segment(60, "B", 200)])


def _req(url: str, *, method: str = "GET", body: dict | None = None,
         headers: dict | None = None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers=headers or {})
    try:
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, _maybe_json(r.read())
    except urllib.error.HTTPError as e:
        return e.code, _maybe_json(e.read())


def _maybe_json(raw: bytes):
    try:
        return json.loads(raw or b"{}")
    except ValueError:
        return raw.decode("utf-8", "replace")  # e.g. the HTML dashboard at /


@pytest.fixture
def server():
    srv = RideServer(LiveState(plan=_plan()), host="127.0.0.1", port=0)
    srv.start()
    try:
        yield f"http://127.0.0.1:{srv.port}"
    finally:
        srv.stop()


def test_phone_endpoints_open(server):
    assert _req(f"{server}/api/live")[0] == 200
    assert _req(f"{server}/api/workout")[1]["segments"][0]["label"] == "A"
    assert _req(f"{server}/", method="GET")[0] == 200


def test_control_message_reflects_in_live(server):
    _req(f"{server}/api/start", method="POST")
    status, r = _req(f"{server}/api/control/message", method="POST",
                     body={"text": "ease back", "level": "warn"})
    assert status == 200 and r["ok"] is True
    live = _req(f"{server}/api/live")[1]
    assert live["message"]["text"] == "ease back"


def test_control_plan_swap_reflects_in_live(server):
    before = _req(f"{server}/api/live")[1]["plan_version"]
    status, r = _req(f"{server}/api/control/plan", method="POST",
                     body={"name": "swap", "segments": [{"duration_s": 30, "label": "Z",
                                                         "power_w": 333}]})
    assert status == 200
    live = _req(f"{server}/api/live")[1]
    assert live["plan_version"] == before + 1
    assert _req(f"{server}/api/workout")[1]["name"] == "swap"
    # erg setpoint tracks the new active segment once the ride is running
    _req(f"{server}/api/start", method="POST")
    assert _req(f"{server}/api/live")[1]["erg_setpoint_w"] == 333


def test_workout_resolves_pct_ftp_against_profile():
    # the /api/workout timeline must resolve %FTP/zone targets, not return None
    plan = RidePlan("z", [Segment(60, "SS", pct_ftp=0.90, cadence_rpm=90)])
    srv = RideServer(LiveState(plan=plan, profile=RiderProfile(ftp_w=300)),
                     host="127.0.0.1", port=0)
    srv.start()
    try:
        seg = _req(f"http://127.0.0.1:{srv.port}/api/workout")[1]["segments"][0]
        assert seg["power_w"] == 270 and seg["zone"] == "Z4"  # 0.90 * 300
    finally:
        srv.stop()


def test_bad_control_request_is_400(server):
    status, r = _req(f"{server}/api/control/plan", method="POST", body={"segments": []})
    assert status == 400 and r["ok"] is False and "error" in r


def test_token_gates_control_only():
    srv = RideServer(LiveState(plan=_plan()), host="127.0.0.1", port=0,
                     control_token="s3cret")
    srv.start()
    base = f"http://127.0.0.1:{srv.port}"
    try:
        # phone endpoints stay open
        assert _req(f"{base}/api/live")[0] == 200
        # control without the token -> 401
        assert _req(f"{base}/api/control/state")[0] == 401
        assert _req(f"{base}/api/control/skip", method="POST")[0] == 401
        # with the header -> ok
        assert _req(f"{base}/api/control/state",
                    headers={"X-Control-Token": "s3cret"})[0] == 200
        # with the query param -> ok
        assert _req(f"{base}/api/control/state?token=s3cret")[0] == 200
        # wrong token -> 401
        assert _req(f"{base}/api/control/state?token=nope")[0] == 401
    finally:
        srv.stop()
