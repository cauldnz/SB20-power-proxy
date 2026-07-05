#pragma once
#include <array>
#include <string>

#include "Config.h"

namespace sb20proxy {

// What the OLED is currently showing — derived from the WiFi state in main.cpp.
enum class OledMode { Portal, Connecting, Connected };

// Format the 0.42" (72x40) OLED's four text rows for a given state. Pure (no Arduino / U8g2), so
// the layout is host-tested exactly like Status.h; src/disp/OledDisplay.h draws these rows via
// U8g2 (the hardware seam). Rows are short by necessity — the panel fits ~12 chars at the 5x7
// font used for rows 2-4.
//   Portal     -> how to provision (join the AP, open the URL)
//   Connecting -> a brief "joining" notice
//   Connected  -> the IP (the thing you came for) + a power+cadence row
// The panel only shows ~3 rows, so a 4th row falls off the bottom: for Connected, power and
// cadence therefore SHARE row 3 ("230W 85rpm") so the rebroadcast cadence is actually visible.
inline std::array<std::string, 4> formatOledLines(OledMode mode, const std::string& ip,
                                                  int watts, int cadenceRpm, int rssi = 0,
                                                  int balancePct = -1,
                                                  const std::string& setupPin = std::string()) {
    switch (mode) {
        case OledMode::Portal:
            // When the AP is WPA2-protected the rider needs the SSID + PIN to join (the captive portal
            // then auto-pops, so the IP falls off the bottom). Open AP -> the old "join + URL" layout.
            if (!setupPin.empty())
                return {"SB20 SETUP", "SB20-Setup", "PIN " + setupPin, Config::SETUP_PORTAL_HOST};
            return {"SB20 SETUP", "join wifi:", "SB20-Setup", Config::SETUP_PORTAL_HOST};
        case OledMode::Connecting:
            return {"SB20 PROXY", "connecting", std::string(), std::string()};
        case OledMode::Connected: {
            // Row 3 (lines[2]) = power then cadence, SHARED so both stay visible on the ~3-row
            // panel. When the meter reports an L/R split we append a compact "L44" and drop the
            // "rpm" unit so it still fits ~12 chars ("230W 85 L44"); no balance -> unchanged
            // ("230W 85rpm"). Title row = signal strength when connected, clearly labelled RSSI
            // (handy for positioning the board for a strong-enough OTA); rssi == 0 -> the brand.
            std::string row = std::to_string(watts) + "W";
            if (cadenceRpm >= 0)
                row += " " + std::to_string(cadenceRpm) + (balancePct >= 0 ? "" : "rpm");
            if (balancePct >= 0) row += " L" + std::to_string(balancePct);
            std::string title = rssi < 0 ? "WiFi " + std::to_string(rssi) : std::string("SB20 PROXY");
            return {title, ip, row, std::string()};
        }
    }
    return {std::string(), std::string(), std::string(), std::string()};
}

}  // namespace sb20proxy
