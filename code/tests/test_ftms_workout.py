"""ftms_workout.py — the PURE segment-plan builder only (no BLE / hardware / network).

Verifies the structured-interval plan: inputs -> ordered list of (label, watts, secs), the right
reps count, the 430 W intervals + 180 s recoveries, the last-recovery-becomes-cooldown rule, and
the total duration. The on-air driver (BLE) is deliberately NOT exercised here.

`ftms_workout` lives in code/scripts/, which isn't an installed package, so we load it by path.
The module imports `sb20proxy.ble.ftms` at top level (pure codec, no bleak), which the conftest
src-path shim makes importable; bleak is imported lazily inside the driver, never at import time.
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

_SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "ftms_workout.py"
_spec = importlib.util.spec_from_file_location("ftms_workout", _SCRIPT)
assert _spec and _spec.loader
ftms_workout = importlib.util.module_from_spec(_spec)
# Register before exec so dataclasses can introspect Segment's __module__ (the module must be
# in sys.modules by the time @dataclass runs, or its _is_type lookup hits None on 3.12+/3.14).
sys.modules["ftms_workout"] = ftms_workout
_spec.loader.exec_module(ftms_workout)

build_segments = ftms_workout.build_segments
total_secs = ftms_workout.total_secs


def _as_tuples(segs):
    return [(s.label, s.watts, s.secs) for s in segs]


def test_default_plan_shape_and_total():
    segs = build_segments()  # the default 6 x 90s @ 430 W workout
    got = _as_tuples(segs)

    # WARMUP, then [INTERVAL, RECOVERY] x5, then INTERVAL 6 + COOLDOWN (last recovery -> cooldown).
    assert got == [
        ("WARMUP", 130, 180),
        ("INTERVAL 1/6", 430, 90),
        ("RECOVERY 1/6", 100, 180),
        ("INTERVAL 2/6", 430, 90),
        ("RECOVERY 2/6", 100, 180),
        ("INTERVAL 3/6", 430, 90),
        ("RECOVERY 3/6", 100, 180),
        ("INTERVAL 4/6", 430, 90),
        ("RECOVERY 4/6", 100, 180),
        ("INTERVAL 5/6", 430, 90),
        ("RECOVERY 5/6", 100, 180),
        ("INTERVAL 6/6", 430, 90),
        ("COOLDOWN", 100, 120),
    ]

    # total = warmup + 6*interval + 5*recovery + cooldown = 180 + 540 + 900 + 120 = 1740 s
    assert total_secs(segs) == 180 + 6 * 90 + 5 * 180 + 120
    assert total_secs(segs) == 1740


def test_reps_count_and_interval_watts():
    segs = build_segments(reps=6, interval_watts=430, recovery_secs=180)
    intervals = [s for s in segs if s.label.startswith("INTERVAL")]
    recoveries = [s for s in segs if s.label.startswith("RECOVERY")]

    assert len(intervals) == 6                          # exactly `reps` work efforts
    assert all(s.watts == 430 for s in intervals)       # the 430 W intervals
    assert len(recoveries) == 5                         # last recovery replaced by cooldown
    assert all(s.secs == 180 for s in recoveries)       # 180 s recoveries
    assert [s.label for s in intervals] == [f"INTERVAL {i}/6" for i in range(1, 7)]


def test_last_segment_is_cooldown_after_final_interval():
    segs = build_segments(reps=3, cooldown_secs=120, cooldown_watts=100)
    assert segs[-1].label == "COOLDOWN"
    assert segs[-1] == ftms_workout.Segment("COOLDOWN", 100, 120)
    assert segs[-2].label == "INTERVAL 3/3"             # cooldown directly follows the last effort
    # No RECOVERY 3/3 — the final recovery is replaced by the cooldown.
    assert not any(s.label == "RECOVERY 3/3" for s in segs)


def test_reps_one_is_warmup_interval_cooldown():
    segs = build_segments(reps=1)
    assert [s.label for s in segs] == ["WARMUP", "INTERVAL 1/1", "COOLDOWN"]


def test_custom_parameters_flow_through():
    segs = build_segments(
        reps=2, interval_secs=60, interval_watts=380,
        recovery_secs=120, recovery_watts=90,
        warmup_secs=300, warmup_watts=140,
        cooldown_secs=200, cooldown_watts=80,
    )
    assert _as_tuples(segs) == [
        ("WARMUP", 140, 300),
        ("INTERVAL 1/2", 380, 60),
        ("RECOVERY 1/2", 90, 120),
        ("INTERVAL 2/2", 380, 60),
        ("COOLDOWN", 80, 200),
    ]
    assert total_secs(segs) == 300 + 60 + 120 + 60 + 200


def test_zero_warmup_and_cooldown_are_dropped():
    segs = build_segments(reps=2, warmup_secs=0, cooldown_secs=0)
    labels = [s.label for s in segs]
    assert "WARMUP" not in labels
    assert "COOLDOWN" not in labels
    # With no cooldown, the final recovery is also dropped (last rep emits only its interval).
    assert labels == ["INTERVAL 1/2", "RECOVERY 1/2", "INTERVAL 2/2"]
    assert total_secs(segs) == 90 + 180 + 90


def test_segments_are_ordered_label_watts_secs_tuples():
    # The contract the driver relies on: each segment is (label:str, watts:int, secs:int).
    for s in build_segments():
        assert isinstance(s.label, str) and s.label
        assert isinstance(s.watts, int)
        assert isinstance(s.secs, int) and s.secs > 0


def test_async_pump_converges_against_the_twin():
    """The on-bike async path: `_pump()` must drive the controller (Request Control -> Start ->
    Set Target Power) against an ASYNC transport, awaiting each write.

    Regression guard for the session-9 bug: the driver originally called the SYNCHRONOUS
    `ftms_erg.drive()` with an async bleak transport, so the transport coroutine was never awaited
    ('coroutine object is not subscriptable' / 'was never awaited') and nothing reached the machine.
    Run against the hermetic InProcessFtmsServer twin (no BLE) with an async transport that mirrors
    the real bleak write+indication; if `_pump` regressed to sync-calling the transport, the twin
    would never see the commands and these asserts would fail."""
    import asyncio

    from sb20proxy.ble import ftms
    from sb20proxy.ble.ftms_erg import ErgController, InProcessFtmsServer

    srv = InProcessFtmsServer(power_range=(0, 4000, 1))
    ctrl = ErgController(power_range=ftms.PowerRange(0, 4000, 1))

    async def transport(cmd: bytes) -> bytes:   # mirrors a bleak write + awaited indication
        return srv.handle(cmd)

    async def main() -> None:
        ctrl.set_desired(430)
        await ftms_workout._pump(ctrl, transport)

    asyncio.run(main())

    assert ctrl.controlled and ctrl.started          # claimed control + started the machine
    assert ctrl.target_w == 430                       # controller believes the target is set
    assert srv.target_power_w == 430                  # the machine ACTUALLY received 430 W
    assert ctrl.last_result == ftms.CP_SUCCESS
