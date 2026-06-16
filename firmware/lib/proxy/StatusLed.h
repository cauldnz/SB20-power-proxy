#pragma once
#include <cstdint>

namespace sb20proxy {

// The link state the onboard status LED reflects. Mirrors the raedian-probe idiom: blink fast
// while the user still has to act (setup portal up) or a join is in flight, pulse slowly once
// joined and healthy.
enum class LinkState {
    Searching,  // setup portal raised, or a station join not yet healthy — fast blink
    Connected,  // joined the network, HTTP up — slow pulse
};

// Pure blink-pattern generator: given the link state and the current millis, returns whether the
// status LED should be ON (logical "lit"). The active-low GPIO mapping is the caller's seam
// (src/main.cpp), so this stays host-testable with no Arduino. A symmetric square wave whose
// half-period encodes the state: fast (~4 Hz) while Searching, slow (~0.5 Hz) while Connected.
struct StatusLed {
    static constexpr uint32_t SEARCHING_HALF_MS = 120;   // fast blink — searching / portal
    static constexpr uint32_t CONNECTED_HALF_MS = 1000;  // slow pulse — connected

    static uint32_t halfPeriodMs(LinkState s) {
        return s == LinkState::Connected ? CONNECTED_HALF_MS : SEARCHING_HALF_MS;
    }

    static bool lit(LinkState s, uint32_t now_ms) {
        return (now_ms / halfPeriodMs(s)) % 2 == 0;
    }
};

}  // namespace sb20proxy
