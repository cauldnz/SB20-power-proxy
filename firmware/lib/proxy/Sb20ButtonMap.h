#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "Obc.h"      // OBC button ids + OBC_STATE_* + the spec's button catalog (re-broadcast targets)
#include "Shifter.h"  // ShifterButton (the 6 physical SB20 buttons)

namespace sb20proxy {

// The user-configurable "what does each SB20 button do" map (edited in the ESP web UI, persisted to NVS,
// shared by the nRF via lib_extra_dirs). Each of the 6 physical buttons binds to one OR MORE action
// tokens (PROTOCOL.md "Multiple Actions Per Button"); a press resolves to OBC re-broadcasts (a training
// app consumes them), LOCAL device actions (a nudge to our own erg target), or nothing. Pure +
// header-only -> host-tested; the firmware seams just route the resolved actions.

enum class Sb20ActionKind : uint8_t { None, Obc, ErgBias };

// A plain aggregate (no default member initializers) so brace-init works on the nRF's older C++
// toolchain too — every use below constructs it explicitly.
struct Sb20ActionSpec {
    Sb20ActionKind kind;
    uint8_t obcId;    // kind==Obc: the OBC button id to emit (a momentary press+release)
    int8_t ergDelta;  // kind==ErgBias: the local erg-target nudge in W (+/-)
};

// The INDEXED actions: the stable `token` for NVS + the form value, `label` for the dropdown. This list
// is a wire format: the Bridge GATT Buttons char (nRF) and /obc/buttons.json "actions" carry a u8 INDEX
// into it, and web/index.html OBC_ACTIONS mirrors it label for label (code/tests/
// test_wire_format_parity.py fails on drift). So it is append-only, and appending needs the SPA in the
// same change. Every other standard OBC id is bindable by TOKEN instead (obcButtonCatalog in Obc.h,
// via the string form and /obc/buttons.json "tokens"), without touching this list.
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

// The action ONE token binds to: an indexed option first, then any clickable id from the spec's
// catalog (so every training control 0x30-0x3C, navigation, view, social and power-up id is
// bindable). None for "none" or an unknown/legacy token — never acts on garbage. The gear-selection
// ids (0x03-0x05) are not bindable: a momentary 0x01 is a no-op for them by spec.
inline Sb20ActionSpec sb20SpecForToken(const std::string& token) {
    size_t n = 0;
    const Sb20ActionOption* o = sb20ActionOptions(n);
    for (size_t i = 0; i < n; ++i)
        if (token == o[i].token) return o[i].spec;
    const ObcButtonInfo* c = obcButtonCatalog(n);
    for (size_t i = 0; i < n; ++i)
        if (c[i].clickable && token == c[i].token) {
            const Sb20ActionSpec s = {Sb20ActionKind::Obc, c[i].id, 0};
            return s;
        }
    const Sb20ActionSpec none = {Sb20ActionKind::None, 0, 0};
    return none;
}

// Multiple actions per button: one slot is "token[+token...]" ('+' never appears in a token, nor
// delimits anything else in the stored line). At most OBC_MAX_ACTIONS per button — the most (id,state)
// pairs one Button-State message carries.
inline constexpr char kSb20ActionJoin = '+';
inline constexpr size_t kSb20MaxActionsPerButton = OBC_MAX_ACTIONS;

// Resolve every action in a slot string into `out` (up to `cap`); returns the count. Unknown tokens
// resolve to None and are kept in place, so the count is the number of tokens, not of live actions.
inline size_t sb20SpecsForSlot(const std::string& slot, Sb20ActionSpec* out, size_t cap) {
    size_t n = 0, i = 0;
    while (out != nullptr && n < cap && i <= slot.size()) {
        const size_t p = slot.find(kSb20ActionJoin, i);
        const std::string t = slot.substr(i, p == std::string::npos ? std::string::npos : p - i);
        if (!t.empty()) out[n++] = sb20SpecForToken(t);
        if (p == std::string::npos) break;
        i = p + 1;
    }
    return n;
}

// The first token of a slot ("shift_up+erg_up" -> "shift_up").
inline std::string sb20FirstToken(const std::string& slot) {
    const size_t p = slot.find(kSb20ActionJoin);
    return p == std::string::npos ? slot : slot.substr(0, p);
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

// Keep only the characters a slot can legitimately hold ([a-z0-9_+]) — the JSON "tokens" input is
// user-supplied and lands in the '|'-delimited NVS line.
inline std::string sb20CleanSlot(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == kSb20ActionJoin) o += c;
    }
    return o;
}

// The per-button binding. Serialized as "s0,s1,s2,s3,s4,s5" (one slot per button, comma-joined; a
// slot is "token[+token...]") — safe inside one '|'-delimited RuntimeConfig field (',' and '+' never
// delimit the outer line). A single-token slot is exactly the pre-multi-action format, so every
// binding saved before multi-action existed loads, resolves and re-serialises unchanged.
struct Sb20ButtonMap {
    std::string token[6];  // per-button slot string ("shift_up", or "shift_up+erg_up")

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

    // Every action bound to `b` (up to `cap`, at most kSb20MaxActionsPerButton); returns the count.
    size_t resolveAll(ShifterButton b, Sb20ActionSpec* out, size_t cap) const {
        const int i = sb20ButtonIndex(b);
        if (i < 0) return 0;
        return sb20SpecsForSlot(token[i], out, cap < kSb20MaxActionsPerButton ? cap : kSb20MaxActionsPerButton);
    }

