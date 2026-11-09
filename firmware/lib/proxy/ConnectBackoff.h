#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// How long to leave a meter alone after it failed to give us any data. Pure + host-tested.
//
// Each pedal of a dual Assioma advertises separately, so a name filter matches both. The right pedal
// was seen connecting and dropping again for more than five hours, which held the main loop up about
// 27 times a minute. Without a hold, the same advert wins every time and the other pedal gets no turn.
//
// A failure is a link that ended before the first frame arrived. One frame clears the record, so a
// working meter that drops mid-ride reconnects straight away. No allocation: this is called with
// interrupts held off, from both the BLE host task and the main loop.
namespace sb20proxy {

class ConnectBackoff {
 public:
    static constexpr uint32_t kFirstHoldMs = 5000;
    static constexpr uint32_t kMaxHoldMs = 30000;
    static constexpr size_t kSlots = 4;

    bool heldOff(const char* addr, uint32_t nowMs) const {
        const Slot* s = find(addr);
        return s && static_cast<int32_t>(nowMs - s->untilMs) < 0;  // survives the millis() wrap
    }

    void recordFailure(const char* addr, uint32_t nowMs) {
        Slot* s = find(addr);
        if (!s) s = claim(addr, nowMs);
        uint32_t hold = kFirstHoldMs;
        for (uint8_t i = 0; i < s->failures && hold < kMaxHoldMs; ++i) hold *= 2;
        if (hold > kMaxHoldMs) hold = kMaxHoldMs;
        if (s->failures < 255) ++s->failures;
        s->untilMs = nowMs + hold;
        s->retryDue = true;
    }

    void recordSuccess(const char* addr) {
        if (Slot* s = find(addr)) *s = Slot{};
    }

    // True once when a hold has ended. The scan reports each address only once per run, so an advert
    // heard during the hold is gone for good unless the caller restarts the scan at this point.
    bool takeExpired(uint32_t nowMs) {
        bool any = false;
        for (Slot& s : slots_) {
            if (s.retryDue && static_cast<int32_t>(nowMs - s.untilMs) >= 0) {
                s.retryDue = false;
                any = true;
            }
        }
        return any;
    }

    void clear() {
        for (Slot& s : slots_) s = Slot{};
    }

 private:
    struct Slot {
        char addr[24] = {0};
        uint32_t untilMs = 0;
        uint8_t failures = 0;
        bool retryDue = false;
    };

    const Slot* find(const char* addr) const {
        for (const Slot& s : slots_) {
            if (s.failures && std::strncmp(s.addr, addr, sizeof(s.addr) - 1) == 0) return &s;
        }
        return nullptr;
    }
    Slot* find(const char* addr) {
        return const_cast<Slot*>(static_cast<const ConnectBackoff*>(this)->find(addr));
    }

    // A free slot, or else the one whose hold ends soonest.
    Slot* claim(const char* addr, uint32_t nowMs) {
        Slot* pick = &slots_[0];
        for (Slot& s : slots_) {
            if (!s.failures) {
                pick = &s;
                break;
            }
            if (static_cast<int32_t>(s.untilMs - nowMs) < static_cast<int32_t>(pick->untilMs - nowMs)) {
                pick = &s;
            }
        }
        *pick = Slot{};
        std::strncpy(pick->addr, addr, sizeof(pick->addr) - 1);
        return pick;
    }

    Slot slots_[kSlots];
};

}  // namespace sb20proxy
