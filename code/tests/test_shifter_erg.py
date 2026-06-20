"""Shifter -> erg mapper (ble/shifter_erg.py) — pure decode + debounce + nudge pipeline.

Frames are from the real session-3 shifter capture (`<type> 00 <bit:u16 LE>`); the
erg mapping + Set-Target-Power bytes are spec-built on ble.ftms.
"""

from __future__ import annotations

from sb20proxy.ble import ftms
from sb20proxy.ble.shifter_erg import (
    SHIFTER_LEFT_DOWN,
    SHIFTER_LEFT_UP,
    SHIFTER_RIGHT_3RD,
    SHIFTER_RIGHT_UP,
    ErgStep,
    ShifterEdgeDebounce,
    ShifterErgController,
    ShifterErgMapper,
    decode_shifter_button,
)


def hx(s: str) -> bytes:
    return bytes.fromhex(s)


HELD_LEFT_UP = hx("01000100")     # 01 00 0100  -> LEFT up held
HELD_RIGHT_UP = hx("01000800")    # RIGHT up held
HELD_RIGHT_3RD = hx("01002000")   # RIGHT 3rd held
COMMIT_LEFT_UP = hx("030001000100")
TERM_LEFT_UP = hx("04000100")     # terminator


# ---- decode ----

def test_decode_button_bit():
    assert decode_shifter_button(HELD_LEFT_UP) == SHIFTER_LEFT_UP
    assert decode_shifter_button(HELD_RIGHT_3RD) == SHIFTER_RIGHT_3RD
    assert decode_shifter_button(hx("0100")) is None      # too short
    assert decode_shifter_button(hx("01000000")) is None  # zero / release


# ---- debounce ----

def test_debounce_collapses_held_run():
    db = ShifterEdgeDebounce()
    assert db.feed(HELD_LEFT_UP) == SHIFTER_LEFT_UP   # rising edge
    for _ in range(15):
        assert db.feed(HELD_LEFT_UP) is None          # streamed repeats suppressed


def test_debounce_terminator_allows_next_same_press():
    db = ShifterEdgeDebounce()
    assert db.feed(HELD_LEFT_UP) == SHIFTER_LEFT_UP
    assert db.feed(TERM_LEFT_UP) is None               # press boundary
    assert db.feed(HELD_LEFT_UP) == SHIFTER_LEFT_UP    # next press of the same button


def test_debounce_commit_is_a_boundary():
    db = ShifterEdgeDebounce()
    assert db.feed(HELD_LEFT_UP) == SHIFTER_LEFT_UP
    assert db.feed(COMMIT_LEFT_UP) is None
    assert db.feed(HELD_LEFT_UP) == SHIFTER_LEFT_UP


def test_debounce_button_change_is_new_event():
    db = ShifterEdgeDebounce()
    assert db.feed(HELD_LEFT_UP) == SHIFTER_LEFT_UP
    assert db.feed(HELD_RIGHT_UP) == SHIFTER_RIGHT_UP  # different bit -> new event


# ---- mapper ----

def test_mapper_nudges_fine_and_coarse_and_clamps():
    m = ShifterErgMapper(target_w=150, step=ErgStep(small=5, big=25),
                         power_range=ftms.PowerRange(0, 200, 1))
    assert m.on_button(SHIFTER_LEFT_UP) == 155     # +fine
    assert m.on_button(SHIFTER_RIGHT_UP) == 180    # +coarse
    assert m.on_button(SHIFTER_RIGHT_UP) == 200    # clamps at max
    assert m.on_button(SHIFTER_LEFT_DOWN) == 195   # -fine
    assert m.on_button(SHIFTER_RIGHT_3RD) is None  # 3rd button unmapped


def test_mapper_clamps_at_zero_without_range():
    m = ShifterErgMapper(target_w=3, step=ErgStep(small=5, big=25))
    assert m.on_button(SHIFTER_LEFT_DOWN) == 0     # 3-5 -> clamped to 0


# ---- full pipeline ----

def test_controller_press_emits_set_target_power_once():
    ctrl = ShifterErgController(ShifterErgMapper(target_w=150, step=ErgStep(5, 25)))
    cmd = ctrl.feed(HELD_LEFT_UP)
    assert cmd == ftms.encode_set_target_power(155)
    assert ctrl.feed(HELD_LEFT_UP) is None         # held repeat -> no command
    ctrl.feed(TERM_LEFT_UP)
    assert ctrl.feed(HELD_LEFT_UP) == ftms.encode_set_target_power(160)  # next press


def test_controller_third_button_emits_nothing():
    ctrl = ShifterErgController()
    assert ctrl.feed(HELD_RIGHT_3RD) is None
