#pragma once
#include <cstddef>
#include <cstdint>

namespace sb20proxy {

// OpenBikeControl (OBC) — the pure, host-testable codec for re-presenting the SB20's handlebar buttons
// to any OBC-speaking trainer app. OBC is an OPEN (MIT) protocol
// (https://github.com/OpenBikeControl/openbikecontrol-protocol, protocol version 1) with two transports
// that carry the IDENTICAL binary message format: BLE (a GATT service, for the nRF and the ESP) and
// mDNS/TCP (for the ESP's WiFi). This header is the wire codec only — pure, no Arduino/BLE — so it
// host-tests with golden vectors from the spec, exactly like Cps.h / Ftms.h. The transport seams live
// in src/ (NimBLE on the ESP, Bluefruit on the nRF, WiFiServer/ESPmDNS on the ESP). The app-to-device
// side (stream framing, the latest AppInfo per link) is ObcApp.h. See code/findings/obc-protocol.md.
//
// C++11-clean on purpose: the nRF build (gnu++11) includes this header through ObcShifterSource.h.

// Message-type prefix byte (first byte of every message, all transports).
inline constexpr uint8_t OBC_MSG_BUTTON_STATE  = 0x01;  // device -> app  [0x01 id state id state ...]
inline constexpr uint8_t OBC_MSG_DEVICE_STATUS = 0x02;  // device -> app  [0x02 battery connected]
inline constexpr uint8_t OBC_MSG_HAPTIC        = 0x03;  // app -> device  [0x03 pattern duration intensity]
inline constexpr uint8_t OBC_MSG_APP_INFO      = 0x04;  // app -> device  [0x04 ver len id.. len ver.. n ids..]

// Button state values (0x02..0xFF are analog: 0x02 = min .. 0xFF = max).
inline constexpr uint8_t OBC_STATE_RELEASED = 0x00;
inline constexpr uint8_t OBC_STATE_PRESSED  = 0x01;

// DeviceStatus battery byte for a device that is not battery-powered (MDNS.md: 0xFF = not applicable).
inline constexpr uint8_t OBC_BATTERY_NA = 0xFF;

// Standard button IDs (PROTOCOL.md, "Standard Button IDs"). A device maps its physical buttons to these;
// a single press may emit SEVERAL ids at once (multi-action) so it works across apps (shifting + erg +
// nav). Every id the spec defines is listed; obcButtonCatalog() below is the token/label table.
// Gear shifting 0x01-0x0F
inline constexpr uint8_t OBC_BTN_SHIFT_UP      = 0x01;
inline constexpr uint8_t OBC_BTN_SHIFT_DOWN    = 0x02;
inline constexpr uint8_t OBC_BTN_GEAR_SET      = 0x03;  // analog: flat gear index + 1
inline constexpr uint8_t OBC_BTN_CHAINRING_SET = 0x04;  // analog: front chainring + 1
inline constexpr uint8_t OBC_BTN_CASSETTE_SET  = 0x05;  // analog: rear cassette + 1
// Navigation 0x10-0x1F
inline constexpr uint8_t OBC_BTN_NAV_UP        = 0x10;
inline constexpr uint8_t OBC_BTN_NAV_DOWN      = 0x11;
inline constexpr uint8_t OBC_BTN_NAV_LEFT      = 0x12;
inline constexpr uint8_t OBC_BTN_NAV_RIGHT     = 0x13;
inline constexpr uint8_t OBC_BTN_SELECT        = 0x14;
inline constexpr uint8_t OBC_BTN_BACK          = 0x15;
inline constexpr uint8_t OBC_BTN_MENU          = 0x16;
inline constexpr uint8_t OBC_BTN_HOME          = 0x17;
inline constexpr uint8_t OBC_BTN_STEER_LEFT    = 0x18;
inline constexpr uint8_t OBC_BTN_STEER_RIGHT   = 0x19;
inline constexpr uint8_t OBC_BTN_BRAKE         = 0x1A;  // analog: 0x01 = full, 0x02.. = percent + 1
// Social 0x20-0x2F
inline constexpr uint8_t OBC_BTN_EMOTE         = 0x20;  // analog: 0x01 = cycle, 0x02.. app-specific
inline constexpr uint8_t OBC_BTN_PUSH_TO_TALK  = 0x21;
inline constexpr uint8_t OBC_BTN_SCREENSHOT    = 0x24;
// Training controls 0x30-0x3F
inline constexpr uint8_t OBC_BTN_ERG_UP        = 0x30;  // Increase Difficulty (erg power up)
inline constexpr uint8_t OBC_BTN_ERG_DOWN      = 0x31;  // Decrease Difficulty (erg power down)
inline constexpr uint8_t OBC_BTN_SKIP          = 0x32;  // Skip Interval
inline constexpr uint8_t OBC_BTN_PAUSE         = 0x33;
inline constexpr uint8_t OBC_BTN_RESUME        = 0x34;
inline constexpr uint8_t OBC_BTN_LAP           = 0x35;
inline constexpr uint8_t OBC_BTN_PREV_INTERVAL = 0x36;
inline constexpr uint8_t OBC_BTN_U_TURN        = 0x37;
inline constexpr uint8_t OBC_BTN_CHANGE_MODE   = 0x38;
inline constexpr uint8_t OBC_BTN_TAKE_BREAK    = 0x39;
inline constexpr uint8_t OBC_BTN_JOIN_RIDER    = 0x3A;
inline constexpr uint8_t OBC_BTN_CHANGE_ROUTE  = 0x3B;
inline constexpr uint8_t OBC_BTN_CRUISE        = 0x3C;  // analog: 0x01 = toggle, 0x0A.. = value*5 W
// View controls 0x40-0x4F (the spec defines 0x40 and 0x44-0x46 only; 0x41-0x43 are unassigned)
inline constexpr uint8_t OBC_BTN_CAMERA_VIEW   = 0x40;  // analog: 0x01 = cycle, 0x02.. app-specific
inline constexpr uint8_t OBC_BTN_HUD_TOGGLE    = 0x44;
inline constexpr uint8_t OBC_BTN_MAP_TOGGLE    = 0x45;
inline constexpr uint8_t OBC_BTN_SPECTATE      = 0x46;
// Power-ups 0x50-0x5F
inline constexpr uint8_t OBC_BTN_POWERUP_1     = 0x50;
inline constexpr uint8_t OBC_BTN_POWERUP_2     = 0x51;
inline constexpr uint8_t OBC_BTN_POWERUP_3     = 0x52;
// 0x60-0x7F reserved, 0x80-0x9F app-specific, 0xA0-0xFF manufacturer-specific.

// One row of the spec's button table. `token` is the stable lowercase name the configurable button map
// stores (Sb20ButtonMap.h); `clickable` says whether a momentary press (PRESSED = 0x01, then RELEASED)
// means something: false for the three gear-selection ids, where the spec makes 0x01 a no-op.
struct ObcButtonInfo {
    uint8_t id;
    const char* token;
    const char* label;
    bool clickable;
};

// Every standard id PROTOCOL.md defines, in id order. Pure data; host-tested for completeness.
inline const ObcButtonInfo* obcButtonCatalog(size_t& count) {
    static const ObcButtonInfo kCatalog[] = {
        {OBC_BTN_SHIFT_UP, "shift_up", "Shift Up", true},
        {OBC_BTN_SHIFT_DOWN, "shift_down", "Shift Down", true},
        {OBC_BTN_GEAR_SET, "gear_set", "Gear Set", false},
        {OBC_BTN_CHAINRING_SET, "chainring_set", "Chainring Set", false},
        {OBC_BTN_CASSETTE_SET, "cassette_set", "Cassette Set", false},
        {OBC_BTN_NAV_UP, "nav_up", "Up", true},
        {OBC_BTN_NAV_DOWN, "nav_down", "Down", true},
        {OBC_BTN_NAV_LEFT, "nav_left", "Left", true},
        {OBC_BTN_NAV_RIGHT, "nav_right", "Right", true},
        {OBC_BTN_SELECT, "select", "Select/Confirm", true},
        {OBC_BTN_BACK, "back", "Back/Cancel", true},
        {OBC_BTN_MENU, "menu", "Menu", true},
        {OBC_BTN_HOME, "home", "Home", true},
        {OBC_BTN_STEER_LEFT, "steer_left", "Steer Left", true},
        {OBC_BTN_STEER_RIGHT, "steer_right", "Steer Right", true},
        {OBC_BTN_BRAKE, "brake", "Brake", true},
        {OBC_BTN_EMOTE, "emote", "Emote", true},
        {OBC_BTN_PUSH_TO_TALK, "push_to_talk", "Push to Talk", true},
        {OBC_BTN_SCREENSHOT, "screenshot", "Screenshot", true},
        {OBC_BTN_ERG_UP, "erg_up", "Increase Difficulty", true},
        {OBC_BTN_ERG_DOWN, "erg_down", "Decrease Difficulty", true},
        {OBC_BTN_SKIP, "skip_interval", "Skip Interval", true},
        {OBC_BTN_PAUSE, "pause", "Pause", true},
        {OBC_BTN_RESUME, "resume", "Resume", true},
        {OBC_BTN_LAP, "lap", "Lap", true},
        {OBC_BTN_PREV_INTERVAL, "prev_interval", "Previous Interval", true},
        {OBC_BTN_U_TURN, "u_turn", "U-Turn", true},
        {OBC_BTN_CHANGE_MODE, "change_mode", "Change Mode", true},
        {OBC_BTN_TAKE_BREAK, "take_break", "Take a break", true},
        {OBC_BTN_JOIN_RIDER, "join_rider", "Join another rider", true},
        {OBC_BTN_CHANGE_ROUTE, "change_route", "Change route", true},
        {OBC_BTN_CRUISE, "cruise_control", "Cruise Control", true},
        {OBC_BTN_CAMERA_VIEW, "camera_view", "Camera View", true},
        {OBC_BTN_HUD_TOGGLE, "hud_toggle", "HUD Toggle", true},
        {OBC_BTN_MAP_TOGGLE, "map_toggle", "Map Toggle", true},
        {OBC_BTN_SPECTATE, "spectate", "Spectate rider", true},
        {OBC_BTN_POWERUP_1, "powerup_1", "Power-up 1", true},
        {OBC_BTN_POWERUP_2, "powerup_2", "Power-up 2", true},
        {OBC_BTN_POWERUP_3, "powerup_3", "Power-up 3", true},
    };
    count = sizeof(kCatalog) / sizeof(kCatalog[0]);
    return kCatalog;
}

// The catalog row for a standard id, or nullptr (reserved / app- or manufacturer-specific).
inline const ObcButtonInfo* obcButtonInfo(uint8_t id) {
    size_t n = 0;
    const ObcButtonInfo* c = obcButtonCatalog(n);
    for (size_t i = 0; i < n; ++i)
        if (c[i].id == id) return &c[i];
    return nullptr;
}

// BLE transport (BLE.md): a GATT service with a notify Button-State characteristic; the notification
// value IS the binary message. Same UUIDs used on the nRF (Bluefruit) and the ESP (NimBLE).
inline constexpr const char* OBC_BLE_SERVICE_UUID = "d273f680-d548-419d-b9d1-fa0472345229";
inline constexpr const char* OBC_BLE_BUTTON_UUID  = "d273f681-d548-419d-b9d1-fa0472345229";  // Read/Notify
inline constexpr const char* OBC_BLE_HAPTIC_UUID  = "d273f682-d548-419d-b9d1-fa0472345229";  // Write/WWR
inline constexpr const char* OBC_BLE_APPINFO_UUID = "d273f683-d548-419d-b9d1-fa0472345229";  // Write/WWR

// mDNS/network transport (MDNS.md): advertise this service type; consumers connect over TCP. (MDNS.md
// defines TCP only — there is no UDP transport in protocol version 1.)
inline constexpr const char* OBC_MDNS_SERVICE = "_openbikecontrol._tcp";
inline constexpr uint16_t    OBC_DEFAULT_PORT = 21587;  // matches the qz OBC producer (#4504)
inline constexpr const char* OBC_PROTOCOL_VERSION = "1";  // the TXT record's version=

// Recommended max BLE payload = 1 msg-type + 9 (id,state) pairs = 19 bytes; use 20 as a safe buffer.
inline constexpr size_t OBC_MAX_MSG = 20;
inline constexpr size_t OBC_MAX_ACTIONS = (OBC_MAX_MSG - 1) / 2;  // 9 (id,state) pairs per message

// One (button-id, state) action within a Button-State message.
struct ObcAction {
    uint8_t id;
    uint8_t state;
};

// Encode a Button-State message into `out`: `[0x01, id0, state0, id1, state1, ...]`. Returns the number
// of bytes written, or 0 if the buffer is too small / no actions. `cap` must be >= 1 + 2*n.
inline size_t encodeButtonState(const ObcAction* actions, size_t n, uint8_t* out, size_t cap) {
    if (n == 0 || actions == nullptr) return 0;
    const size_t need = 1 + 2 * n;
    if (out == nullptr || cap < need) return 0;
    out[0] = OBC_MSG_BUTTON_STATE;
    for (size_t i = 0; i < n; ++i) {
        out[1 + 2 * i] = actions[i].id;
        out[2 + 2 * i] = actions[i].state;
    }
    return need;
}

// Convenience: encode a single button action -> `[0x01, id, state]`.
inline size_t encodeButtonPress(uint8_t id, uint8_t state, uint8_t* out, size_t cap) {
    const ObcAction a = {id, state};
    return encodeButtonState(&a, 1, out, cap);
}

// Emit one momentary click for a SET of button ids (PROTOCOL.md "Multiple Actions Per Button"): ONE
// Button-State message with every id PRESSED, then ONE with every id RELEASED, each via
// `emit(bytes, len)`. Every call is its own press/release transition — nothing here ever merges two
// presses or adds a cooldown (the spec's MUST for 0x30/0x31). Zero ids are skipped; at most
// OBC_MAX_ACTIONS ids fit one message, extras are dropped rather than split across messages.
template <typename Emit>
inline void emitObcClicks(const uint8_t* ids, size_t n, Emit&& emit) {
    if (ids == nullptr) return;
    ObcAction acts[OBC_MAX_ACTIONS];
    size_t k = 0;
    for (size_t i = 0; i < n && k < OBC_MAX_ACTIONS; ++i) {
        if (ids[i] == 0) continue;
        acts[k].id = ids[i];
        acts[k].state = OBC_STATE_PRESSED;
        ++k;
    }
    if (k == 0) return;
    uint8_t buf[OBC_MAX_MSG];
    size_t len = encodeButtonState(acts, k, buf, sizeof(buf));
    if (len > 0) emit(buf, len);
    for (size_t i = 0; i < k; ++i) acts[i].state = OBC_STATE_RELEASED;
    len = encodeButtonState(acts, k, buf, sizeof(buf));
    if (len > 0) emit(buf, len);
}

// Emit a stateless momentary click for one button id: PRESSED then RELEASED, each as its own
// Button-State message via `emit(bytes, len)`. This is the click shape every button SOURCE
// re-broadcasts (a stateless press any OBC app accepts) — SB20 shifter buttons and ANT+ Controls
// alike — so it lives here once rather than being hand-unrolled per source. id 0 = nothing to fire.
template <typename Emit>
inline void emitObcClick(uint8_t id, Emit&& emit) {
    emitObcClicks(&id, 1, emit);
}

// Encode a Device-Status message -> `[0x02, battery, connected]`. battery 0..100, or 0xFF if n/a.
inline size_t encodeDeviceStatus(uint8_t batteryPct, bool connected, uint8_t* out, size_t cap) {
    if (out == nullptr || cap < 3) return 0;
    out[0] = OBC_MSG_DEVICE_STATUS;
    out[1] = batteryPct;
    out[2] = connected ? 0x01 : 0x00;
    return 3;
}

// ---- App -> device messages ---------------------------------------------------------------------

// Haptic Feedback `[0x03, pattern, duration, intensity]` (BLE.md / MDNS.md). We have no motor: the
// spec says such a device accepts the write and does nothing, so this exists to validate + log.
inline constexpr size_t OBC_HAPTIC_LEN = 4;
struct ObcHaptic {
    uint8_t pattern;       // 0x00 stop, 0x01 short, 0x02 double, ... 0x07 error, 0x08.. reserved
    uint8_t duration10ms;  // 0 = the pattern's default, else units of 10 ms
    uint8_t intensity;     // 0 = default, 0x01..0xFF low..max
};

// Decode a Haptic message. False unless it starts with 0x03 and carries all four bytes (trailing bytes
// are ignored, so a longer future revision still decodes).
inline bool decodeHaptic(const uint8_t* d, size_t n, ObcHaptic& out) {
    if (d == nullptr || n < OBC_HAPTIC_LEN || d[0] != OBC_MSG_HAPTIC) return false;
    out.pattern = d[1];
    out.duration10ms = d[2];
    out.intensity = d[3];
    return true;
}

// App Information `[0x04, version, idLen, id..., verLen, ver..., count, ids...]` (BLE.md / MDNS.md).
// Strings are at most 32 bytes by spec; a longer one is truncated on store (the bytes are still
// consumed, so framing holds). count 0 = "the app supports every button".
inline constexpr size_t OBC_APP_STR_MAX = 32;
struct ObcAppInfo {
    uint8_t version;
    char appId[OBC_APP_STR_MAX + 1];  // NUL-terminated, printable-sanitised (see below)
    char appVersion[OBC_APP_STR_MAX + 1];
    uint8_t buttonCount;              // as declared (0 = all)
    uint8_t supportedBits[32];        // 256-bit set of the declared ids

