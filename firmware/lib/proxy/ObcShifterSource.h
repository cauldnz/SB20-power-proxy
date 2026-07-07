#pragma once
#include <cstddef>
#include <cstdint>

#include "Obc.h"         // OBC_MAX_MSG + the ButtonState codec
#include "ObcSb20Map.h"  // encodeSb20ButtonState (SB20 button -> OBC ids)
#include "Shifter.h"     // ShifterDebounce (decode char 0c46be60 + edge-debounce)

namespace sb20proxy {

// Compose the SB20-shifter read path into OpenBikeControl output: feed raw notifications from the SB20's
// vendor button characteristic 0c46be60 (see code/findings/shifter-ble-protocol.md) and, on each fresh
// debounced press, emit a **momentary OBC click** — the mapped OBC ids PRESSED, then RELEASED — via a
// caller-supplied sink (the firmware wires it straight to the OBC notify char, notifyObc).
//
// This is the pure, host-tested spine of the "sink SB20 buttons -> broadcast as OBC" feature shared by
// the ESP32 (NimBLE central) and nRF (Bluefruit central) seams: the radio just delivers the raw bytes
// and forwards the emitted messages; all the decode/debounce/map/encode logic lives here with golden
// vectors from the real capture. Stateless SB20 -> the debounce collapses the ~10-20x held-frame stream
// to one logical event per press (Shifter.h). Header-only; no hardware.
class ObcShifterSource {
public:
    // Feed one raw shifter notification. On a fresh press, calls `emit(bytes, len)` twice — the PRESSED
    // message then the RELEASED message (a momentary click across every OBC id the button maps to). No
    // call for a streamed repeat, a boundary/terminator frame, an unmapped button, or a short frame.
    // `Emit` is any callable `void(const uint8_t*, size_t)` (a lambda forwarding to notifyObc, or a test
    // collector). Nothing is emitted if the encode doesn't fit OBC_MAX_MSG (it always does for the SB20).
    template <typename Emit>
    void feed(const uint8_t* data, size_t len, Emit&& emit) {
        const ShifterButton btn = debounce_.feed(data, len);
        if (btn == ShifterButton::None) return;  // repeat / boundary / release / short / unknown
        uint8_t buf[OBC_MAX_MSG];
        size_t n = encodeSb20ButtonState(btn, 0x01, buf, sizeof(buf));  // all mapped ids PRESSED
        if (n > 0) emit(buf, n);
        n = encodeSb20ButtonState(btn, 0x00, buf, sizeof(buf));         // ... then RELEASED
        if (n > 0) emit(buf, n);
    }

    // Drop any in-flight press state (call on a source disconnect so a stale held-bit can't suppress the
    // first press after reconnect).
    void reset() { debounce_.reset(); }

private:
    ShifterDebounce debounce_;
};

}  // namespace sb20proxy
