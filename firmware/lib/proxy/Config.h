#pragma once

#include <cstdint>

// Firmware build version (semver) — the OTA fleet channel compares this against a release
// manifest's version (OtaManifest::shouldUpdate), and it's surfaced in /status + /diag so we
// know which build a tester is on. Override per release with a build flag in platformio.ini /
// the release runbook:   build_flags = -DSB20_FIRMWARE_VERSION=\"0.2.0\"
#ifndef SB20_FIRMWARE_VERSION
#define SB20_FIRMWARE_VERSION "0.1.0"
#endif

// Build stamp — the git short SHA + build time, injected by scripts/build_version.py so we can tell
// dev builds apart (the semver above only changes at release). Fallbacks keep host/test builds compiling
// when the pre-build hook didn't run. Surfaced next to the semver in /status + /diag + the screen.
#ifndef SB20_BUILD_SHA
#define SB20_BUILD_SHA "nogit"
#endif
#ifndef SB20_BUILD_TIME
#define SB20_BUILD_TIME "nobuild"
#endif

namespace sb20proxy {

// What the proxy spoofs and reads. Phase 0 / Phase 1 values; later these move to NVS +
// an on-device setup wizard. Kept platform-agnostic so the core + tests see the same values.
struct Config {
    // Build version, surfaced in /status + /diag and used as the OTA "currentVersion".
    static constexpr const char* FIRMWARE_VERSION = SB20_FIRMWARE_VERSION;
    // Build stamp (git SHA + build time) — uniquely identifies a dev build; see build_version.py.
    static constexpr const char* BUILD_SHA  = SB20_BUILD_SHA;
    static constexpr const char* BUILD_TIME = SB20_BUILD_TIME;

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

    // --- board status LED (active-low, LOW = lit) ---
    // Default 8 = the C3 Super Mini's onboard LED. Boards MUST override where 8 is not safe:
    // on a classic ESP32, GPIOs 6-11 are the SPI-FLASH bus — driving GPIO 8 wedges the chip
    // (TG1WDT boot loop; found porting to the CYD, whose RGB-red lives on GPIO 4).
#ifndef SB20_STATUS_LED_PIN
#define SB20_STATUS_LED_PIN 8
#endif
    static constexpr int STATUS_LED_PIN = SB20_STATUS_LED_PIN;

    // --- WiFi setup AP (always WPA2-protected) ---
    // OLED builds: the AP password is a per-device 8-digit PIN derived from the chip MAC + this secret
    // (SetupPin.h), shown on the screen. Override per deployment for a unique secret; see SetupPin.h
    // for the threat-model caveat.
    static constexpr const char* SETUP_PIN_SECRET = "sb20proxy-setup-ap-v1";
    // Screenless builds: no display to show a per-device PIN, so the AP uses this KNOWN default
    // passphrase (>= 8 chars, WPA2 minimum) that the user can type. Shared across screenless boards —
    // weaker than the per-device PIN, but usable without a screen. Override per deployment.
    static constexpr const char* SETUP_AP_DEFAULT_PASSWORD = "sb20setup";

    // The setup-AP (captive portal) network: 172.29.4.0/24. Home routers essentially never
    // default to 172.16/12, and this /24 also dodges Docker's usual 172.17-.20 bridge pools and
    // AWS's 172.31 default VPC — the collision case is a tester's laptop (Ethernet + our AP) or
    // a phone VPN sharing the portal's subnet, which silently breaks routing to the portal
    // (owner call, 2026-07-05; the old 192.168.4.1 is the universal ESP default = worst case).
    static constexpr uint8_t SETUP_AP_IP[4] = {172, 29, 4, 1};
    static constexpr const char* SETUP_PORTAL_HOST = "172.29.4.1";
    static constexpr const char* SETUP_PORTAL_URL = "http://172.29.4.1/";
};

}  // namespace sb20proxy
