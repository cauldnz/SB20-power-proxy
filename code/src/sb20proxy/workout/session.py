"""WorkoutSession — the in-memory session the MCP workout tools drive.

Owns a `ride.state.LiveState` and exposes the agent-facing verbs as plain methods so the
whole vocabulary is host-tested without any MCP transport or HTTP:

    build_workout / list_workouts        — compose + load a plan
    start / stop / skip / goto / extend  — lifecycle + live navigation
    set_target / message / set_profile   — ad-hoc override + coaching + FTP
    status                               — the monitoring snapshot (control_state)

The erg drive is kept as a transport-injected pump (`drive_pump` / `release`) so it too is
host-tested against the in-process FTMS twin (`ble.ftms_erg.InProcessFtmsServer`); the real
bleak on-air loop is the hardware seam, layered in the MCP server (`ble.ftms_erg.FtmsErgSession`).
The setpoint the pump chases is `LiveState.snapshot()["erg_setpoint_w"]` — the active segment's
resolved target, or an ad-hoc hold — so set_target / skip / extend all steer the erg for free.
"""

from __future__ import annotations

import time
from collections.abc import Callable
from typing import Any

from ..ble import ftms
from ..ble.ftms_erg import ErgController, RideErgBridge, Transport
from ..ride.control import apply_control, control_state
from ..ride.director import RiderProfile
from ..ride.state import LiveState
from ..ride.workouts import WORKOUTS
from .builder import WorkoutSpecError, build_plan


class WorkoutSession:
    """A single live workout the agent composes and drives. Not thread-safe beyond what
    `LiveState` already guards; one session per driven bike."""

    def __init__(
        self,
        *,
        ftp_w: int = 250,
        scale: str = "stages",
        now_fn: Callable[[], float] = time.monotonic,
    ) -> None:
        self.state = LiveState(profile=RiderProfile(ftp_w=ftp_w, scale=scale), now_fn=now_fn)
        self._erg = ErgController()
        self._bridge = RideErgBridge(self._erg, self.erg_setpoint)

    # ---- build / load -------------------------------------------------------------------

    def build_workout(self, spec: Any, *, name: str | None = None,
                      start: bool = False) -> dict[str, Any]:
        """Build a plan from any supported spec (name / structured dict / shorthand) and load
        it. With `start=True`, begin the ride immediately. Returns the status snapshot."""
        plan = build_plan(spec, name=name)
        if not plan.segments:
            raise WorkoutSpecError("workout has no segments")
        self.state.replace_plan(plan)
        if start:
            self.state.start_ride()
        return self.status()

    @staticmethod
    def list_workouts() -> list[dict[str, Any]]:
        """The built-in workout library: each entry's key, name, segment count, and minutes."""
        out: list[dict[str, Any]] = []
        for key, w in WORKOUTS.items():
            out.append({
                "key": key,
                "name": w.name,
                "segments": len(w.segments),
                "total_s": w.total_s,
                "total_min": round(w.total_s / 60.0, 1),
            })
        return out

    # ---- lifecycle + navigation ---------------------------------------------------------

    def start(self) -> dict[str, Any]:
        self.state.start_ride()
        return self.status()

    def stop(self) -> dict[str, Any]:
        """Stop the ride (release the clock + cursor). The on-air erg release is the driver's
        job — see `release()`."""
        self.state.stop_ride()
        return self.status()

    def skip(self) -> dict[str, Any]:
        apply_control(self.state, "skip", {})
        return self.status()

    def goto(self, index: int) -> dict[str, Any]:
        apply_control(self.state, "goto", {"index": index})
        return self.status()

    def extend(self, seconds: float) -> dict[str, Any]:
        apply_control(self.state, "extend", {"seconds": seconds})
        return self.status()

    # ---- ad-hoc target + coaching + profile ---------------------------------------------

    def set_target(self, *, power_w: int | None = None, pct_ftp: float | None = None,
                   cadence_rpm: int | None = None, duration_s: float | None = None,
                   clear: bool = False) -> dict[str, Any]:
        """Set (or clear) an ad-hoc hold target that supersedes the segment target — and so
        the erg setpoint — until cleared or `duration_s` elapses."""
        body: dict[str, Any] = {"clear": True} if clear else {
            "power_w": power_w, "pct_ftp": pct_ftp,
            "cadence_rpm": cadence_rpm, "duration_s": duration_s,
        }
        apply_control(self.state, "target", body)
        return self.status()

    def message(self, text: str, *, level: str = "info",
                ttl_s: float | None = None) -> dict[str, Any]:
        apply_control(self.state, "message", {"text": text, "level": level, "ttl_s": ttl_s})
        return self.status()

    def set_profile(self, *, ftp_w: int | None = None,
                    scale: str | None = None) -> dict[str, Any]:
        body: dict[str, Any] = {}
        if ftp_w is not None:
            body["ftp_w"] = ftp_w
        if scale is not None:
            body["scale"] = scale
        apply_control(self.state, "profile", body)
        return self.status()

    # ---- monitoring ---------------------------------------------------------------------

    def status(self) -> dict[str, Any]:
        """The agent's monitoring view: live snapshot + the resolved plan timeline."""
        return control_state(self.state)

    def erg_setpoint(self) -> int | None:
        """The watts the erg should currently hold (active segment target or ad-hoc hold).
        None means 'no target' (free / coast) — the driver leaves resistance where it is."""
        return self.state.snapshot()["erg_setpoint_w"]

    # ---- erg drive (transport-injected; the bleak seam lives in the MCP server) ----------

    @property
    def erg_controller(self) -> ErgController:
        return self._erg

    def drive_pump(self, transport: Transport) -> None:
        """One convergence pass: read the current setpoint and drive the FTMS control point
        toward it via `transport` (Request Control → Start → Set Target Power). Host-tested
        against `InProcessFtmsServer`; on the bike `transport` is a bleak write+await."""
        self._bridge.pump(transport)

    def release(self, transport: Transport) -> None:
        """Hand the bike back: send FTMS Reset so resistance returns to neutral, and forget we
        own the machine. ALWAYS call this on stop / error — never leave the rider at target."""
        try:
            transport(ftms.encode_reset())
        finally:
            self._erg.controlled = False
            self._erg.started = False
            self._erg.target_w = None
            self._erg.pending_target = None
