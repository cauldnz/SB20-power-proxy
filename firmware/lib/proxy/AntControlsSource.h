#pragma once
// AntControlsSource — pure decoder for the ANT+ "Controls" (Generic Control) device profile
// (device type 16), mapping its Generic Command page (0x49) to OpenBikeControl button events.
//
// This is the source side for electronic-shifter spare buttons that broadcast as ANT+ controllers —
// SRAM AXS Bonus buttons / Wireless Blips / MultiClics (see code/findings/obc-shifter-sources.md).
//
// GROUNDING + INVARIANT: the page layout + command set here are the PUBLIC ANT+ Controls profile
// (matched to the openant reference `controls_device.py` — read to understand, not copied). The
// command->OBC mapping is the natural standard mapping (MenuUp->NavUp, Lap->Lap, …). What is NOT yet
// grounded, per the project's capture-before-code invariant, is the AXS-SPECIFIC binding of a
// *physical* button to a command number (and whether a controller must be bonded first) — that waits
// on a real on-air capture from a 2024+ AXS controller (run-sheet R4). So: the standard decode is
// solid + host-tested; treat the physical-button semantics as TODO-on-capture.
//
// Pure: no ANT radio, no Arduino. The on-device ANT *slave* channel that receives these pages is the
// hardware seam (firmware-nrf, S340-gated) — this header is its host-tested brain, like AntBikePower.h.
#include <cstddef>
#include <cstdint>

#include "Obc.h"  // OBC action ids + encodeButtonState (the re-broadcast target)

namespace sb20proxy {

// ANT+ Controls "Generic Command" numbers (profile §Generic Command Page). Values are the standard.
enum class AntControlCmd : uint16_t {
    MenuUp = 0,
    MenuDown = 1,
    MenuSelect = 2,
    MenuBack = 3,
    Home = 4,
    Start = 32,
    Stop = 33,
    Reset = 34,
    Length = 35,
    Lap = 36,
    NoCommand = 0xFFFF,
};

inline constexpr uint8_t ANT_CTRL_PAGE_GENERIC_CMD = 0x49;  // the Generic Command Page number
inline constexpr uint8_t ANT_CTRL_DEVICE_TYPE = 16;         // ANT+ Controls device type

// One decoded Generic Command page. `valid` is false when the payload isn't a 0x49 page.
struct AntControlEvent {
    bool valid = false;
    AntControlCmd cmd = AntControlCmd::NoCommand;
    uint16_t raw = 0xFFFF;   // the raw 16-bit command number (Custom/Reserved keep their raw value)
    uint8_t seq = 0;         // command sequence — increments per new press (dedup key)
    uint16_t serial = 0;     // slave serial number (which controller)
    uint16_t mfg = 0;        // slave manufacturer id
};

// Decode an 8-byte ANT data payload as a Generic Command page (0x49):
//   [0x49, serialLo, serialHi, mfgLo, mfgHi, seq, cmdLo, cmdHi]
inline AntControlEvent decodeAntControlPage(const uint8_t* d, size_t n) {
    AntControlEvent e;
    if (d == nullptr || n < 8 || d[0] != ANT_CTRL_PAGE_GENERIC_CMD) return e;
    e.valid = true;
    e.serial = (uint16_t)(d[1] | (d[2] << 8));
    e.mfg = (uint16_t)(d[3] | (d[4] << 8));
    e.seq = d[5];
    e.raw = (uint16_t)(d[6] | (d[7] << 8));
    e.cmd = static_cast<AntControlCmd>(e.raw);
    return e;
}

// Map a standard control command to OBC action id(s). Returns the number written (0 = unmapped, e.g.
// NoCommand). These are the natural standard mappings; per-controller multi-action bindings (the
// obc-shifter-sources.md table) are a future config once real AXS button->command pairs are captured.
inline size_t antControlToObc(AntControlCmd cmd, uint8_t* ids, size_t cap) {
    uint8_t id = 0;
    switch (cmd) {
        case AntControlCmd::MenuUp:     id = OBC_BTN_NAV_UP; break;
        case AntControlCmd::MenuDown:   id = OBC_BTN_NAV_DOWN; break;
        case AntControlCmd::MenuSelect: id = OBC_BTN_SELECT; break;
        case AntControlCmd::MenuBack:   id = OBC_BTN_BACK; break;
        case AntControlCmd::Home:       id = OBC_BTN_MENU; break;
        case AntControlCmd::Lap:        id = OBC_BTN_LAP; break;
        case AntControlCmd::Start:      id = OBC_BTN_RESUME; break;
        case AntControlCmd::Stop:       id = OBC_BTN_PAUSE; break;
        default: return 0;  // NoCommand / Reset / Length / Reserved / Custom -> unmapped for now
    }
    if (cap < 1) return 0;
    ids[0] = id;
    return 1;
}

// Stateful source: feed ANT Controls pages; it dedups re-transmits (same seq) and, for each NEW
// mapped command, emits a stateless OBC click (PRESSED then RELEASED) via `emit(msg, len)` —
// mirroring ObcShifterSource for the SB20's own buttons.
class AntControlsSource {
public:
    void reset() { have_ = false; lastSeq_ = 0; }

    // Emit signature: void emit(const uint8_t* msg, size_t len)
    template <class Emit>
    void feed(const uint8_t* page, size_t n, Emit emit) {
        const AntControlEvent e = decodeAntControlPage(page, n);
        if (!e.valid) return;
        if (have_ && e.seq == lastSeq_) return;   // same press re-broadcast -> ignore
        have_ = true;
        lastSeq_ = e.seq;
        uint8_t ids[4];
        const size_t nIds = antControlToObc(e.cmd, ids, sizeof(ids));
        if (nIds == 0) return;                     // NoCommand / unmapped -> nothing to fire
        ObcAction acts[4];
        uint8_t buf[OBC_MAX_MSG];
        // PRESSED for all mapped ids, then RELEASED — a stateless click any OBC app accepts.
        const uint8_t states[2] = {OBC_STATE_PRESSED, OBC_STATE_RELEASED};
        for (int s = 0; s < 2; ++s) {
            for (size_t i = 0; i < nIds; ++i) acts[i] = ObcAction{ids[i], states[s]};
            const size_t len = encodeButtonState(acts, nIds, buf, sizeof(buf));
            if (len) emit(buf, len);
        }
    }

    bool have() const { return have_; }
    uint8_t lastSeq() const { return lastSeq_; }

private:
    bool have_ = false;
    uint8_t lastSeq_ = 0;
};

}  // namespace sb20proxy
