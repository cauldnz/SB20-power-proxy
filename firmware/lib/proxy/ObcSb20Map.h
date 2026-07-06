#pragma once
#include <cstddef>
#include <cstdint>
#include "Shifter.h"
#include "Obc.h"

namespace sb20proxy {

// Default SB20 handlebar-button -> OpenBikeControl action mapping. Uses OBC's "multiple actions per
// button" (PROTOCOL.md) so a single press works across apps without mode-switching: the paddles emit
// BOTH a gear shift and an ERG-difficulty step, so a shifting app shifts and an erg app bumps power off
// the same press. Pure + header-only -> host-tested. This is the built-in default; it becomes runtime-
// configurable in M5 (see code/findings/obc-protocol.md, issue #247).
//
//   LEFT/RIGHT up    -> Shift Up   + ERG Up       (either paddle shifts / raises difficulty)
//   LEFT/RIGHT down  -> Shift Down + ERG Down
//   LEFT 3rd         -> Lap
//   RIGHT 3rd        -> Menu

// Fill `ids` with the OBC button IDs a SB20 button maps to; returns the count (0 = unmapped / no fit).
// The caller applies state: send all PRESSED, then all RELEASED, for a momentary click.
inline size_t defaultSb20ObcButtonIds(ShifterButton btn, uint8_t* ids, size_t cap) {
    uint8_t tmp[2];
    size_t n = 0;
    switch (btn) {
        case ShifterButton::LeftUp:    tmp[0] = OBC_BTN_SHIFT_UP;   tmp[1] = OBC_BTN_ERG_UP;   n = 2; break;
        case ShifterButton::RightUp:   tmp[0] = OBC_BTN_SHIFT_UP;   tmp[1] = OBC_BTN_ERG_UP;   n = 2; break;
        case ShifterButton::LeftDown:  tmp[0] = OBC_BTN_SHIFT_DOWN; tmp[1] = OBC_BTN_ERG_DOWN; n = 2; break;
        case ShifterButton::RightDown: tmp[0] = OBC_BTN_SHIFT_DOWN; tmp[1] = OBC_BTN_ERG_DOWN; n = 2; break;
        case ShifterButton::Left3:     tmp[0] = OBC_BTN_LAP;                                   n = 1; break;
        case ShifterButton::Right3:    tmp[0] = OBC_BTN_MENU;                                  n = 1; break;
        default: return 0;  // None / unknown
    }
    if (ids == nullptr || n > cap) return 0;
    for (size_t i = 0; i < n; ++i) ids[i] = tmp[i];
    return n;
}

// Encode the full OBC ButtonState message for a SB20 button at `state` (M2 map + M1 codec composed).
// Returns bytes written (0 if the button is unmapped or the buffer is too small).
inline size_t encodeSb20ButtonState(ShifterButton btn, uint8_t state, uint8_t* out, size_t cap) {
    uint8_t ids[2];
    const size_t n = defaultSb20ObcButtonIds(btn, ids, 2);
    if (n == 0) return 0;
    ObcAction acts[2];
    for (size_t i = 0; i < n; ++i) { acts[i].id = ids[i]; acts[i].state = state; }
    return encodeButtonState(acts, n, out, cap);
}

}  // namespace sb20proxy
