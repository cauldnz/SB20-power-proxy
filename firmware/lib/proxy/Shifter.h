#pragma once
#include <cstddef>
#include <cstdint>

namespace sb20proxy {

// SB20 shifter buttons — the on-device decode of the vendor characteristic 0c46be60, grounded in the
// real session-3 capture (code/findings/shifter-ble-protocol.md). A notification is
// `<type:u8> 00 <bit:u16 LE>[ <bit:u16 LE>]`; the bit is a ONE-HOT identifier of WHICH of the 6
// buttons was pressed (the SB20 is STATELESS — it owns no gear). The held frame (type 0x01) streams
// ~10-20x per press, so any consumer must DEBOUNCE to one logical event per press. This is the C++
// mirror of code/src/sb20proxy/ble/shifter_erg.py (same bits, same edge-debounce). Pure + header-only
// → host-tested with golden vectors from the capture; no hardware. The foundation for re-presenting
// the shifter to a training app (Zwift Click) and for nudging the erg target on-device.

// One-hot button bits (u16 LE at bytes 2-3 of a frame).
inline constexpr uint16_t SHIFTER_LEFT_UP    = 0x0001;  // LEFT  up
inline constexpr uint16_t SHIFTER_LEFT_DOWN  = 0x0002;  // LEFT  down
inline constexpr uint16_t SHIFTER_LEFT_3RD   = 0x0004;  // LEFT  3rd
inline constexpr uint16_t SHIFTER_RIGHT_UP   = 0x0008;  // RIGHT up
inline constexpr uint16_t SHIFTER_RIGHT_DOWN = 0x0010;  // RIGHT down
inline constexpr uint16_t SHIFTER_RIGHT_3RD  = 0x0020;  // RIGHT 3rd

inline constexpr uint8_t SHIFTER_FRAME_HELD   = 0x01;  // `01 00 <bit>` — streamed while held
inline constexpr uint8_t SHIFTER_FRAME_COMMIT = 0x03;  // `03 00 <bit> <bit>` — the shift commit
// 0x04 / 0x08 are press terminators (04 = gear changed, 08 = no-change/at-limit); both end a press.

enum class ShifterButton : uint16_t {
    None = 0,
    LeftUp = SHIFTER_LEFT_UP, LeftDown = SHIFTER_LEFT_DOWN, Left3 = SHIFTER_LEFT_3RD,
    RightUp = SHIFTER_RIGHT_UP, RightDown = SHIFTER_RIGHT_DOWN, Right3 = SHIFTER_RIGHT_3RD,
};

inline ShifterButton shifterButtonFromBit(uint16_t bit) {
    switch (bit) {
        case SHIFTER_LEFT_UP: return ShifterButton::LeftUp;
        case SHIFTER_LEFT_DOWN: return ShifterButton::LeftDown;
        case SHIFTER_LEFT_3RD: return ShifterButton::Left3;
        case SHIFTER_RIGHT_UP: return ShifterButton::RightUp;
        case SHIFTER_RIGHT_DOWN: return ShifterButton::RightDown;
        case SHIFTER_RIGHT_3RD: return ShifterButton::Right3;
        default: return ShifterButton::None;  // 0, multi-bit, or an unknown bit
    }
}

inline const char* shifterButtonName(ShifterButton b) {
    switch (b) {
        case ShifterButton::LeftUp: return "LEFT up";
        case ShifterButton::LeftDown: return "LEFT down";
        case ShifterButton::Left3: return "LEFT 3rd";
        case ShifterButton::RightUp: return "RIGHT up";
        case ShifterButton::RightDown: return "RIGHT down";
        case ShifterButton::Right3: return "RIGHT 3rd";
        default: return "(none)";
    }
}

// The one-hot button bit carried by a notification (0 = too short, or a zero/release frame).
inline uint16_t decodeShifterButtonBit(const uint8_t* data, size_t len) {
    if (len < 4) return 0;
    return (uint16_t)(data[2] | (data[3] << 8));
}

// One logical event per physical press: fire on the RISING EDGE of a held run and suppress the
// streamed repeats; a non-held frame (commit / terminator / release / short) ends the press, so the
// same button can fire again on its next press. The mirror of shifter_erg.py's ShifterEdgeDebounce —
// keyed off the `01` (held) vs `03`/`04`/`08` (bracket) frames exactly as the protocol prescribes.
class ShifterDebounce {
public:
    // Returns the pressed button on a fresh press, else ShifterButton::None (repeat / boundary).
    ShifterButton feed(const uint8_t* data, size_t len) {
        if (len < 4) { active_ = 0; return ShifterButton::None; }
        const uint8_t type = data[0];
        const uint16_t bit = (uint16_t)(data[2] | (data[3] << 8));
        if (type != SHIFTER_FRAME_HELD || bit == 0) {  // commit / terminator / release -> boundary
            active_ = 0;
            return ShifterButton::None;
        }
        if (bit == active_) return ShifterButton::None;  // streamed repeat of the held button
        active_ = bit;                                   // rising edge -> one event
        return shifterButtonFromBit(bit);
    }
    void reset() { active_ = 0; }

private:
    uint16_t active_ = 0;
};

}  // namespace sb20proxy
