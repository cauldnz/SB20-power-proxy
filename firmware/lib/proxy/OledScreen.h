#pragma once
#include <array>
#include <string>

#include "Config.h"
#include "UiModel.h"   // RideView + ProvisionView — the shared view-model the LCD boards also fill

namespace sb20proxy {

// What the OLED is currently showing — derived from the WiFi state in main.cpp.
enum class OledMode { Portal, Connecting, Connected };

// Format the C3 OLED's four text rows from the SHARED view-model (the same RideView/ProvisionView
// the LCD boards fill via buildLcdViews) — so the OLED and LCD project one model, not two. Pure (no
// Arduino / U8g2), so the layout is host-tested; src/disp/OledDisplay.h draws these rows via U8g2.
// `wifiUp` distinguishes Connecting from Connected (the one OLED state the views don't carry); `ip`
// is the connected IP (MoreView.ip on the LCD side). Rows are short by necessity — the panel fits
// ~12 chars at the 5x7 font used for rows 2-4, and only ~3 rows are visible, so the 4th falls off.
//   Portal     -> how to provision (join the AP, open the URL)
//   Connecting -> a brief "joining" notice
//   Connected  -> the IP (the thing you came for) + a power+cadence row
inline std::array<std::string, 4> formatOledLines(const ProvisionView& prov, const RideView& ride,
                                                  bool wifiUp, const std::string& ip) {
    if (prov.portal) {
        // When the AP is WPA2-protected the rider needs the SSID + PIN to join (the captive portal
        // then auto-pops, so the IP falls off the bottom). Open AP -> the "join + URL" layout.
        // apSsid is the per-device name (e.g. "Setup-A6E9") so the screen matches the broadcast AP
        // when several boards are in setup mode at once.
        const std::string ssid = prov.apSsid.empty() ? std::string("Setup") : prov.apSsid;
        if (!prov.pin.empty())
            return {"SB20 SETUP", ssid, "PIN " + prov.pin, Config::SETUP_PORTAL_HOST};
        return {"SB20 SETUP", "join wifi:", ssid, Config::SETUP_PORTAL_HOST};
    }
    if (!wifiUp)
        return {"SB20 PROXY", "connecting", std::string(), std::string()};
    // Connected. Row 3 (lines[2]) = power then cadence, SHARED so both stay visible on the ~3-row
    // panel. When the meter reports an L/R split we append a compact "L44" and drop the "rpm" unit so
    // it still fits ~12 chars ("230W 85 L44"); no balance -> "230W 85rpm". Title row = signal
    // strength when connected, clearly labelled RSSI (handy for positioning the board for a
    // strong-enough OTA); rssi >= 0 -> the brand.
    std::string row = std::to_string(ride.watts) + "W";
    if (ride.cadence >= 0)
        row += " " + std::to_string(ride.cadence) + (ride.balancePct >= 0 ? "" : "rpm");
    if (ride.balancePct >= 0) row += " L" + std::to_string(ride.balancePct);
    std::string title = ride.wifiRssi < 0 ? "WiFi " + std::to_string(ride.wifiRssi)
                                          : std::string("SB20 PROXY");
    return {title, ip, row, std::string()};
}

// Scalar convenience adapter (back-compat + the host tests): builds the shared views from loose
// scalars then delegates to the struct-based projection above. main.cpp's OLED task uses the
// struct form directly (it fills a RideView/ProvisionView), so the live path is fully model-driven.
inline std::array<std::string, 4> formatOledLines(OledMode mode, const std::string& ip,
                                                  int watts, int cadenceRpm, int rssi = 0,
                                                  int balancePct = -1,
                                                  const std::string& setupPin = std::string(),
                                                  const std::string& apSsid = "Setup") {
    ProvisionView prov;
    RideView ride;
    ride.watts = (int16_t)watts;
    ride.cadence = (int16_t)cadenceRpm;
    ride.balancePct = (int16_t)balancePct;
    ride.wifiRssi = rssi;
    bool wifiUp = false;
    switch (mode) {
        case OledMode::Portal:
            prov.portal = true;
            prov.apSsid = apSsid;
            prov.pin = setupPin;
            break;
        case OledMode::Connecting:
            break;  // wifiUp stays false
        case OledMode::Connected:
            wifiUp = true;
            break;
    }
    return formatOledLines(prov, ride, wifiUp, ip);
}

}  // namespace sb20proxy
