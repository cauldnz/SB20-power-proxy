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
    std::string srcName;           // the connected source's advertised name ("" if none)
    int32_t forwarded = 0;         // readings relayed to the crank
    // The proxy carries two power streams; the UI shows both so each direction is visible:
    //   * src*  — what we RECEIVED from the meter (the BLE-central / goal-#1 side)
    //   * last* — what we BROADCAST to the crank after correction (the peripheral / goal-#2 side)
    int16_t srcPowerW = 0;         // last power received from the source meter
    int16_t srcCadenceRpm = -1;    // -1 = unknown
    int16_t srcBalanceHalfPct = -1;   // left% × 2 received from the meter; -1 = none reported
    int16_t lastPowerW = 0;        // last corrected power published to the crank
    int16_t lastCadenceRpm = -1;   // -1 = unknown
    int16_t lastBalanceHalfPct = -1;  // left% × 2 forwarded to the crank; -1 = none (crank sends 50/50)
    int32_t rssi = 0;              // WiFi RSSI (0 when not applicable)
    uint32_t freeHeap = 0;         // ESP.getFreeHeap()
    uint32_t uptimeMs = 0;         // millis()
};

// Render a ProxyStatus as a compact JSON object. Pure (no Arduino / NimBLE), so it is
// host-tested exactly like the rest of lib/proxy.
// Minimal JSON string escape (backslash, double-quote, control chars) so a source name with an
// odd character can't break the JSON. Pure.
inline std::string jsonEscape(const std::string& in) {
    std::string o;
    o.reserve(in.size() + 2);
    for (char c : in) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) { o += ' '; }  // drop control chars
        else o += c;
    }
    return o;
}

inline std::string renderStatusJson(const ProxyStatus& s) {
    const char* source = s.mock ? "mock" : (s.sourceConnected ? "connected" : "searching");
    std::string j = "{";
    j += "\"fw\":\"";
    j += s.fw;
    j += "\",\"source\":\"";
    j += source;
    j += "\",\"src_name\":\"" + jsonEscape(s.srcName) + "\"";
    j += ",\"forwarded\":" + std::to_string(s.forwarded);
    // src_* = received from the meter (goal #1); power_w/cadence_rpm = broadcast to the crank (goal #2)
    j += ",\"src_power_w\":" + std::to_string(s.srcPowerW);
    j += ",\"src_cadence_rpm\":" + std::to_string(s.srcCadenceRpm);
    // left-pedal % (half-pct / 2), -1 when the meter reports no L/R split. balance passes through
    // the correction unchanged, so src_ and the broadcast value match when present.
    j += ",\"src_balance_pct\":" + std::to_string(s.srcBalanceHalfPct < 0 ? -1 : s.srcBalanceHalfPct / 2);
    j += ",\"power_w\":" + std::to_string(s.lastPowerW);
    j += ",\"cadence_rpm\":" + std::to_string(s.lastCadenceRpm);
    j += ",\"balance_pct\":" + std::to_string(s.lastBalanceHalfPct < 0 ? -1 : s.lastBalanceHalfPct / 2);
    j += ",\"rssi\":" + std::to_string(s.rssi);
    j += ",\"heap\":" + std::to_string(s.freeHeap);
    j += ",\"ms\":" + std::to_string(s.uptimeMs);
    j += "}";
    return j;
}

}  // namespace sb20proxy
