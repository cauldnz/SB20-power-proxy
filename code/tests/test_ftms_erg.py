"""FTMS erg control (ble/ftms_erg.py) — the pure controller + the hermetic Ride
Director -> erg loop, driven against an in-process fake FTMS machine (no bike).
"""

from __future__ import annotations

from sb20proxy.ble import ftms
from sb20proxy.ble.ftms_erg import (
    ErgController,
    InProcessFtmsServer,
    RideErgBridge,
    drive,
)
from sb20proxy.ride import LiveState, RidePlan, RiderProfile
from sb20proxy.ride.director import Segment


class FakeClock:
    def __init__(self) -> None:
        self.t = 0.0

    def __call__(self) -> float:
        return self.t


# ---- pure controller ----

def test_controller_converges_request_start_settarget():
    server = InProcessFtmsServer(power_range=(0, 500, 1))
    ctrl = ErgController(power_range=server.power_range())
    ctrl.set_desired(100)
    sent = drive(ctrl, server.handle)
    assert sent == 3  # request control, start, set target
    assert ctrl.controlled and ctrl.started
    assert server.controlled and server.started and server.target_power_w == 100


def test_controller_resends_only_on_change():
    server = InProcessFtmsServer()
    ctrl = ErgController(power_range=server.power_range())
    ctrl.set_desired(100)
    assert drive(ctrl, server.handle) == 3
    ctrl.set_desired(100)               # unchanged -> nothing to send
    assert drive(ctrl, server.handle) == 0
    ctrl.set_desired(200)               # changed -> one Set Target Power
    assert drive(ctrl, server.handle) == 1
    assert server.target_power_w == 200


def test_controller_clamps_to_power_range():
    server = InProcessFtmsServer(power_range=(0, 400, 1))
    ctrl = ErgController(power_range=server.power_range())
    ctrl.set_desired(5000)
    drive(ctrl, server.handle)
    assert server.target_power_w == 400
    ctrl.set_desired(-50)
    drive(ctrl, server.handle)
    assert server.target_power_w == 0


def test_controller_handles_control_not_permitted():
    server = InProcessFtmsServer(allow_control=False)
    ctrl = ErgController()
    ctrl.set_desired(150)
    drive(ctrl, server.handle)
    assert not ctrl.controlled
    assert ctrl.last_result == ftms.CP_CONTROL_NOT_PERMITTED
    assert server.target_power_w is None


def test_server_rejects_target_before_control():
    server = InProcessFtmsServer()
    resp = ftms.decode_control_point(server.handle(ftms.encode_set_target_power(200)))
    assert not resp.success and resp.result == ftms.CP_CONTROL_NOT_PERMITTED


# ---- hermetic Ride Director -> erg loop ----

def test_ride_director_drives_erg_end_to_end():
    clk = FakeClock()
    plan = RidePlan("t", [Segment(60, "A", 100), Segment(60, "B", 200)])
    state = LiveState(plan=plan, profile=RiderProfile(ftp_w=250), now_fn=clk)
    state.start_ride()
    server = InProcessFtmsServer(power_range=(0, 500, 1))
    ctrl = ErgController(power_range=server.power_range())
    bridge = RideErgBridge(ctrl, lambda: state.snapshot()["erg_setpoint_w"])

    bridge.pump(server.handle)            # converge to segment A's 100 W
    assert server.controlled and server.started and server.target_power_w == 100

    clk.t = 65.0                          # into segment B (200 W)
    bridge.pump(server.handle)
    assert server.target_power_w == 200

    state.set_hold(power_w=275)           # agent ad-hoc hold overrides the segment
    bridge.pump(server.handle)
    assert server.target_power_w == 275

    state.set_hold(power_w=9999)          # clamped to the machine's max
    bridge.pump(server.handle)
    assert server.target_power_w == 500


def test_bridge_no_setpoint_is_safe():
    # not started -> erg_setpoint_w is None -> controller still claims control but sets no target
    clk = FakeClock()
    state = LiveState(plan=RidePlan("t", [Segment(60, "A", 100)]), now_fn=clk)
    server = InProcessFtmsServer()
    bridge = RideErgBridge(ErgController(power_range=server.power_range()),
                           lambda: state.snapshot()["erg_setpoint_w"])
    bridge.pump(server.handle)
    assert server.controlled and server.started and server.target_power_w is None
