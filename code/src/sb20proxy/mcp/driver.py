"""ErgDriver — the background erg-drive loop behind the MCP start_drive / stop_drive tools.

Reads the live erg setpoint (the active segment's target, or an ad-hoc hold) and converges the
FTMS machine to it on a fixed cadence, and — the safety invariant — **ALWAYS sends Reset on the
way out** (resistance → neutral), whether the loop ends normally, by `stop()`, or on error. This
is the teardown `ble.ftms_erg.FtmsErgSession` lacks; the MCP server must never leave the rider
grinding at an interval target because the agent/connection went away.

The transport is injected (an async `bytes -> bytes` control-point write), so the whole loop is
host-tested against the in-process FTMS twin; on the bike it's a bleak write+await-indication
(`server.py`). No MCP-SDK import here — the loop is testable without the optional dependency.
"""

from __future__ import annotations

import asyncio
from collections.abc import Awaitable, Callable

from ..ble import ftms
from ..ble.ftms_erg import ErgController

AsyncTransport = Callable[[bytes], Awaitable[bytes]]
SetpointSource = Callable[[], "int | None"]


class ErgDriver:
    """Owns one ErgController and an asyncio task that pumps it toward the live setpoint."""

    def __init__(
        self,
        controller: ErgController,
        get_setpoint: SetpointSource,
        transport: AsyncTransport,
        *,
        poll_s: float = 1.0,
    ) -> None:
        self.controller = controller
        self.get_setpoint = get_setpoint
        self.transport = transport
        self.poll_s = poll_s
        self.running = False
        self.last_error: str | None = None
        self._task: asyncio.Task[None] | None = None

    @property
    def active(self) -> bool:
        return self._task is not None and not self._task.done()

    async def _converge_once(self) -> None:
        """One pass: read the setpoint and send whatever commands move us toward it
        (Request Control → Start → Set Target Power), consuming each reply."""
        self.controller.set_desired(self.get_setpoint())
        for _ in range(6):
            cmd = self.controller.next_command()
            if cmd is None:
                break
            reply = await self.transport(cmd)
            if reply:
                msg = ftms.decode_control_point(reply)
                if isinstance(msg, ftms.ControlPointResponse):
                    self.controller.on_response(msg)

    async def run(self, *, duration_s: float | None = None) -> None:
        """Drive until `duration_s` elapses, the task is cancelled, or an error escapes — then
        ALWAYS release. Run directly (`await driver.run()`) or via `start()`/`stop()`."""
        self.running = True
        loop = asyncio.get_event_loop()
        end = None if duration_s is None else loop.time() + duration_s
        try:
            while end is None or loop.time() < end:
                await self._converge_once()
                await asyncio.sleep(self.poll_s)
        finally:
            self.running = False
            await self._release()

    async def _release(self) -> None:
        """Send FTMS Reset (resistance → neutral) and forget we own the machine. Best-effort:
        we're tearing down, so a transport failure here is recorded, not raised."""
        try:
            await self.transport(ftms.encode_reset())
        except Exception as e:  # noqa: BLE001 — teardown is best-effort
            self.last_error = f"reset on release failed: {e}"
        finally:
            self.controller.controlled = False
            self.controller.started = False
            self.controller.target_w = None
            self.controller.pending_target = None

    def start(self, *, duration_s: float | None = None) -> bool:
        """Spawn the drive loop on the running event loop. Returns False if already active."""
        if self.active:
            return False
        self.last_error = None
        self._task = asyncio.ensure_future(self.run(duration_s=duration_s))
        return True

    async def stop(self) -> None:
        """Cancel the loop and await its teardown (which sends Reset). Safe to call when idle."""
        task = self._task
        self._task = None
        if task is None:
            return
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass  # run()'s finally already released the machine
