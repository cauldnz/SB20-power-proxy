"""FTMS erg control — drive a trainer's resistance via Set Target Power.

The "power control" half of the FTMS work, and the thing that closes the Ride
Director loop: the director exposes `erg_setpoint_w`, and this writes it to the
SB20's Fitness Machine Control Point (Request Control -> Start -> Set Target Power)
so the bike actually ergs to the workout's target.

Split, like the rest of the project, into a **pure, host-tested core** and a thin
**hardware seam**:
- `ErgController` — the pure state machine: given a desired target and the machine's
  control-point responses, it yields the next command to converge (request control,
  start, set target), clamps to the Supported Power Range, and only re-sends on change.
- `RideErgBridge` — reads `erg_setpoint_w` from a source (the Ride Director's live
  state) and feeds the controller.
- `InProcessFtmsServer` — an in-process fake FTMS machine (the test twin) so the whole
  loop is hermetically testable with no bike.
- `FtmsErgSession` — the seam: a bleak central that runs the controller against the
  REAL SB20 over BLE. Not host-tested (needs the bike); the on-air proof is Session 4 §C.

SPEC-BUILT on `ble.ftms`, pending real-capture validation (Session 4 §C).
"""

from __future__ import annotations

from collections.abc import Callable

from . import ftms

# a transport takes a control-point write and returns the indication bytes (or b"")
Transport = Callable[[bytes], bytes]
SetpointSource = Callable[[], int | None]


class ErgController:
    """Pure erg control state machine. Drives an FTMS machine to a target power by
    sequencing Request Control -> Start -> Set Target Power and consuming the 0x80
    responses. No I/O — `next_command()` emits bytes to write; `on_response()` consumes
    the machine's reply. Clamps to the Supported Power Range; only re-sends Set Target
    Power when the target changes (the natural rate limit)."""

    def __init__(self, power_range: ftms.PowerRange | None = None) -> None:
        self.power_range = power_range
        self.controlled = False        # got success on Request Control
        self.started = False           # got success on Start/Resume
        self.target_w: int | None = None       # last successfully-set target
        self.pending_target: int | None = None  # desired (clamped)
        self.last_result: int | None = None      # last response result code

    def _clamp(self, watts: int) -> int:
        return self.power_range.clamp(watts) if self.power_range else watts

    def set_desired(self, watts: int | None) -> None:
        self.pending_target = None if watts is None else self._clamp(watts)

    def next_command(self) -> bytes | None:
        """The next control-point write to converge toward the desired target, or None
        when there's nothing to do (fully controlled, started, target already set)."""
        if not self.controlled:
            return ftms.encode_request_control()
        if not self.started:
            return ftms.encode_start()
        if self.pending_target is not None and self.pending_target != self.target_w:
            return ftms.encode_set_target_power(self.pending_target)
        return None

    def on_response(self, resp: ftms.ControlPointResponse) -> None:
        self.last_result = resp.result
        if not resp.success:
            # control refused (e.g. control-not-permitted): drop control so we retry /
            # surface it rather than silently believing we own the machine.
            if resp.request_opcode == ftms.CP_REQUEST_CONTROL:
                self.controlled = False
            return
        if resp.request_opcode == ftms.CP_REQUEST_CONTROL:
            self.controlled = True
        elif resp.request_opcode == ftms.CP_START_RESUME:
            self.started = True
        elif resp.request_opcode == ftms.CP_SET_TARGET_POWER:
            self.target_w = self.pending_target


def drive(controller: ErgController, transport: Transport, *, max_steps: int = 12) -> int:
    """Pump the controller against a synchronous transport until it converges (or
    `max_steps`). Returns the number of commands sent. The transport returns the
    machine's indication bytes for each write (the in-process fake, or a bleak
    write+await-indication on the bike)."""
    sent = 0
    for _ in range(max_steps):
        cmd = controller.next_command()
        if cmd is None:
            break
        sent += 1
        reply = transport(cmd)
        if reply:
            msg = ftms.decode_control_point(reply)
            if isinstance(msg, ftms.ControlPointResponse):
                controller.on_response(msg)
    return sent


