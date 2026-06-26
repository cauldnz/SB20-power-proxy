"""Tests for the MCP workout server (sb20proxy.mcp.server).

Skipped entirely if the optional MCP SDK isn't installed, so the hermetic suite stays green
without it; CI installs the [mcp] extra so these run there. The drive tools use an injected
fake transport (the in-process FTMS twin) — no bike, no bleak.
"""

from __future__ import annotations

import json

import pytest

pytest.importorskip("mcp")  # the [mcp] extra; skip the whole module without it

from sb20proxy.ble.ftms_erg import ErgController, InProcessFtmsServer  # noqa: E402
from sb20proxy.mcp.server import build_server  # noqa: E402
from sb20proxy.workout.session import WorkoutSession  # noqa: E402

EXPECTED_TOOLS = {
    "list_workouts", "build_workout", "start", "stop", "skip", "goto", "extend",
    "set_target", "message", "set_profile", "status",
    "start_drive", "stop_drive", "drive_status",
}


def _payload(result) -> dict:
    """Pull the JSON dict out of a FastMCP call_tool result (a list of TextContent)."""
    if isinstance(result, dict):
        return result
    if isinstance(result, tuple):  # some versions: (content, structured)
        for part in result:
            if isinstance(part, dict):
                return part
        result = result[0]
    block = result[0]
    return json.loads(block.text)


def _fake_provider(srv: InProcessFtmsServer):
    async def provider(address, controller: ErgController):
        controller.power_range = srv.power_range()

        async def transport(cmd: bytes) -> bytes:
            return srv.handle(cmd)

        async def aclose() -> None:
            return None

        return transport, aclose
    return provider


@pytest.mark.asyncio
async def test_all_tools_registered():
    srv = build_server()
    tools = await srv.list_tools()
    names = {t.name for t in tools}
    assert EXPECTED_TOOLS <= names


@pytest.mark.asyncio
async def test_build_and_status_via_call_tool():
    server = build_server(WorkoutSession())
    built = _payload(await server.call_tool(
        "build_workout",
        {"spec": "5min @ 130W; 3x(1min @ 300W; 2min @ 100W)", "start": True},
    ))
    assert built["ride_started"] is True
    assert len(built["plan"]["segments"]) == 1 + 3 * 2

    st = _payload(await server.call_tool("status", {}))
    assert st["director"]["label"] == "130 W"
    assert st["erg_setpoint_w"] == 130


@pytest.mark.asyncio
async def test_build_from_structured_segments():
    server = build_server(WorkoutSession())
    built = _payload(await server.call_tool("build_workout", {
        "segments": [
            {"duration_s": 60, "label": "Warm-up", "power_w": 120},
            {"duration_s": 120, "label": "Work", "power_w": 250},
        ],
        "name": "My session",
        "start": True,
    }))
    assert built["plan"]["name"] == "My session"
    assert len(built["plan"]["segments"]) == 2


@pytest.mark.asyncio
async def test_set_target_overrides_setpoint():
    server = build_server(WorkoutSession())
    await server.call_tool("build_workout",
                           {"segments": [{"duration_s": 60, "power_w": 200}], "start": True})
    held = _payload(await server.call_tool("set_target", {"power_w": 333}))
    assert held["erg_setpoint_w"] == 333


@pytest.mark.asyncio
async def test_drive_lifecycle_with_fake_transport():
    srv = InProcessFtmsServer(power_range=(0, 1000, 1))
    session = WorkoutSession()
    server = build_server(session, transport_provider=_fake_provider(srv))
    await server.call_tool("build_workout",
                           {"segments": [{"duration_s": 60, "power_w": 240}], "start": True})

    started = _payload(await server.call_tool("start_drive", {"poll_s": 0.01}))
    assert started["ok"] is True

    import asyncio
    for _ in range(100):
        await asyncio.sleep(0.01)
        if srv.target_power_w == 240:
            break
    assert srv.target_power_w == 240

    ds = _payload(await server.call_tool("drive_status", {}))
    assert ds["active"] is True
    assert ds["controlled"] is True

    stopped = _payload(await server.call_tool("stop_drive", {}))
    assert stopped["active"] is False
    assert srv.controlled is False        # released → resistance neutral
    assert srv.target_power_w is None


@pytest.mark.asyncio
async def test_start_drive_twice_is_rejected():
    srv = InProcessFtmsServer(power_range=(0, 1000, 1))
    server = build_server(WorkoutSession(), transport_provider=_fake_provider(srv))
    await server.call_tool("build_workout",
                           {"segments": [{"duration_s": 60, "power_w": 200}], "start": True})
    first = _payload(await server.call_tool("start_drive", {"poll_s": 0.01}))
    assert first["ok"] is True
    second = _payload(await server.call_tool("start_drive", {"poll_s": 0.01}))
    assert second["ok"] is False
    await server.call_tool("stop_drive", {})