    // The FIRST action bound to `b` (None when unbound) — the single-action view older callers use.
    Sb20ActionSpec resolve(ShifterButton b) const {
        Sb20ActionSpec s[1];
        if (resolveAll(b, s, 1) == 1) return s[0];
        const Sb20ActionSpec none = {Sb20ActionKind::None, 0, 0};
        return none;
    }

    // The compact wire form: each button's FIRST token as an action-option INDEX (the Bridge GATT
    // Buttons char / the SPA's dropdowns). Lossy for a multi-action slot or a catalog-only token (the
    // index can say one indexed action); /obc/buttons.json "tokens" carries the full binding.
    void toIndices(uint8_t idx[6]) const {
        for (int i = 0; i < 6; ++i) idx[i] = (uint8_t)sb20IndexForToken(sb20FirstToken(token[i]));
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

    // Parse "s0,..,s5". Missing/empty slots keep the default for that slot (backward-compatible with a
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

// JSON for the shared web SPA's HttpTransport (the ESP32 side):
//   {"enabled":<bool>,"actions":[i0..i5],"tokens":["s0",..,"s5"]}
// "actions" is the SAME action-option index as the BLE Buttons char (what today's SPA reads and
// writes); "tokens" is the full per-button slot (multi-action, any catalog id). The parse handles ONLY
// this fixed shape (our own trusted SPA; there is no general JSON parser on-device — mirrors how the
// curve uses a compact string). When a body carries "tokens" it wins; an "actions"-only body (today's
// SPA) still works exactly as before. Host-tested.
inline std::string buttonsToJson(bool enabled, const Sb20ButtonMap& m) {
    uint8_t idx[6];
    m.toIndices(idx);
    std::string s = "{\"enabled\":";
    s += enabled ? "true" : "false";
    s += ",\"actions\":[";
    for (int i = 0; i < 6; ++i) {
        if (i) s += ',';
        char num[8];
        std::snprintf(num, sizeof(num), "%d", (int)idx[i]);  // not std::to_string (newlib-nano lacks it)
        s += num;
    }
    s += "],\"tokens\":[";
    for (int i = 0; i < 6; ++i) {
        if (i) s += ',';
        s += '"';
        s += sb20CleanSlot(m.token[i]);
        s += '"';
    }
    s += "]}";
    return s;
}

// Read the "tokens":[...] string array (up to 6) into `m`; false when the key is absent or malformed.
inline bool buttonsTokensFromJson(const std::string& body, Sb20ButtonMap& m) {
    const size_t k = body.find("\"tokens\"");
    if (k == std::string::npos) return false;
    const size_t lb = body.find('[', k);
    if (lb == std::string::npos) return false;
    const size_t rb = body.find(']', lb);
    if (rb == std::string::npos) return false;
    Sb20ButtonMap out;  // tokens are authoritative: a slot the array omits is "none"
    for (int i = 0; i < 6; ++i) out.token[i] = "none";
    size_t p = lb + 1;
    int n = 0;
    while (n < 6) {
        const size_t q0 = body.find('"', p);
        if (q0 == std::string::npos || q0 > rb) break;
        const size_t q1 = body.find('"', q0 + 1);
        if (q1 == std::string::npos || q1 > rb) return false;
        const std::string slot = sb20CleanSlot(body.substr(q0 + 1, q1 - q0 - 1));
        out.token[n++] = slot.empty() ? std::string("none") : slot;
        p = q1 + 1;
    }
    m = out;
    return true;
}

// Parse {"enabled":...,"actions":[i0,..]} (+ optional "tokens") -> enabled + map. Returns false
// (leaving outputs untouched) unless "enabled" and one of "actions"/"tokens" are present.
// Whitespace-tolerant; reads up to 6 ints in actions[].
inline bool buttonsFromJson(const std::string& body, bool& enabled, Sb20ButtonMap& m) {
    const size_t e = body.find("\"enabled\"");
    if (e == std::string::npos) return false;
    size_t ev = e + 9;  // past "enabled"
    while (ev < body.size() && body[ev] != 't' && body[ev] != 'f') ++ev;
    const bool en = (ev < body.size() && body[ev] == 't');
    Sb20ButtonMap fromTokens;
    if (buttonsTokensFromJson(body, fromTokens)) {
        enabled = en;
        m = fromTokens;
        return true;
    }
    const size_t a = body.find("\"actions\"");
    if (a == std::string::npos) return false;
    const size_t lb = body.find('[', a);
    if (lb == std::string::npos) return false;
    uint8_t idx[6] = {0, 0, 0, 0, 0, 0};
    size_t p = lb + 1;
    int n = 0;
    while (p < body.size() && body[p] != ']' && n < 6) {
        while (p < body.size() && (body[p] < '0' || body[p] > '9') && body[p] != ']') ++p;
        if (p >= body.size() || body[p] == ']') break;
        int v = 0;
        while (p < body.size() && body[p] >= '0' && body[p] <= '9') { v = v * 10 + (body[p] - '0'); ++p; }
        idx[n++] = (uint8_t)(v > 255 ? 0 : v);
    }
    enabled = en;
    m = Sb20ButtonMap::fromIndices(idx);
    return true;
}

}  // namespace sb20proxy
