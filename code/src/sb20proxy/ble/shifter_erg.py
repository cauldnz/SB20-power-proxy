"""Shifter buttons -> erg target — the SB20's missing feature (nudge watts from the bars).

Pure, host-tested pipeline (per `code/findings/shifter-erg-control.md`): a shifter
notification on char 0c46be60 -> a debounced button event -> the held erg target
+/- a step (clamped to the Supported Power Range) -> FTMS Set Target Power bytes.

The shifter is **stateless** (the consumer owns the target). Notifications are
`<type:u8> 00 <bitmask:u16 LE>` and the held frame streams ~10-20x per press, so we
**debounce** by collapsing a run of the same bit into one logical event (grounded in
the session-3 capture). Whether a *hold* fires repeated shifts is UNCONFIRMED
(session 4 §B), so we default to one event per physical press.

The output bytes are produced by `ble.ftms`; this is the translation layer that sits
between the shifter input and the FTMS erg control (`ble.ftms_erg`). SPEC-BUILT on the
FTMS codec; the shifter bits themselves are from the real session-3 capture.
"""

from __future__ import annotations

from dataclasses import dataclass

from . import ftms

# One-hot button bits (char 0c46be60), from the real session-3 capture
# (shifter-ble-protocol.md). The bitmask is a u16 LE at bytes 2-3 of a frame.
SHIFTER_LEFT_UP = 0x0001
SHIFTER_LEFT_DOWN = 0x0002
SHIFTER_LEFT_3RD = 0x0004
SHIFTER_RIGHT_UP = 0x0008
SHIFTER_RIGHT_DOWN = 0x0010
SHIFTER_RIGHT_3RD = 0x0020


SHIFTER_FRAME_HELD = 0x01     # `01 00 <bit>` — streamed while held
SHIFTER_FRAME_COMMIT = 0x03   # `03 00 <bit> <bit>` — the shift commit
# 0x04 / 0x08 are press terminators (the open detail); anything non-held ends a press.


def decode_shifter_button(frame: bytes) -> int | None:
    """The one-hot button bit from a shifter notification `<type> 00 <bit:u16 LE>[ ...]`.
    Returns the bit, or None for a too-short / zero (release) frame."""
    if len(frame) < 4:
        return None
    bit = frame[2] | (frame[3] << 8)
    return bit or None


class ShifterEdgeDebounce:
    """One logical event per physical press: emit a button on the rising edge of its
    held run and suppress the streamed repeats; a non-held frame (commit / terminator /
    release) ends the press so the same button can fire again next time. Keys off the
    `01` vs `03/04/08` frame bracket exactly as shifter-erg-control.md prescribes."""

    def __init__(self) -> None:
        self._active: int | None = None

    def feed(self, frame: bytes) -> int | None:
        if len(frame) < 4:
            self._active = None
            return None
        ftype = frame[0]
        bit = frame[2] | (frame[3] << 8)
        if ftype != SHIFTER_FRAME_HELD or bit == 0:
            self._active = None           # commit / terminator / release -> press boundary
            return None
        if bit == self._active:
            return None                   # streamed repeat of the held button -> suppressed
        self._active = bit
        return bit                        # rising edge -> one event


@dataclass(frozen=True)
class ErgStep:
    """How much each side nudges the erg target (watts). LEFT = fine, RIGHT = coarse."""

    small: int = 5
    big: int = 25


class ShifterErgMapper:
    """Holds the erg target and nudges it by a button's step (clamped). Default mapping
    (MVP from shifter-erg-control.md): LEFT up/down = +/- fine, RIGHT up/down = +/- coarse;
    the 3rd buttons are unmapped (reserved as control)."""

    def __init__(self, *, target_w: int = 150, power_range: ftms.PowerRange | None = None,
                 step: ErgStep | None = None, mapping: dict[int, int] | None = None) -> None:
        step = step or ErgStep()
        self.target_w = target_w
        self.power_range = power_range
        self.mapping = mapping or {
            SHIFTER_LEFT_UP: step.small, SHIFTER_LEFT_DOWN: -step.small,
            SHIFTER_RIGHT_UP: step.big, SHIFTER_RIGHT_DOWN: -step.big,
        }
        self.target_w = self._clamp(target_w)

    def _clamp(self, watts: int) -> int:
        return self.power_range.clamp(watts) if self.power_range else max(0, watts)

    def on_button(self, button: int) -> int | None:
        """Apply a button's nudge; returns the new (clamped) target, or None if the
        button isn't an erg button (e.g. a 3rd/control button)."""
        delta = self.mapping.get(button)
        if delta is None:
            return None
        self.target_w = self._clamp(self.target_w + delta)
        return self.target_w

    def set_target_command(self) -> bytes:
        return ftms.encode_set_target_power(self.target_w)


class ShifterErgController:
    """The full pure pipeline: a shifter frame in -> Set Target Power bytes out (or None
    when the frame is a held-repeat, a release, or a non-erg button). Wire this between
    the shifter notifications and the FTMS control point write."""

    def __init__(self, mapper: ShifterErgMapper | None = None,
                 debounce: ShifterEdgeDebounce | None = None) -> None:
        self.mapper = mapper or ShifterErgMapper()
        self.debounce = debounce or ShifterEdgeDebounce()

    def feed(self, frame: bytes) -> bytes | None:
        button = self.debounce.feed(frame)
        if button is None:
            return None
        if self.mapper.on_button(button) is None:
            return None
        return self.mapper.set_target_command()
