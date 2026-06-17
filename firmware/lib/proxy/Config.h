#pragma once

#include <cstdint>

namespace sb20proxy {

// What the proxy spoofs and reads. Phase 0 / Phase 1 values; later these move to NVS +
// an on-device setup wizard. Kept platform-agnostic so the core + tests see the same values.
struct Config {
    // --- the crank we impersonate (the Stages L crank; values captured 2026-06-17 over BLE) ---
    static constexpr const char* SPOOF_NAME         = "Stages 62144";
    static constexpr const char* SPOOF_MANUFACTURER = "Stages Cycling";
    static constexpr const char* SPOOF_MODEL        = "SPM2";    // DIS model number (2A24)
    static constexpr const char* SPOOF_FW           = "1.8.2";   // DIS firmware revision (2A26)
    static constexpr const char* SPOOF_SERIAL       = "11821518";
    static constexpr int         SPOOF_CAL_OFFSET   = 903;  // captured 0xAC zero-offset
    // Crank length reported (CP op 0x05) and stored when the bike sets it (0x04), in 1/2 mm.
    // 345 = 172.5 mm — the real crank's captured Request-Crank-Length value (`20 05 59 01`).
    static constexpr uint16_t    SPOOF_CRANK_LENGTH_HALFMM = 345;

    // Stages proprietary service + chars (captured GATT) — advertised + exposed so the SB20
    // recognises us as a genuine Stages crank. (Contents still opaque; presence is the point.)
    static constexpr const char* STAGES_SVC       = "d445fe01-d139-9a5d-6707-1cc6a58b6303";
    static constexpr const char* STAGES_CHAR_CTRL = "d445fe02-d139-9a5d-6707-1cc6a58b6303";  // notify+write
    static constexpr const char* STAGES_CHAR_DATA = "d445fe03-d139-9a5d-6707-1cc6a58b6303";  // notify

    // --- the real meter to read (BLE central): name substring match ---
    static constexpr const char* METER_NAME_FILTER  = "ASSIOMA";
    // Optional: pin the meter to READ to one exact BLE address (lowercase, colon-separated, as it
    // appears in /log). "" = match by name/UUID. Set this to stop bouncing between meters (session 2),
    // or to target a specific crank for the single-right-crank use case — e.g. "e3:25:39:38:92:71".
    static constexpr const char* METER_ADDRESS      = "";

    // --- meter-to-meter correction (linear; a fitted grid lands later) ---
    static constexpr float CORRECTION_SCALE  = 1.0f;
    static constexpr float CORRECTION_OFFSET = 0.0f;

    // --- board (ESP32-C3 Super Mini) ---
    static constexpr int STATUS_LED_PIN = 8;  // onboard LED, active-low (LOW = lit)
};

}  // namespace sb20proxy
