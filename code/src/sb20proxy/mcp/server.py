"""The MCP server: the SB20 erg as agent-drivable workout tools (forward-plan §13).

`build_server()` registers a `workout.WorkoutSession` as MCP tools — compose / load / steer a
workout and monitor it — plus the live erg drive (`ErgDriver`). The session core and the drive
loop are host-tested with no bike; the only hardware seam is the **transport provider** that
connects to the real SB20 over bleak, which is injectable so the drive tools are testable with the
in-process FTMS twin.

Tools:
  list_workouts                         the built-in workout library
  build_workout(spec|segments, ...)     compose + load a plan (optionally start it)
  start / stop / skip / goto / extend   ride lifecycle + live navigation
  set_target / message / set_profile    ad-hoc hold override / coaching banner / FTP
  status                                the monitoring snapshot
  start_drive / stop_drive / drive_status   the on-air erg loop (Set Target Power)

Requires the optional MCP SDK: ``pip install -e ".[mcp]"``.
"""

from __future__ import annotations

import asyncio
from collections.abc import Awaitable, Callable
from typing import Any

from ..ble import ftms
from ..ble.ftms_erg import ErgController
from ..workout.session import WorkoutSession
from .driver import AsyncTransport, ErgDriver

# the SB20's FTMS identity (G-sb20-ftms-erg recon)
DEFAULT_ADDRESS = "E4:AA:5A:D6:0E:D4"

# provider(address, controller) -> (transport, aclose). The default connects over bleak; tests
# inject a fake wrapping InProcessFtmsServer. The provider may set controller.power_range.
TransportProvider = Callable[
    [str | None, ErgController],
    Awaitable[tuple[AsyncTransport, Callable[[], Awaitable[None]]]],
]


async def _bleak_provider(
    address: str | None, controller: ErgController
) -> tuple[AsyncTransport, Callable[[], Awaitable[None]]]:
    """The hardware seam: connect to the SB20 over bleak, subscribe to control-point
    indications, and return a write+await-indication transport (+ a disconnect closer).
    Not host-tested — the bike is the proof. bleak is imported lazily."""
    from bleak import BleakClient

    client = BleakClient(address or DEFAULT_ADDRESS, timeout=20.0)
    await client.connect()
    try:
        raw = await client.read_gatt_char(ftms.UUID_SUPPORTED_POWER_RANGE)
        controller.power_range = ftms.decode_supported_power_range(raw)
    except Exception:  # noqa: BLE001 — range read is best-effort
        pass
    last: dict[str, bytes] = {"v": b""}

    def _on_indicate(_c: Any, data: bytearray) -> None:
        last["v"] = bytes(data)

    await client.start_notify(ftms.UUID_FTMS_CONTROL_POINT, _on_indicate)

    async def transport(cmd: bytes) -> bytes:
        last["v"] = b""
        await client.write_gatt_char(ftms.UUID_FTMS_CONTROL_POINT, cmd, response=True)
        await asyncio.sleep(0.3)  # let the indication land
        return last["v"]

    async def aclose() -> None:
        try:
            await client.disconnect()
        except Exception:  # noqa: BLE001 — already tearing down
            pass

    return transport, aclose