    void clear() {
        version = 0;
        appId[0] = '\0';
        appVersion[0] = '\0';
        buttonCount = 0;
        for (size_t i = 0; i < sizeof(supportedBits); ++i) supportedBits[i] = 0;
    }
    bool allButtons() const { return buttonCount == 0; }
    // Does the app say it handles this id? (true for every id when it declared none — the spec's
    // "0 = all", and the default a device must assume when no AppInfo arrived.)
    bool supports(uint8_t id) const {
        return allButtons() || (supportedBits[id >> 3] & (uint8_t)(1u << (id & 7))) != 0;
    }
};

// The total byte length of the App Information message at the head of `d`, or 0 when `n` bytes are
// not yet enough to tell (TCP framing: the stream parser asks this as bytes arrive).
inline size_t obcAppInfoLength(const uint8_t* d, size_t n) {
    if (d == nullptr || n < 3) return 0;
    size_t pos = 3 + (size_t)d[2];  // type, version, idLen, id...
    if (n < pos + 1) return 0;
    pos += 1 + (size_t)d[pos];      // verLen, ver...
    if (n < pos + 1) return 0;
    return pos + 1 + (size_t)d[pos];  // count, ids...
}

namespace detail {
// Copy at most OBC_APP_STR_MAX bytes and keep the result safe to log or drop into JSON: control
// characters, '"' and '\\' become '?'. UTF-8 multibyte sequences (>= 0x80) pass through.
inline void obcCopyAppString(const uint8_t* src, size_t len, char* dst) {
    const size_t k = len < OBC_APP_STR_MAX ? len : OBC_APP_STR_MAX;
    for (size_t i = 0; i < k; ++i) {
        const uint8_t c = src[i];
        dst[i] = (c < 0x20 || c == 0x7F || c == '"' || c == '\\') ? '?' : (char)c;
    }
    dst[k] = '\0';
}
}  // namespace detail

// Decode an App Information message. False (and `out` untouched) unless it starts with 0x04, has a
// non-zero version and every declared length fits inside `n`. Trailing bytes are ignored.
inline bool decodeAppInfo(const uint8_t* d, size_t n, ObcAppInfo& out) {
    if (d == nullptr || n < 1 || d[0] != OBC_MSG_APP_INFO) return false;
    const size_t total = obcAppInfoLength(d, n);
    if (total == 0 || total > n || d[1] == 0) return false;
    ObcAppInfo a;
    a.clear();
    a.version = d[1];
    const size_t idLen = d[2];
    detail::obcCopyAppString(d + 3, idLen, a.appId);
    const size_t verAt = 3 + idLen;
    const size_t verLen = d[verAt];
    detail::obcCopyAppString(d + verAt + 1, verLen, a.appVersion);
    const size_t cntAt = verAt + 1 + verLen;
    a.buttonCount = d[cntAt];
    for (size_t i = 0; i < a.buttonCount; ++i) {
        const uint8_t id = d[cntAt + 1 + i];
        a.supportedBits[id >> 3] = (uint8_t)(a.supportedBits[id >> 3] | (1u << (id & 7)));
    }
    out = a;
    return true;
}

}  // namespace sb20proxy
