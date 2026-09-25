#pragma once
#include <array>
#include <string>

namespace sb20proxy {

// The FTMS trainer simulator's four OLED rows.
//
// Why this exists: the simulator build (`esp32c3-ftms-server`) compiles only the FTMS server, so it
// never touched the panel — a board running it looked *identical to a dead one*, and on a board
// with factory demo firmware it still showed that, because an OLED retains its last frame. With a
// box of a dozen identical C3s on the bench that is a trap, and it cost real confusion on
// 2026-09-25. A simulator you cannot identify by looking at it is a bench hazard.
//
// The rows answer the three questions someone standing at the bench actually has: is this the
// simulator, has a head unit taken control of it, and what target is it being given?
//
// Pure (no Arduino / U8g2) so the layout is host-tested; src/disp/OledDisplay.h draws these rows.
// The 0.42" panel fits ~12 chars at the 5x7 font used for rows 2-4, so rows are kept short.
//
//   FTMS SIM        <- what this board IS, visible across the desk
//   <link state>    <- idle / linked / CONTROLLED
//   tgt <n>W        <- the target the head unit has set (the thing under test)
//   pwr <n>W        <- the synthetic power it is reporting back
inline std::array<std::string, 4> formatSimOledLines(bool controlled, bool started, bool hasTarget,
                                                     int targetW, int powerW) {
    std::array<std::string, 4> out;
    out[0] = "FTMS SIM";

    // `controlled` = a central has connected and written the control point; `started` = it sent
    // Start/Resume. Both true is the state the erg rows assert, so it gets the loud label.
    if (controlled && started) {
        out[1] = "CONTROLLED";
    } else if (controlled) {
        out[1] = "linked";
    } else {
        out[1] = "idle";
    }

    // A target of 0 while controlled is meaningful, not missing: the head unit releases the target
    // to 0 W on pause and stop. Distinguish "no target set at all" from "target is zero".
    out[2] = hasTarget ? ("tgt " + std::to_string(targetW) + "W") : std::string("tgt --");
    out[3] = "pwr " + std::to_string(powerW) + "W";
    return out;
}

}  // namespace sb20proxy
