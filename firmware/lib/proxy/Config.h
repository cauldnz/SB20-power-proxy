#pragma once

#include <cstdint>

// Firmware build version (semver) — the OTA fleet channel compares this against a release
// manifest's version (OtaManifest::shouldUpdate), and it's surfaced in /status + /diag so we
// know which build a tester is on. Override per release with a build flag in platformio.ini /
// the release runbook:   build_flags = -DSB20_FIRMWARE_VERSION=\"0.2.0\"
#ifndef SB20_FIRMWARE_VERSION
#define SB20_FIRMWARE_VERSION "0.1.0"
#endif

namespace sb20proxy {

// What the proxy spoofs and reads. Phase 0 / Phase 1 values; later these move to NVS +
// an on-device setup wizard. Kept platform-agnostic so the core + tests see the same values.
struct Config {
    // Build version, surfaced in /status + /diag and used as the OTA "currentVersion".
    static constexpr const char* FIRMWARE_VERSION = SB20_FIRMWARE_VERSION;

    // --- the crank we impersonate (the Stages L crank; values captured 2026-06-17 over BLE) ---
    static constexpr const char* SPOOF_NAME         = "Stages 62144";
    static constexpr const char* SPOOF_MANUFACTURER = "Stages Cycling";
    static constexpr const char* SPOOF_MODEL        = "SPM2";    // DIS model number (2A24)
    static constexpr const char* SPOOF_FW           = "1.8.2";   // DIS firmware revision (2A26)
    static constexpr const char* SPOOF_SERIAL       = "11821518";
    // BLE Start Offset Compensation (0x2A66) reply offset. The real Stages crank returns **0** over
    // BLE after a zero-reset (captured `200c010000` in G-crank62144-ble-zero-20260615-070353.jsonl),
    // NOT the ANT+ raw zero-offset 903 (page 0x01 / 0xAC). They are different representations of the
    // same calibration (see decisions.md 2026-06-17 offset reconciliation); this firmware IS the BLE
    // crank, so its zero-reset must answer with the BLE value, 0.
    static constexpr int         SPOOF_CAL_OFFSET   = 0;
    // Manufacturer Company ID (uint16) carried in the ENHANCED Offset Compensation (0x10) reply the
    // Stages app sends for its zero-reset — the spec puts it after the offset (see Cps.h
    // encodeEnhancedOffsetCompResponse). GROUNDED on-bike 2026-06-25 (session 8 G1, capture
    // G-crank62144-ble-enhanced-0x10-20260625-0716.jsonl): the real Stages SPM2 crank's 0x10 reply is
    // `20 10 01 00 00 ba 01 04 85 03 b7 03` -> offset 0, **company id 0x01BA = 442** (the Stages
    // company id; independently corroborated by the SB20's own advert manufacturer-data key 442), then
    // 5 bytes of opaque mfg data (SPOOF_MFG_DATA). The old 0x0000 placeholder left the Stages app's
    // calibrate spinning (session 8 G2); replying with the real 442 + mfg data is the candidate fix.
    static constexpr uint16_t    SPOOF_MFG_COMPANY_ID = 0x01BA;  // 442 = Stages Cycling
    // Opaque manufacturer-specific data trailing the company id in the real crank's captured 0x10 reply
    // (session 8 G1). Replayed verbatim for byte-faithfulness — the app likely treats it as opaque
    // (company id 442 is the field that matters), but matching the real crank exactly avoids guessing.
    static constexpr uint8_t     SPOOF_MFG_DATA[]   = {0x04, 0x85, 0x03, 0xB7, 0x03};
    // Crank length reported (CP op 0x05) and stored when the bike sets it (0x04), in 1/2 mm.
    // 345 = 172.5 mm — the real crank's captured Request-Crank-Length value (`20 05 59 01`).
    static constexpr uint16_t    SPOOF_CRANK_LENGTH_HALFMM = 345;

    // --- corrector mode: our OWN, honest identity (NOT a spoof). A head unit / Garmin accepts any
    //     standard CPS meter, so the meter-to-meter corrector advertises as itself — no Stages
    //     proprietary service, generic DIS. The advertised name is runtime (RuntimeConfig.spoofName,
    //     set by the calibration wizard); these are the fixed DIS strings + the default name. ---
    static constexpr const char* CORRECTOR_NAME         = "SB20 Corrector";
    static constexpr const char* CORRECTOR_MANUFACTURER = "SB20Proxy";
    static constexpr const char* CORRECTOR_MODEL        = "Corrector";

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
    // BENCH ONLY: built with -DMETER_MATCH_ANY_CPS=1 (the *-bench envs), read ANY CPS-advertising
    // device that is not our spoof and not a "Stages "-named crank. This is how the WinRT fake_meter
    // rig is matched — Windows stamps the PC's name (not "ASSIOMA") into the scan response, defeating
    // the name filter, while still advertising CPS 0x1818 in the primary advert (decisions.md
    // 2026-06-22). OFF in production: there the name filter ensures we read only the configured meter,
    // never a stranger's CPS device.
#ifndef METER_MATCH_ANY_CPS
#define METER_MATCH_ANY_CPS 0
#endif
    static constexpr bool MATCH_ANY_CPS = (METER_MATCH_ANY_CPS != 0);

    // --- meter-to-meter correction (linear; a fitted grid lands later) ---
    static constexpr float CORRECTION_SCALE  = 1.0f;
    static constexpr float CORRECTION_OFFSET = 0.0f;

    // --- board (ESP32-C3 Super Mini) ---
    static constexpr int STATUS_LED_PIN = 8;  // onboard LED, active-low (LOW = lit)

    // --- WiFi setup AP (always WPA2-protected) ---
    // OLED builds: the AP password is a per-device 8-digit PIN derived from the chip MAC + this secret
    // (SetupPin.h), shown on the screen. Override per deployment for a unique secret; see SetupPin.h
    // for the threat-model caveat.
    static constexpr const char* SETUP_PIN_SECRET = "sb20proxy-setup-ap-v1";
    // Screenless builds: no display to show a per-device PIN, so the AP uses this KNOWN default
    // passphrase (>= 8 chars, WPA2 minimum) that the user can type. Shared across screenless boards —
    // weaker than the per-device PIN, but usable without a screen. Override per deployment.
    static constexpr const char* SETUP_AP_DEFAULT_PASSWORD = "sb20setup";
};

}  // namespace sb20proxy
