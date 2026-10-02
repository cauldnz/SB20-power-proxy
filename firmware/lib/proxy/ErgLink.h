#pragma once
#include <cstdint>

namespace sb20proxy {

// The erg leg's live state, as the web app's ride view reads it from GET /workout/state (#347, F17)
// — the HTTP twin of the nRF Wk characteristic's ergConnected / ergControlled flags and biasW.
//   connected  — the FTMS trainer link is up (FtmsErgClient::connected()).
//   controlled — the trainer granted control (Request Control answered); targets now drive it.
//   biasW      — the rider's local nudge on top of the workout target (the SB20 shifter's erg
//                actions and the web app's ±10 W), applied in main.cpp's erg sync.
// Pure data; the seam fills it per request.
struct ErgLink {
    bool connected = false;
    bool controlled = false;
    int biasW = 0;
};

// The bias is a nudge, not a second target: clamped to ±kErgBiasLimitW so a stuck button or a
// runaway client cannot walk the trainer to an absurd load. Same bound the shifter path has used
// since the button sink landed (main.cpp, ±200 W).
constexpr int kErgBiasLimitW = 200;

// Apply one nudge and return the new, clamped bias. Pure.
inline int16_t nudgeErgBias(int current, int deltaW) {
    const int v = current + deltaW;
    return (int16_t)(v < -kErgBiasLimitW ? -kErgBiasLimitW : (v > kErgBiasLimitW ? kErgBiasLimitW : v));
}

}  // namespace sb20proxy
