"""MCP server that exposes the SB20 erg as agent-drivable workout tools (forward-plan §13).

`server.build_server()` wraps a `workout.WorkoutSession` as MCP tools (compose / load / steer a
workout + monitor it) plus the live erg drive (`driver.ErgDriver`). The session core and the
driver loop are host-tested with no bike; only the bleak BLE transport in `server` is the
hardware seam. The MCP SDK is an optional dependency — install `sb20proxy[mcp]` to run the
server; `mcp.driver` itself imports nothing from the SDK so the drive loop is testable without it.
"""

from __future__ import annotations

from .driver import ErgDriver

__all__ = ["ErgDriver"]
