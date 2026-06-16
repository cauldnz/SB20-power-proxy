#pragma once

namespace sb20proxy {

// What the proxy spoofs and reads. Phase 0 / Phase 1 values; later these move to NVS +
// an on-device setup wizard. Kept platform-agnostic so the core + tests see the same values.
struct Config {
    // --- the crank we impersonate (the Stages L crank, from the Phase 0 captures) ---
    static constexpr const char* SPOOF_NAME         = "Stages 62144";
    static constexpr const char* SPOOF_MANUFACTURER = "Stages Cycling";
    static constexpr const char* SPOOF_SERIAL       = "11821518";
    static constexpr int         SPOOF_CAL_OFFSET   = 903;  // captured 0xAC zero-offset

    // --- the real meter to read (BLE central): name substring match ---
    static constexpr const char* METER_NAME_FILTER  = "ASSIOMA";

    // --- meter-to-meter correction (linear; a fitted grid lands later) ---
    static constexpr float CORRECTION_SCALE  = 1.0f;
    static constexpr float CORRECTION_OFFSET = 0.0f;

    // --- board (ESP32-C3 Super Mini) ---
    static constexpr int STATUS_LED_PIN = 8;  // onboard LED, active-low (LOW = lit)
};

}  // namespace sb20proxy
