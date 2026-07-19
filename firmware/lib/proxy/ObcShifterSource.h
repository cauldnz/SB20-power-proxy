#pragma once
#include <cstddef>
#include <cstdint>

#include "Obc.h"            // emitObcClick + the ButtonState codec
#include "Sb20ButtonMap.h"  // the configurable per-button action binding
#include "Shifter.h"        // ShifterDebounce (decode char 0c46be60 + edge-debounce)

namespace sb20proxy {

// Compose the SB20-shifter read path into the user-configured action: feed raw notifications from the
// SB20's vendor button characteristic 0c46be60 (see code/findings/shifter-ble-protocol.md) and, on each
// fresh debounced press, resolve the button through the configurable Sb20ButtonMap and dispatch it:
//   - an OBC action -> a momentary OBC click (the mapped id PRESSED then RELEASED) via `emitObc`
//     (the firmware wires this straight to the OBC notify char, notifyObc / notifyClients);
//   - a LOCAL erg-bias action -> `onErg(deltaW)` (the firmware nudges its own erg target);
//   - None -> nothing.
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
    // PRESSED then RELEASED — for an OBC-bound button); `onErg(deltaW)` receives a local erg nudge for an
    // erg-bias-bound button. Both are any callable; pass no-ops for unused kinds. Nothing happens for a
    // streamed repeat, a boundary/terminator frame, a short frame, or a button bound to None.
    template <typename EmitObc, typename OnErg>
    void feed(const uint8_t* data, size_t len, EmitObc&& emitObc, OnErg&& onErg) {
        const ShifterButton btn = debounce_.feed(data, len);
        if (btn == ShifterButton::None) return;  // repeat / boundary / release / short / unknown
        const Sb20ActionSpec spec = bindings_.resolve(btn);
        if (spec.kind == Sb20ActionKind::Obc && spec.obcId != 0) {
            emitObcClick(spec.obcId, emitObc);   // PRESSED then RELEASED (Obc.h)
        } else if (spec.kind == Sb20ActionKind::ErgBias && spec.ergDelta != 0) {
            onErg(spec.ergDelta);
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
