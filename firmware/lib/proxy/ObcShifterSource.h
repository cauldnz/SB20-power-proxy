#pragma once
#include <cstddef>
#include <cstdint>

#include "Obc.h"            // emitObcClick + the ButtonState codec
#include "Sb20ButtonMap.h"  // the configurable per-button action binding
#include "Shifter.h"        // ShifterDebounce (decode char 0c46be60 + edge-debounce)

namespace sb20proxy {

// Compose the SB20-shifter read path into the user-configured action: feed raw notifications from the
// SB20's vendor button characteristic 0c46be60 (see code/findings/shifter-ble-protocol.md) and, on each
// fresh debounced press, resolve the button through the configurable Sb20ButtonMap and dispatch every
// action bound to it:
//   - the OBC actions -> ONE momentary OBC click carrying all of them (one Button-State message with
//     every bound id PRESSED, then one with every id RELEASED) via `emitObc` (the firmware wires this to
//     the OBC transports);
//   - each LOCAL erg-bias action -> `onErg(deltaW)` (the firmware nudges its own erg target);
//   - None -> nothing.
//
// Every physical press is its own press/release pair: there is no clock in here, so no cooldown can
// exist, and the debounce only folds the SB20's streamed repeats of ONE held press (PROTOCOL.md: MUST
// NOT coalesce rapid presses, for 0x30/0x31). Pinned by test_obc's rapid-press test.
//
// The pure, host-tested spine of "sink SB20 buttons -> configurable action", shared by the ESP32 (NimBLE
// central) and nRF (Bluefruit central) seams. Stateless SB20 -> the debounce collapses the ~10-20x
// held-frame stream to one event per press (Shifter.h). Header-only; no hardware.
class ObcShifterSource {
public:
    // Set the live binding (from NVS / the web UI). Defaults to Sb20ButtonMap::defaults() until set.
    void setBindings(const Sb20ButtonMap& m) { bindings_ = m; }
    const Sb20ButtonMap& bindings() const { return bindings_; }

    // Feed one raw shifter notification. `emitObc(bytes,len)` receives each OBC message (called twice —
    // PRESSED then RELEASED — when the button has any OBC action); `onErg(deltaW)` receives each local
    // erg nudge. Both are any callable; pass no-ops for unused kinds. Nothing happens for a streamed
    // repeat, a boundary/terminator frame, a short frame, or a button bound to None.
    template <typename EmitObc, typename OnErg>
    void feed(const uint8_t* data, size_t len, EmitObc&& emitObc, OnErg&& onErg) {
        const ShifterButton btn = debounce_.feed(data, len);
        if (btn == ShifterButton::None) return;  // repeat / boundary / release / short / unknown
        Sb20ActionSpec specs[kSb20MaxActionsPerButton];
        const size_t n = bindings_.resolveAll(btn, specs, kSb20MaxActionsPerButton);
        uint8_t ids[kSb20MaxActionsPerButton];
        size_t k = 0;
        for (size_t i = 0; i < n; ++i) {
            if (specs[i].kind == Sb20ActionKind::Obc && specs[i].obcId != 0) ids[k++] = specs[i].obcId;
        }
        emitObcClicks(ids, k, emitObc);  // PRESSED then RELEASED, all ids in each (Obc.h)
        for (size_t i = 0; i < n; ++i) {
            if (specs[i].kind == Sb20ActionKind::ErgBias && specs[i].ergDelta != 0) onErg(specs[i].ergDelta);
        }
    }

    // Drop any in-flight press state (call on a source disconnect so a stale held-bit can't suppress the
    // first press after reconnect).
    void reset() { debounce_.reset(); }

private:
    ShifterDebounce debounce_;
    Sb20ButtonMap bindings_ = Sb20ButtonMap::defaults();
};

}  // namespace sb20proxy
