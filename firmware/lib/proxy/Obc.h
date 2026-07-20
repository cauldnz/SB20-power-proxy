#pragma once
#include <cstddef>
#include <cstdint>

namespace sb20proxy {

// OpenBikeControl (OBC) — the pure, host-testable codec for re-presenting the SB20's handlebar buttons
// to any OBC-speaking trainer app. OBC is an OPEN (MIT) protocol
// (https://github.com/OpenBikeControl/openbikecontrol-protocol) with two transports that carry the
// IDENTICAL binary message format: BLE (a GATT service, for the nRF and the ESP) and mDNS/TCP-UDP (for
// the ESP's WiFi). This header is the wire codec only — pure, no Arduino/BLE — so it host-tests with
// golden vectors from the spec, exactly like Cps.h / Ftms.h. The transport seams live in src/ (NimBLE on
// the ESP, Bluefruit on the nRF, WiFiServer/ESPmDNS on the ESP). See code/findings/obc-protocol.md.

// Message-type prefix byte (first byte of every message, all transports).
inline constexpr uint8_t OBC_MSG_BUTTON_STATE  = 0x01;  // device -> app  [0x01 id state id state ...]
inline constexpr uint8_t OBC_MSG_DEVICE_STATUS = 0x02;  // device -> app  [0x02 battery connected]
inline constexpr uint8_t OBC_MSG_HAPTIC        = 0x03;  // app -> device  [0x03 pattern duration intensity]
inline constexpr uint8_t OBC_MSG_APP_INFO      = 0x04;  // app -> device

// Button state values (0x02..0xFF are analog: 0x02 = min .. 0xFF = max).
inline constexpr uint8_t OBC_STATE_RELEASED = 0x00;
inline constexpr uint8_t OBC_STATE_PRESSED  = 0x01;

// Standard button IDs (PROTOCOL.md). A device maps its physical buttons to these; a single press may
// emit SEVERAL ids at once (multi-action) so it works across apps (shifting + erg + nav).
inline constexpr uint8_t OBC_BTN_SHIFT_UP    = 0x01;  // gear shifting 0x01-0x0F
inline constexpr uint8_t OBC_BTN_SHIFT_DOWN  = 0x02;
inline constexpr uint8_t OBC_BTN_GEAR_SET    = 0x03;  // analog: direct gear
inline constexpr uint8_t OBC_BTN_NAV_UP      = 0x10;  // navigation 0x10-0x1F
inline constexpr uint8_t OBC_BTN_NAV_DOWN    = 0x11;
inline constexpr uint8_t OBC_BTN_NAV_LEFT    = 0x12;
inline constexpr uint8_t OBC_BTN_NAV_RIGHT   = 0x13;
inline constexpr uint8_t OBC_BTN_SELECT      = 0x14;
inline constexpr uint8_t OBC_BTN_BACK        = 0x15;
inline constexpr uint8_t OBC_BTN_MENU        = 0x16;
inline constexpr uint8_t OBC_BTN_STEER_LEFT  = 0x18;
inline constexpr uint8_t OBC_BTN_STEER_RIGHT = 0x19;
inline constexpr uint8_t OBC_BTN_EMOTE       = 0x20;  // social 0x20-0x2F
inline constexpr uint8_t OBC_BTN_ERG_UP      = 0x30;  // training 0x30-0x3F (increase difficulty / erg)
inline constexpr uint8_t OBC_BTN_ERG_DOWN    = 0x31;
inline constexpr uint8_t OBC_BTN_SKIP        = 0x32;
inline constexpr uint8_t OBC_BTN_PAUSE       = 0x33;
inline constexpr uint8_t OBC_BTN_RESUME      = 0x34;
inline constexpr uint8_t OBC_BTN_LAP         = 0x35;
inline constexpr uint8_t OBC_BTN_CHANGE_MODE = 0x38;
// 0x80-0x9F app-specific, 0xA0-0xFF manufacturer-specific.

// BLE transport (BLE.md): a GATT service with a notify Button-State characteristic; the notification
// value IS the binary message. Same UUIDs used on the nRF (Bluefruit) and the ESP (NimBLE).
inline constexpr const char* OBC_BLE_SERVICE_UUID = "d273f680-d548-419d-b9d1-fa0472345229";
inline constexpr const char* OBC_BLE_BUTTON_UUID  = "d273f681-d548-419d-b9d1-fa0472345229";  // Read/Notify
inline constexpr const char* OBC_BLE_HAPTIC_UUID  = "d273f682-d548-419d-b9d1-fa0472345229";  // Write
inline constexpr const char* OBC_BLE_APPINFO_UUID = "d273f683-d548-419d-b9d1-fa0472345229";  // Write

// mDNS/network transport (MDNS.md): advertise this service type; consumers connect over TCP/UDP.
inline constexpr const char* OBC_MDNS_SERVICE = "_openbikecontrol._tcp";
inline constexpr uint16_t    OBC_DEFAULT_PORT = 21587;  // matches the qz OBC producer (#4504)

// Recommended max BLE payload = 1 msg-type + 9 (id,state) pairs = 19 bytes; use 20 as a safe buffer.
inline constexpr size_t OBC_MAX_MSG = 20;

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
    const ObcAction a{id, state};
    return encodeButtonState(&a, 1, out, cap);
}

// Emit a stateless momentary click for one button id: PRESSED then RELEASED, each as its own
// Button-State message via `emit(bytes, len)`. This is the click shape every button SOURCE
// re-broadcasts (a stateless press any OBC app accepts) — SB20 shifter buttons and ANT+ Controls
// alike — so it lives here once rather than being hand-unrolled per source. id 0 = nothing to fire.
template <typename Emit>
inline void emitObcClick(uint8_t id, Emit&& emit) {
    if (id == 0) return;
    uint8_t buf[OBC_MAX_MSG];
    const uint8_t states[2] = {OBC_STATE_PRESSED, OBC_STATE_RELEASED};
    for (uint8_t state : states) {
        const size_t n = encodeButtonPress(id, state, buf, sizeof(buf));
        if (n > 0) emit(buf, n);
    }
}

// Encode a Device-Status message -> `[0x02, battery, connected]`. battery 0..100, or 0xFF if n/a.
inline size_t encodeDeviceStatus(uint8_t batteryPct, bool connected, uint8_t* out, size_t cap) {
    if (out == nullptr || cap < 3) return 0;
    out[0] = OBC_MSG_DEVICE_STATUS;
    out[1] = batteryPct;
    out[2] = connected ? 0x01 : 0x00;
    return 3;
}

}  // namespace sb20proxy