def build_server(
    session: WorkoutSession | None = None,
    *,
    transport_provider: TransportProvider | None = None,
    name: str = "sb20-workout",
) -> Any:
    """Build the FastMCP server around a WorkoutSession. Importing FastMCP is deferred so this
    module only needs the MCP SDK when actually building the server."""
    from mcp.server.fastmcp import FastMCP

    session = session or WorkoutSession()
    provider = transport_provider or _bleak_provider
    # one drive at a time: the live driver + its transport closer
    drive: dict[str, Any] = {"driver": None, "aclose": None}

    mcp = FastMCP(name, instructions=(
        "Drive a Stages SB20 smart bike through structured erg workouts. Compose a plan with "
        "build_workout (a built-in name, a 'segments' list, or a shorthand like "
        "'5min @ 130W; 6x(90s @ 430W; 3min @ 100W)'), start it, then connect the bike with "
        "start_drive to hold each segment's target power. Steer live with skip / goto / extend / "
        "set_target, and watch progress with status. ALWAYS stop_drive when done — it returns "
        "the bike's resistance to neutral."
    ))

    # ---- compose / library ----

    @mcp.tool()
    def list_workouts() -> list[dict[str, Any]]:
        """List the built-in workout library (key, name, segment count, minutes)."""
        return session.list_workouts()

    @mcp.tool()
    def build_workout(
        spec: str | None = None,
        segments: list[dict] | None = None,
        name: str | None = None,
        start: bool = False,
    ) -> dict[str, Any]:
        """Build and load a workout. Pass a built-in `spec` name or a shorthand string, OR a
        structured `segments` list ([{duration_s, label?, power_w?|pct_ftp?|zone?, cadence_rpm?},
        ...], `repeat` nodes allowed). With start=True, begin the ride immediately."""
        if segments is not None:
            spec_obj: Any = {"name": name or "Custom workout", "segments": segments}
        elif spec is not None:
            spec_obj = spec
        else:
            return {"ok": False, "error": "provide either 'spec' or 'segments'"}
        return session.build_workout(spec_obj, name=name, start=start)

    # ---- lifecycle + navigation ----

    @mcp.tool()
    def start() -> dict[str, Any]:
        """Start (or restart) the loaded workout's clock."""
        return session.start()

    @mcp.tool()
    def stop() -> dict[str, Any]:
        """Stop the ride clock. Use stop_drive to also release the bike's resistance."""
        return session.stop()

    @mcp.tool()
    def skip() -> dict[str, Any]:
        """Advance to the next segment now."""
        return session.skip()

    @mcp.tool()
    def goto(index: int) -> dict[str, Any]:
        """Jump to segment `index` and restart it now."""
        return session.goto(index)

    @mcp.tool()
    def extend(seconds: float) -> dict[str, Any]:
        """Lengthen (or, with a negative value, shorten) the active segment by `seconds`."""
        return session.extend(seconds)

    # ---- ad-hoc target / coaching / profile ----

    @mcp.tool()
    def set_target(
        power_w: int | None = None,
        pct_ftp: float | None = None,
        cadence_rpm: int | None = None,
        duration_s: float | None = None,
        clear: bool = False,
    ) -> dict[str, Any]:
        """Set (clear=False) or clear (clear=True) an ad-hoc hold target that supersedes the
        segment target — and the erg setpoint — until cleared or `duration_s` elapses."""
        return session.set_target(power_w=power_w, pct_ftp=pct_ftp, cadence_rpm=cadence_rpm,
                                  duration_s=duration_s, clear=clear)

    @mcp.tool()
    def message(text: str, level: str = "info", ttl_s: float | None = None) -> dict[str, Any]:
        """Push a coaching banner message to the rider's phone."""
        return session.message(text, level=level, ttl_s=ttl_s)

    @mcp.tool()
    def set_profile(ftp_w: int | None = None, scale: str | None = None) -> dict[str, Any]:
        """Update the rider's FTP / meter scale (re-resolves %FTP and zone targets)."""
        return session.set_profile(ftp_w=ftp_w, scale=scale)

    @mcp.tool()
    def status() -> dict[str, Any]:
        """The monitoring snapshot: live state, the active segment, the resolved plan, and the
        current erg setpoint."""
        return session.status()

    # ---- on-air erg drive ----

    def _drive_status() -> dict[str, Any]:
        drv: ErgDriver | None = drive["driver"]
        return {
            "active": bool(drv and drv.active),
            "erg_setpoint_w": session.erg_setpoint(),
            "controlled": bool(drv and drv.controller.controlled),
            "target_w": drv.controller.target_w if drv else None,
            "last_error": drv.last_error if drv else None,
        }

    @mcp.tool()
    async def start_drive(address: str | None = None, poll_s: float = 1.0) -> dict[str, Any]:
        """Connect to the SB20 and start holding the live erg setpoint. Safety: stop_drive (or
        any error) returns resistance to neutral."""
        drv: ErgDriver | None = drive["driver"]
        if drv and drv.active:
            return {"ok": False, "error": "drive already active", **_drive_status()}
        controller = ErgController()
        try:
            transport, aclose = await provider(address, controller)
        except Exception as e:  # noqa: BLE001 — surface a connect failure to the agent
            return {"ok": False, "error": f"connect failed: {e}"}
        new = ErgDriver(controller, session.erg_setpoint, transport, poll_s=poll_s)
        drive["driver"] = new
        drive["aclose"] = aclose
        new.start()
        return {"ok": True, **_drive_status()}

    @mcp.tool()
    async def stop_drive() -> dict[str, Any]:
        """Stop the erg drive and release the bike (FTMS Reset → resistance neutral)."""
        drv: ErgDriver | None = drive["driver"]
        if drv is not None:
            await drv.stop()
        aclose = drive["aclose"]
        if aclose is not None:
            try:
                await aclose()
            except Exception:  # noqa: BLE001 — best-effort teardown
                pass
        drive["driver"] = None
        drive["aclose"] = None
        return {"ok": True, "active": False}

    @mcp.tool()
    def drive_status() -> dict[str, Any]:
        """Whether the erg drive is active and what target it's holding."""
        return _drive_status()

    return mcp
