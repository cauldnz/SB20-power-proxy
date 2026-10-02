"""The desk mock of a board's HTTP surface (sb20proxy.qa.mock_board) must match the firmware.

The mock exists so the shared SPA's ride view (#351) and the #347 routes can be exercised in a
browser with no hardware. It is only trustworthy while it speaks the board's dialect, so these tests
parse the C++ serializers the board actually runs and require the mock's JSON keys to match them
exactly — a field renamed in the firmware fails here, not on a bench phone. Hermetic: an in-process
server on 127.0.0.1, no hardware.
"""

from __future__ import annotations

import json
import re
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

import pytest

from sb20proxy.qa import mock_board

ROOT = Path(__file__).resolve().parents[2]
PROXY = ROOT / "firmware" / "lib" / "proxy"


def cpp_keys(header: str, fn: str) -> set[str]:
    """JSON keys `inline std::string fn(...)` writes (its \\"key\\": literals), per gen_webjson."""
    text = (PROXY / header).read_text(encoding="utf-8")
    m = re.search(r"inline std::string\s+" + re.escape(fn) + r"\s*\(", text)
    assert m, f"{fn}() not found in {header}"
    tail = text[m.end() :]
    nxt = re.search(r"\ninline\s", tail)
    return set(re.findall(r'\\"(\w+)\\":', tail[: nxt.start()] if nxt else tail))


def all_keys(obj) -> set[str]:
    """Every key at any depth (the C++ regex sees nested keys flat, so compare like with like)."""
    if isinstance(obj, dict):
        return set(obj) | set().union(*(all_keys(v) for v in obj.values()))
    if isinstance(obj, list):
        return set().union(*(all_keys(v) for v in obj)) if obj else set()
    return set()


@pytest.fixture()
def board():
    srv, b = mock_board.serve(port=0)
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    yield f"http://127.0.0.1:{srv.server_address[1]}", b
    srv.shutdown()
    srv.server_close()


def get(base: str, path: str):
    with urllib.request.urlopen(base + path, timeout=5) as r:
        return json.loads(r.read())


def post(base: str, path: str, body: str = ""):
    req = urllib.request.Request(base + path, data=body.encode(), method="POST")
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.status, r.read()


def test_app_is_the_embedded_spa_the_board_streams(board):
    base, _ = board
    with urllib.request.urlopen(base + "/app", timeout=5) as r:
        body = r.read()
    header = (PROXY / "WebSpa.h").read_text(encoding="utf-8")
    assert body.decode("utf-8") in header
    assert b"const RM = (() => {" in body, "ride-model.js must be inlined into the board's copy"
    assert b'import * as RM from "./ride-model.js"' not in body


def test_status_keys_match_renderStatusJson(board):
    base, _ = board
    assert set(get(base, "/status")) == cpp_keys("Status.h", "renderStatusJson")


def test_workout_state_keys_match_the_engine_plus_the_erg_leg(board):
    base, _ = board
    post(base, "/workout/preset?key=4x8")
    post(base, "/workout/start")
    j = get(base, "/workout/state")
    want = cpp_keys("WorkoutEngine.h", "renderWorkoutJson") | cpp_keys(
        "WebJson.h", "renderWorkoutStateJson"
    )
    assert all_keys(j) == want
    assert j["running"] and j["seg_label"] == "Warm-up"
    assert (
        j["seg_target_w"] == 138
    )  # WorkoutPresets.h: Warm-up at 55 % of 250 W = 137.5, lround -> 138


def test_web_json_routes_match_their_serializers(board):
    base, b = board
    assert all_keys(get(base, "/scan")) == cpp_keys("WebJson.h", "renderScanJson")
    assert all_keys(get(base, "/config")) == cpp_keys("WebJson.h", "renderConfigJson")
    assert all_keys(get(base, "/curve")) == cpp_keys("WebJson.h", "renderCurveJson")
    assert all_keys(get(base, "/calibrate/state")) == cpp_keys("WebJson.h", "renderCalStateJson")


def test_presets_are_the_boards_own(board):
    _, b = board
    assert set(b.presets) == set(mock_board.PRESET_KEYS)
    assert b.presets["4x8"]["name"] == "4x8 Threshold"


def test_bias_and_trainer_follow_the_firmware_rules(board):
    base, b = board
    assert json.loads(post(base, "/workout/bias?d=10")[1]) == {"bias_w": 10}
    with pytest.raises(urllib.error.HTTPError) as e:
        post(base, "/workout/bias?d=60")
    assert e.value.code == 400
    # a changed trainer reboots; an unchanged one does not
    assert json.loads(post(base, "/workout/trainer", "name=SB20-FTMS-Server")[1])["reboot"] is True
    b.down_until = 0  # skip the simulated restart
    assert json.loads(post(base, "/workout/trainer", "name=SB20-FTMS-Server")[1])["reboot"] is False


def test_a_simulated_reboot_drops_requests_then_restarts_uptime(board):
    base, b = board
    b.boot_t -= 100  # pretend the board has been up for 100 s
    assert get(base, "/status")["ms"] >= 100_000
    post(base, "/_mock/reboot?down_s=0.4")
    with pytest.raises((urllib.error.URLError, ConnectionError, OSError)):
        get(base, "/status")  # no response at all, like a board mid-restart
    time.sleep(0.5)
    assert (
        get(base, "/status")["ms"] < 1000
    )  # uptime went backwards: the SPA reads that as a reboot
