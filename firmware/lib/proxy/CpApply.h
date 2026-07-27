#pragma once
// Applying a CpResult to the hardware, in the order the SB20 demands.
//
// WHY THIS EXISTS
// handleControlPoint() (Cps.h) is pure and well tested: given control-point bytes it returns the
// reply, any crank-length change, and whether the write was an offset-comp/zero-reset that we
// must forward to the REAL source meter. What was NOT tested is the part that actually talks to
// the radio -- the order in which the seam applies those three outcomes. That order is not a
// style choice; it is a captured protocol fact:
//
//   The SB20 drops the link (reason 531) if a control-point write goes unanswered, and the
//   Assioma's zero takes ~3.6 s. So we MUST indicate the reply first and fire the source zero
//   fire-and-forget afterwards. Reversing those two lines reintroduces a disconnect that cost a
//   whole bike session to diagnose. See findings/decisions.md and forward-plan section 10.
//
// Before this header that ordering lived only as adjacent statements inside a NimBLE callback,
// where nothing could observe it and no test could hold it. Now the sequence is data: applyCpResult
// drives a sink, and a host test asserts reply-strictly-before-zero without a radio.

#include <stddef.h>
#include <stdint.h>

#include "Cps.h"

namespace sb20proxy {

// The three things a control-point write can ask the hardware to do.
// Implemented by the BLE peripheral seam in firmware/src/ble/, and by a recording fake in tests.
class ICpSink {
 public:
    virtual ~ICpSink() = default;

    // Persist a crank length the client just set (0x04). Called only when it changed.
    virtual void setCrankLength(uint16_t halfMm) = 0;

    // Answer the client. MUST happen before any source-meter traffic -- see the header comment.
    virtual void reply(const uint8_t* data, size_t len) = 0;

    // Forward a real zero-offset to the source meter (0x0C / 0x10). Fire-and-forget: the seam
    // only flags the work for loop(), never a re-entrant central op from this callback.
    virtual void requestSourceZero() = 0;
};

// Apply a decoded control-point result in the mandated order:
//   crank length (local state)  ->  reply to the client  ->  forward the zero.
inline void applyCpResult(const CpResult& r, ICpSink& sink) {
    if (r.crankLengthChanged) sink.setCrankLength(r.crankLengthHalfMm);
    sink.reply(r.response.data(), r.response.size());
    if (r.requestSourceZero) sink.requestSourceZero();
}

}  // namespace sb20proxy
