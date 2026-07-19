#pragma once
// UiModel.h — the shared, pure UI view-model structs, compiled by EVERY panel build (OLED + LCD).
//
// These are the "what to show" data structs the head-unit UIs project from. They live here (not in
// LcdUi.h) so the OLED builds — which do NOT compile the LVGL/canvas renderers or LcdUi.h — can still
// speak the same view-model vocabulary: the C3 OLED projects its 4 text rows from the very same
// RideView/ProvisionView the LCD boards fill (see OledScreen.h::formatOledLines). LcdUi.h includes
// this header and adds the LCD-only views (workout/setup/more/calibrate) + the renderers.
//
// Pure (no Arduino / U8g2 / LVGL) — host-unit-tested like the rest of lib/proxy.
#include <cstdint>
#include <string>

#include "MeterCompare.h"  // MeterBand + kTorqueBands (CompareView's torque domain)

namespace sb20proxy {

// The ride/live-data view — the numbers both panels display (power/cadence/balance + link state).
struct RideView {
    std::string srcName;      // "ASSIOMA17039L" or "searching…"
    std::string outName;      // the identity we advertise ("Stages 62144")
    bool srcOn = false;
    bool outOn = true;
    int16_t watts = 0;        // broadcast power (the hero)
    int16_t srcWatts = 0;     // received power (details)
    int16_t cadence = -1;
    int16_t balancePct = -1;  // left %
    const int16_t* hist = nullptr;  // power history ring, oldest first
    int nHist = 0;
    int16_t histMax = 300;
    // details panel extras
    int32_t wifiRssi = 0;
    uint32_t uptimeMs = 0;
    uint32_t freeHeap = 0;
    std::string version;
    // live workout strip
    bool wkRunning = false;
    bool wkPaused = false;
    int16_t wkTarget = -1;
    long wkRemainS = 0;
};

// WiFi onboarding (the captive setup portal): when active, the LCD boards replace the normal UI
// with a join-this-AP screen (QR + SSID/PIN); the C3's OLED shows the same facts as text.
struct ProvisionView {
    bool portal = false;
    std::string apSsid;   // per-device setup AP name, e.g. "Setup-A6E9"
    std::string pin;      // the AP's WPA2 password (per-device PIN on OLED builds)
    std::string url;      // where the portal lives (Config::SETUP_PORTAL_URL)
};

// #10 A/B meter compare — what the Compare screen shows (filled by CompareService::fillView).
// Lives here, not LcdUi.h, so it stays free of LcdCanvas/LCD_PANEL: CompareService (pure, and
// compiled on WiFi-only builds for the GET /compare payload) speaks this vocabulary too.
// The per-band chart is bias% by TORQUE band — it reveals torque-dependent error that a power
// axis mixes away (code/findings/meter-compare-visualization.md).
struct CompareView {
    std::string aName = "Meter A";
    std::string bName = "Meter B";
    bool valid = false;
    // B is FABRICATED from A by the bench adapter (B := A x ratio), so every "finding" below is a
    // restatement of that ratio — NOT a measurement. Surfaces must say so instead of printing a
    // verdict: whether the real SB20 error is flat or torque-dependent is the OPEN question these
    // views exist to answer (code/findings/meter-compare-visualization.md), not one to fake an
    // answer to. False once a real second meter feeds the B source.
    bool simulated = false;
    int16_t aWatts = 0, bWatts = 0, deltaW = 0;
    float ratio = 1.0f;
    float biasPct = 0.0f;
    uint16_t nPairs = 0;
    // DERIVED from the core's torque domain, never a hand-picked subset: an 8-band head-unit silently
    // dropped 40-60 N·m — exactly where sprints clamp and where the error is worst — while /compare
    // showed all 12. Same data, two domains, no test to catch it. Deriving it makes that undriftable.
    static constexpr int NBANDS = MeterCompare::kTorqueBands;
    MeterBand bands[NBANDS];   // per-torque-band bias; nPairs == 0 means "empty band" (no sentinel)
};

}  // namespace sb20proxy
