#pragma once
// Pending-slot handoff from a radio callback context to the main loop.
//
// WHY THIS EXISTS
// Readings and control-point events arrive on the BLE host task. The objects they feed
// (CalibrationSession's pairs vector, the central's GATT ops) must only ever be touched from
// loop(). So the callback *publishes* the latest value into a single-writer/single-reader slot
// and loop() *takes* it. firmware/src/main.cpp hand-rolled this protocol three times over bare
// `volatile` globals (g_pendZeroReset, g_pendDut{,P,T}, g_pendRef{,P,T}); this header is that
// protocol, once, host-tested.
//
// THE INVARIANTS THIS TYPE EXISTS TO HOLD
//  1. Publish writes the payload BEFORE raising the flag; take() reads the payload BEFORE
//     clearing it.
//     This is a single-slot handoff without atomics, so a publish that lands mid-take is
//     inherently either lost or duplicated -- the order chooses WHICH. main.cpp cleared the
//     flag first, which duplicates: the consumer reads the new payload, the flag stays raised,
//     and the same sample is delivered again next loop (while the one it replaced is lost).
//     Reading first instead loses that sample outright. For calibration accumulation losing one
//     sample of hundreds is harmless, whereas a duplicate biases the fit toward one point -- so
//     read-then-clear is the correct trade, and it is the one this type makes.
//     Be honest about the limit: that window is between the BLE task and loop(), so a
//     single-threaded host test cannot observe it. The tests below pin the behaviour that IS
//     observable (deliver-once, coalescing, re-entrant publish); invariant 1 rests on the
//     reasoning above, not on a test. Making it airtight would need atomics, which is a real
//     option if a duplicate sample is ever suspected in a fit.
//  2. A slot coalesces: it holds only the LATEST value. Two publishes between takes drop the
//     older deliberately -- these are periodic samples where freshest wins, not a queue.
//  3. Draining a calibration pair applies REF BEFORE DUT, so the accumulator already has a
//     reference to pair the DUT sample against (main.cpp documented this in a comment with
//     nothing enforcing it).
//
// Header-only, no Arduino/RTOS dependency, so it compiles into the host test build and into
// both the ESP32 and nRF targets via lib_extra_dirs.

#include <stdint.h>

namespace sb20proxy {

// A single-slot, coalescing handoff from one producer context to one consumer context.
// T must be trivially copyable (these are small PODs crossing a task boundary).
template <class T>
class PendingSlot {
 public:
    // Producer context (BLE callback). Payload first, flag last.
    void publish(const T& v) {
        value_ = v;
        pending_ = true;
    }

    // Consumer context (loop). Returns false when nothing is pending; otherwise copies the
    // payload out and clears the flag. Payload first, flag last -- see invariant 1.
    bool take(T& out) {
        if (!pending_) return false;
        out = const_cast<const T&>(value_);
        pending_ = false;
        return true;
    }

    bool pending() const { return pending_; }

    // Drop anything pending without acting on it (e.g. a source dropped mid-flight).
    void clear() { pending_ = false; }

 private:
    volatile bool pending_ = false;
    T value_{};
};

// A slot carrying no payload -- just "this happened at least once since the last drain".
class PendingFlag {
 public:
    void publish() { pending_ = true; }
    bool take() {
        if (!pending_) return false;
        pending_ = false;
        return true;
    }
    bool pending() const { return pending_; }
    void clear() { pending_ = false; }

 private:
    volatile bool pending_ = false;
};

// One power sample crossing the task boundary.
struct CalSample {
    float power_w = 0.0f;
    uint32_t t_ms = 0;
};

// The reference/DUT pair of slots plus the mandated drain order.
//
// Sink is anything with onRef(float, uint32_t) and onDut(float, uint32_t) -- i.e.
// CalibrationSession, or a recording fake in the host tests.
class CalibrationDrain {
 public:
    void publishRef(float power_w, uint32_t t_ms) { ref_.publish(CalSample{power_w, t_ms}); }
    void publishDut(float power_w, uint32_t t_ms) { dut_.publish(CalSample{power_w, t_ms}); }

    // REF BEFORE DUT (invariant 3). Returns the number of samples delivered.
    template <class Sink>
    int drain(Sink& sink) {
        int n = 0;
        CalSample s;
        if (ref_.take(s)) {
            sink.onRef(s.power_w, s.t_ms);
            ++n;
        }
        if (dut_.take(s)) {
            sink.onDut(s.power_w, s.t_ms);
            ++n;
        }
        return n;
    }

    void clear() {
        ref_.clear();
        dut_.clear();
    }

    bool refPending() const { return ref_.pending(); }
    bool dutPending() const { return dut_.pending(); }

 private:
    PendingSlot<CalSample> ref_;
    PendingSlot<CalSample> dut_;
};

// Falling-edge detector: fires once on true -> false.
//
// main.cpp uses this for "the meter just dropped -> clear the last readings so the OLED and
// /stats stop showing a stale power_w alongside a 'searching' source". The subtlety worth
// pinning is that it must fire ONCE per transition, not every poll while disconnected, and must
// not fire on the very first poll of a session that starts disconnected.
class FallingEdge {
 public:
    explicit FallingEdge(bool initial = false) : last_(initial) {}

    bool update(bool now) {
        const bool fell = last_ && !now;
        last_ = now;
        return fell;
    }

    bool last() const { return last_; }

 private:
    bool last_;
};

}  // namespace sb20proxy
