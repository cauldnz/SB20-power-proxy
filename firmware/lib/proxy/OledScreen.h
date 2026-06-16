#pragma once
#include <array>
#include <string>

namespace sb20proxy {

// What the OLED is currently showing — derived from the WiFi state in main.cpp.
enum class OledMode { Portal, Connecting, Connected };

// Format the 0.42" (72x40) OLED's four text rows for a given state. Pure (no Arduino / U8g2), so
// the layout is host-tested exactly like Status.h; src/disp/OledDisplay.h draws these rows via
// U8g2 (the hardware seam). Rows are short by necessity — the panel fits ~12 chars at the 5x7
// font used for rows 2-4.
//   Portal     -> how to provision (join the AP, open the URL)
//   Connecting -> a brief "joining" notice
//   Connected  -> the IP (the thing you came for) + live power / cadence
inline std::array<std::string, 4> formatOledLines(OledMode mode, const std::string& ip,
                                                  int watts, int cadenceRpm) {
    switch (mode) {
        case OledMode::Portal:
            return {"SB20 SETUP", "join wifi:", "SB20-Setup", "192.168.4.1"};
        case OledMode::Connecting:
            return {"SB20 PROXY", "connecting", std::string(), std::string()};
        case OledMode::Connected: {
            std::string w = std::to_string(watts) + "W";
            std::string c = cadenceRpm >= 0 ? std::to_string(cadenceRpm) + " rpm" : std::string();
            return {"SB20 PROXY", ip, w, c};
        }
    }
    return {std::string(), std::string(), std::string(), std::string()};
}

}  // namespace sb20proxy
