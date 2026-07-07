#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "Obc.h"      // OBC button ids + OBC_STATE_* (the re-broadcast targets)
#include "Shifter.h"  // ShifterButton (the 6 physical SB20 buttons)

namespace sb20proxy {

// The user-configurable "what does each SB20 button do" map (edited in the ESP web UI, persisted to NVS,
// shared by the nRF via lib_extra_dirs). Each of the 6 physical buttons binds to ONE action token; a
// press resolves to either an OBC re-broadcast (a training app consumes it) or a LOCAL device action (a
// nudge to our own erg target), or nothing. Pure + header-only -> host-tested; the firmware seams just
// route the resolved action. Replaces the old fixed ObcSb20Map default with a per-user binding.

enum class Sb20ActionKind : uint8_t { None, Obc, ErgBias };

// A plain aggregate (no default member initializers) so brace-init works on the nRF's older C++
// toolchain too — every use below constructs it explicitly.
struct Sb20ActionSpec {
    Sb20ActionKind kind;
    uint8_t obcId;    // kind==Obc: the OBC button id to emit (a momentary press+release)
    int8_t ergDelta;  // kind==ErgBias: the local erg-target nudge in W (+/-)
};

// The selectable actions (stable `token` for NVS + the form value; `label` for the dropdown). The list
// order is the UI dropdown order. Extend here to add an action — nothing else needs to change.
struct Sb20ActionOption {
    const char* token;
    const char* label;
    Sb20ActionSpec spec;
};

inline const Sb20ActionOption* sb20ActionOptions(size_t& count) {
    static const Sb20ActionOption kOptions[] = {
        {"none", "None", {Sb20ActionKind::None, 0, 0}},
        {"shift_up", "OBC Shift Up", {Sb20ActionKind::Obc, OBC_BTN_SHIFT_UP, 0}},
        {"shift_down", "OBC Shift Down", {Sb20ActionKind::Obc, OBC_BTN_SHIFT_DOWN, 0}},
        {"erg_up", "OBC ERG Up", {Sb20ActionKind::Obc, OBC_BTN_ERG_UP, 0}},
        {"erg_down", "OBC ERG Down", {Sb20ActionKind::Obc, OBC_BTN_ERG_DOWN, 0}},
        {"lap", "OBC Lap", {Sb20ActionKind::Obc, OBC_BTN_LAP, 0}},
        {"menu", "OBC Menu", {Sb20ActionKind::Obc, OBC_BTN_MENU, 0}},
        {"pause", "OBC Pause", {Sb20ActionKind::Obc, OBC_BTN_PAUSE, 0}},
        {"bias_up", "Erg target +10W (local)", {Sb20ActionKind::ErgBias, 0, 10}},
        {"bias_down", "Erg target -10W (local)", {Sb20ActionKind::ErgBias, 0, -10}},
    };
    count = sizeof(kOptions) / sizeof(kOptions[0]);
    return kOptions;
}

// The action a token binds to (None for "none" or an unknown/legacy token — never acts on garbage).
inline Sb20ActionSpec sb20SpecForToken(const std::string& token) {
    size_t n = 0;
    const Sb20ActionOption* o = sb20ActionOptions(n);
    for (size_t i = 0; i < n; ++i)
        if (token == o[i].token) return o[i].spec;
    return {Sb20ActionKind::None, 0, 0};
}

// Index <-> token, so a compact wire form (the Bridge GATT Buttons char uses a u8 action INDEX into
// this same option order) and the SPA can agree byte-for-byte. Index 0 is always "none".
inline size_t sb20ActionCount() {
    size_t n = 0;
    sb20ActionOptions(n);
    return n;
}
inline const char* sb20TokenForIndex(size_t i) {
    size_t n = 0;
    const Sb20ActionOption* o = sb20ActionOptions(n);
    return i < n ? o[i].token : "none";
}
inline int sb20IndexForToken(const std::string& token) {
    size_t n = 0;
    const Sb20ActionOption* o = sb20ActionOptions(n);
    for (size_t i = 0; i < n; ++i)
        if (token == o[i].token) return (int)i;
    return 0;  // unknown -> "none" (index 0)
}

// The 6 physical SB20 buttons in a stable order (the map array + the UI rows share this order).
inline const ShifterButton* sb20Buttons(size_t& count) {
    static const ShifterButton kButtons[] = {
        ShifterButton::LeftUp,  ShifterButton::LeftDown,  ShifterButton::Left3,
        ShifterButton::RightUp, ShifterButton::RightDown, ShifterButton::Right3,
    };
    count = sizeof(kButtons) / sizeof(kButtons[0]);
    return kButtons;
}

inline int sb20ButtonIndex(ShifterButton b) {
    size_t n = 0;
    const ShifterButton* btns = sb20Buttons(n);
    for (size_t i = 0; i < n; ++i)
        if (btns[i] == b) return (int)i;
    return -1;
}

// The per-button binding. Serialized as "t0,t1,t2,t3,t4,t5" (one token per button, comma-joined) — safe
// inside one '|'-delimited RuntimeConfig field (',' never delimits the outer line).
struct Sb20ButtonMap {
    std::string token[6];

    // The shipped default: paddles re-broadcast Shift Up/Down, the 3rd buttons Lap (left) / Menu (right).
    static Sb20ButtonMap defaults() {
        Sb20ButtonMap m;
        m.token[0] = "shift_up";    // LEFT up
        m.token[1] = "shift_down";  // LEFT down
        m.token[2] = "lap";         // LEFT 3rd
        m.token[3] = "shift_up";    // RIGHT up
        m.token[4] = "shift_down";  // RIGHT down
        m.token[5] = "menu";        // RIGHT 3rd
        return m;
    }

    Sb20ActionSpec resolve(ShifterButton b) const {
        const int i = sb20ButtonIndex(b);
        if (i < 0) return {Sb20ActionKind::None, 0, 0};
        return sb20SpecForToken(token[i]);
    }

    // The compact wire form: each button's token as an action-option INDEX (the Bridge GATT Buttons
    // char / the SPA's BLE transport). Round-trips through fromIndices below.
    void toIndices(uint8_t idx[6]) const {
        for (int i = 0; i < 6; ++i) idx[i] = (uint8_t)sb20IndexForToken(token[i]);
    }
    static Sb20ButtonMap fromIndices(const uint8_t idx[6]) {
        Sb20ButtonMap m;  // indices are authoritative (not defaults())
        for (int i = 0; i < 6; ++i) m.token[i] = sb20TokenForIndex(idx[i]);
        return m;
    }

    std::string toString() const {
        std::string s;
        for (int i = 0; i < 6; ++i) {
            if (i) s += ',';
            s += token[i];
        }
        return s;
    }

    // Parse "t0,..,t5". Missing/empty tokens keep the default for that slot (backward-compatible with a
    // shorter/absent stored value); unknown tokens are kept verbatim and resolve to None.
    static Sb20ButtonMap fromString(const std::string& s) {
        Sb20ButtonMap m = defaults();
        size_t i = 0;
        for (int idx = 0; idx < 6; ++idx) {
            const size_t c = s.find(',', i);
            const std::string t = s.substr(i, c == std::string::npos ? std::string::npos : c - i);
            if (!t.empty()) m.token[idx] = t;
            if (c == std::string::npos) break;
            i = c + 1;
        }
        return m;
    }
};

}  // namespace sb20proxy
