"""End-to-end desk proof of the steerable session, hermetic (no bike):

  real committed capture --replay--> LiveState --> RideServer
        agent --HTTP control--> plan/message/target --> GET /api/live reflects

Pumps a real capture through the replay feed so the meters are live, boots the
actual server on an ephemeral loopback port, then drives the agent control API and
asserts the phone's /api/live + /api/workout reflect each step. This is the whole
agent -> director -> phone loop with the real data feed, proven at the desk.
"""

from __future__ import annotations

import json
import urllib.request

import pytest

from sb20proxy.ride import LiveState, RidePlan
from sb20proxy.ride.replay import replay_into
from sb20proxy.ride.server import RideServer
from sb20proxy.ride.workouts import DEMO

STEADY = "A-stagesL-steady-20260614-165737.jsonl"


def _req(url: str, *, method: str = "GET", body: dict | None = None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.status, json.loads(r.read() or b"{}")


@pytest.fixture
def live_server(captures_dir):
    # pump the real capture into the state instantly (no wall-clock waits), then serve
    state = LiveState(plan=RidePlan.from_workout(DEMO), mode="replay")
    emitted = replay_into(captures_dir / STEADY, state, sleep=lambda _d: None)
    assert emitted > 0
    srv = RideServer(state, host="127.0.0.1", port=0)
    srv.start()
    try:
        yield f"http://127.0.0.1:{srv.port}", emitted
    finally:
        srv.stop()


def test_replay_meters_reach_the_phone(live_server):
    base, _ = live_server
    live = _req(f"{base}/api/live")[1]
    m = live["meters"]["stages"]
    assert m["count"] > 100 and 0 <= m["power_w"] <= 2000


def test_full_agent_steering_loop(live_server):
    base, _ = live_server

    # rider/agent starts the ride; the director is now live
    _req(f"{base}/api/start", method="POST")
    assert _req(f"{base}/api/live")[1]["director"]["started"] is True

    # agent pushes a coaching message -> phone banner field
    _req(f"{base}/api/control/message", method="POST",
         body={"text": "settle in", "level": "info"})
    assert _req(f"{base}/api/live")[1]["message"]["text"] == "settle in"

    # agent sets an ad-hoc hold -> supersedes the segment target + the erg setpoint
    _req(f"{base}/api/control/target", method="POST", body={"power_w": 222})
    live = _req(f"{base}/api/live")[1]
    assert live["hold"]["power_w"] == 222 and live["erg_setpoint_w"] == 222

    # agent clears the hold -> erg setpoint falls back to the segment target
    _req(f"{base}/api/control/target", method="POST", body={"clear": True})
    assert _req(f"{base}/api/live")[1]["hold"] is None

    # agent swaps the whole plan -> version bumps and the timeline follows
    before = _req(f"{base}/api/live")[1]["plan_version"]
    _req(f"{base}/api/control/plan", method="POST",
         body={"name": "agent set", "segments": [{"duration_s": 120, "label": "Build",
                                                  "pct_ftp": 0.85}]})
    after = _req(f"{base}/api/control/state")[1]
    assert after["plan_version"] == before + 1
    assert after["plan"]["name"] == "agent set"
    # %FTP resolves against the default profile (250 W) -> 0.85 * 250 = 212
    assert after["plan"]["segments"][0]["power_w"] == 212

    # agent skips the active block -> cursor advances (single-segment plan -> finished)
    _req(f"{base}/api/control/skip", method="POST")
    assert _req(f"{base}/api/live")[1]["director"]["finished"] is True
