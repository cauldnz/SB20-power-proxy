#pragma once
#include <cstdint>
#include <string>

namespace sb20proxy {

// A snapshot of the proxy's runtime state — the pure, host-testable observability model.
// The WiFi layer (WifiLink) serves renderStatusJson() at GET / so the device can be curled:
// the C3 Super Mini's native-USB serial is flaky, so HTTP is the reliable window in (the
// same reason raedian-probe serves status over HTTP). The firmware analogue of the Python
// proxy's stats line.
struct ProxyStatus {
    const char* fw = "sb20proxy-esp32";
    bool sourceConnected = false;  // meter linked (always false in mock mode)
    bool mock = false;             // running the synthetic meter, no real source
    int32_t forwarded = 0;         // readings relayed to the crank
    int16_t lastPowerW = 0;        // last corrected power published
    int16_t lastCadenceRpm = -1;   // -1 = unknown
    int32_t rssi = 0;              // WiFi RSSI (0 when not applicable)
    uint32_t freeHeap = 0;         // ESP.getFreeHeap()
    uint32_t uptimeMs = 0;         // millis()
};

// Render a ProxyStatus as a compact JSON object. Pure (no Arduino / NimBLE), so it is
// host-tested exactly like the rest of lib/proxy.
inline std::string renderStatusJson(const ProxyStatus& s) {
    const char* source = s.mock ? "mock" : (s.sourceConnected ? "connected" : "searching");
    std::string j = "{";
    j += "\"fw\":\"";
    j += s.fw;
    j += "\",\"source\":\"";
    j += source;
    j += "\",\"forwarded\":" + std::to_string(s.forwarded);
    j += ",\"power_w\":" + std::to_string(s.lastPowerW);
    j += ",\"cadence_rpm\":" + std::to_string(s.lastCadenceRpm);
    j += ",\"rssi\":" + std::to_string(s.rssi);
    j += ",\"heap\":" + std::to_string(s.freeHeap);
    j += ",\"ms\":" + std::to_string(s.uptimeMs);
    j += "}";
    return j;
}

}  // namespace sb20proxy
