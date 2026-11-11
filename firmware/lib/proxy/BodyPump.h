#pragma once
// BodyPump.h — send HTTP response bodies a slice at a time from loop(), never blocking it.
//
// Why (2026-11-11, measured on the bike C3): the board used to write a page body synchronously,
// waiting inside loop() until the phone had taken every byte. With WiFi power-save on (it must stay
// on beside BLE), about one page load in four stalled for 1.5-2.6 s, and loop() -- which forwards the
// meter's power to the bike -- was frozen for the whole stall (/stats loop_max_us 2,629 ms for an
// 8.5 KB page; 0.4-3.3 s for the 64 KB /app). Opening the web page mid-ride dropped the bike's power.
//
// Now the adapter sends the status line and headers, hands the body here, and returns. Each loop()
// pass calls pump(), which writes what the socket will take WITHOUT waiting and moves on. A phone
// that stops taking data is dropped after its stall budget.
//
// Pure + header-only: the socket write is injected (`WriteFn`), so the slicing, completion and
// stall rules are host-tested (test_webroutes). The Arduino seam (WifiLink) supplies a writer that
// calls lwIP send() with MSG_DONTWAIT on the held client's fd.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace sb20proxy {

class BodyPump {
public:
    // Write up to `len` bytes without blocking. Return the bytes taken (> 0), 0 if the socket is
    // full right now (try again next pass), or < 0 if the connection is gone.
    using WriteFn = std::function<int(const char* data, size_t len)>;
    // Called once per body when it is finished, sent or abandoned, so the seam can close the socket.
    using DoneFn = std::function<void(bool complete)>;

    static constexpr size_t kMaxBodies = 4;       // concurrent bodies in flight
    static constexpr size_t kSlice = 1460;        // one TCP segment per write call
    static constexpr size_t kSlicesPerPass = 4;   // cap the work one loop() pass can do

    // Queue a body. `owned` keeps a dynamic page alive; `data`/`len` may instead point at a static
    // page in flash (then pass an empty `owned`). Returns false when full: the caller must then
    // send some other way.
    bool add(std::string owned, const char* data, size_t len, WriteFn write, DoneFn done,
             uint32_t nowMs, uint32_t stallMs) {
        if (bodies_.size() >= kMaxBodies) return false;
        Body b;
        b.owned = std::move(owned);
        b.data = b.owned.empty() ? data : nullptr;  // resolved after the move into the vector
        b.len = b.owned.empty() ? len : b.owned.size();
        b.write = std::move(write);
        b.done = std::move(done);
        b.lastProgressMs = nowMs;
        b.stallMs = stallMs;
        bodies_.push_back(std::move(b));
        return true;
    }

    // One non-blocking pass over every body in flight.
    void pump(uint32_t nowMs) {
        for (size_t i = 0; i < bodies_.size();) {
            Body& b = bodies_[i];
            const char* base = b.owned.empty() ? b.data : b.owned.data();
            bool gone = false;
            for (size_t s = 0; s < kSlicesPerPass && b.off < b.len; ++s) {
                const size_t want = (b.len - b.off > kSlice) ? kSlice : (b.len - b.off);
                const int n = b.write(base + b.off, want);
                if (n < 0) { gone = true; break; }
                if (n == 0) break;  // socket full: come back next pass
                b.off += static_cast<size_t>(n);
                b.lastProgressMs = nowMs;
            }
            const bool complete = b.off >= b.len;
            const bool stalled = !complete && static_cast<uint32_t>(nowMs - b.lastProgressMs) > b.stallMs;
            if (complete || gone || stalled) {
                if (b.done) b.done(complete);
                bodies_.erase(bodies_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }

    size_t inFlight() const { return bodies_.size(); }

private:
    struct Body {
        std::string owned;
        const char* data = nullptr;
        size_t len = 0;
        size_t off = 0;
        WriteFn write;
        DoneFn done;
        uint32_t lastProgressMs = 0;
        uint32_t stallMs = 0;
    };
    std::vector<Body> bodies_;
};

}  // namespace sb20proxy
