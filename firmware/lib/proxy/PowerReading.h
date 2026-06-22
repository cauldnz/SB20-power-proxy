#pragma once
#include <cstdint>

namespace sb20proxy {

// The lingua franca between a power source (meter/mock) and the spoofed crank —
// the C++ mirror of the Python PowerReading.
struct PowerReading {
    int16_t power_w = 0;
    int16_t cadence_rpm = -1;       // -1 = unknown
    int16_t balance_half_pct = -1;  // left pedal % × 2 (0..200), -1 = unknown (no source split)
    uint32_t t_ms = 0;              // millis() when received (0 in host tests)
};

}  // namespace sb20proxy