class RideErgBridge:
    """Glue: read `erg_setpoint_w` from a source (the Ride Director's live state) and
    drive the controller to it. `pump()` does one convergence pass against a transport."""

    def __init__(self, controller: ErgController, get_setpoint: SetpointSource) -> None:
        self.controller = controller
        self.get_setpoint = get_setpoint

    def pump(self, transport: Transport) -> None:
        self.controller.set_desired(self.get_setpoint())
        drive(self.controller, transport)


class InProcessFtmsServer:
    """A faithful in-process FTMS machine for tests — decodes control-point writes with
    `ble.ftms` and replies with the spec responses, tracking control/started/target. The
    hermetic twin of the SB20's control point (no bike). `allow_control=False` models a
    machine that refuses a secondary controller (result 0x05)."""

    def __init__(self, power_range: tuple[int, int, int] = (0, 1000, 1),
                 *, allow_control: bool = True) -> None:
        self.minimum, self.maximum, self.increment = power_range
        self.allow_control = allow_control
        self.controlled = False
        self.started = False
        self.target_power_w: int | None = None

    def power_range(self) -> ftms.PowerRange:
        return ftms.PowerRange(self.minimum, self.maximum, self.increment)

    def handle(self, cmd: bytes) -> bytes:
        msg = ftms.decode_control_point(cmd)
        if not isinstance(msg, ftms.ControlPointRequest):
            return b""
        op = msg.opcode

        def reply(result: int = ftms.CP_SUCCESS) -> bytes:
            return ftms.encode_control_point_response(op, result)

        if op == ftms.CP_REQUEST_CONTROL:
            if not self.allow_control:
                return reply(ftms.CP_CONTROL_NOT_PERMITTED)
            self.controlled = True
            return reply()
        if not self.controlled:
            return reply(ftms.CP_CONTROL_NOT_PERMITTED)
        if op == ftms.CP_START_RESUME:
            self.started = True
            return reply()
        if op == ftms.CP_SET_TARGET_POWER:
            tp = msg.target_power_w
            if tp is None or tp < self.minimum or tp > self.maximum:
                return reply(ftms.CP_INVALID_PARAMETER)
            self.target_power_w = tp
            return reply()
        if op == ftms.CP_RESET:
            self.controlled = self.started = False
            self.target_power_w = None
            return reply()
        return reply(ftms.CP_OP_NOT_SUPPORTED)


class FtmsErgSession:
    """Hardware seam: a bleak central that runs an ErgController against the REAL SB20.
    Connects, subscribes to the control-point indications, and writes the controller's
    commands, reading `erg_setpoint_w` from a source each pump. Not host-tested — the
    on-air proof is the bench loop (F6) / Session 4 §C. bleak is imported lazily so this
    module stays import-clean without it."""

    def __init__(self, address: str, get_setpoint: SetpointSource) -> None:
        self.address = address
        self.controller = ErgController()
        self.bridge = RideErgBridge(self.controller, get_setpoint)
        self._last_indication: bytes = b""

    async def run(self, *, poll_s: float = 1.0, duration_s: float | None = None) -> None:
        import asyncio

        from bleak import BleakClient  # lazy: only needed on the bike

        loop = asyncio.get_event_loop()

        def _on_indicate(_c, data: bytearray) -> None:
            self._last_indication = bytes(data)

        async with BleakClient(self.address, timeout=20.0) as client:
            # read the machine's power range so the controller clamps correctly
            try:
                raw = await client.read_gatt_char(ftms.UUID_SUPPORTED_POWER_RANGE)
                self.controller.power_range = ftms.decode_supported_power_range(raw)
            except Exception:  # noqa: BLE001 — range read is best-effort
                pass
            await client.start_notify(ftms.UUID_FTMS_CONTROL_POINT, _on_indicate)

            async def transport(cmd: bytes) -> bytes:
                self._last_indication = b""
                await client.write_gatt_char(ftms.UUID_FTMS_CONTROL_POINT, cmd, response=True)
                await asyncio.sleep(0.3)  # let the indication land
                return self._last_indication

            end = None if duration_s is None else loop.time() + duration_s
            while end is None or loop.time() < end:
                self.controller.set_desired(self.bridge.get_setpoint())
                for _ in range(4):
                    cmd = self.controller.next_command()
                    if cmd is None:
                        break
                    reply = await transport(cmd)
                    if reply:
                        msg = ftms.decode_control_point(reply)
                        if isinstance(msg, ftms.ControlPointResponse):
                            self.controller.on_response(msg)
                await asyncio.sleep(poll_s)
