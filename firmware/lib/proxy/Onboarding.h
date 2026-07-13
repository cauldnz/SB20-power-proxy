#pragma once
// Onboarding — shared, pure helpers for the WiFi setup (captive-portal) flow, so every panel builds
// the join affordances from ONE tested source. Post-U3 the onboarding *data* is already shared (the
// ProvisionView in UiModel.h: apSsid / pin / url); this adds the one remaining un-shared, un-tested
// primitive: the "WIFI:" QR payload a phone camera scans to join the board's setup AP.
//
// Pure (no Arduino / LVGL / U8g2) — host-unit-tested like the rest of lib/proxy. See
// code/findings/ui-unification.md §U4.
#include <string>

namespace sb20proxy {

// Backslash-escape the characters that are special in the "WIFI:" QR grammar (MECARD-like). Without
// this, a ';' ':' ',' '\\' or '"' in the SSID or password would truncate/corrupt the payload and the
// QR would fail to join. (Our AP names "Setup-XXXX" + numeric PINs don't hit these today, but the
// escape keeps it correct if either ever contains a special char — e.g. a future word-list PIN.)
inline std::string wifiQrEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

// The WiFi-provisioning QR payload: "WIFI:T:WPA;S:<ssid>;P:<pin>;;" (T=WPA auth type). A phone's
// camera scans this to join the board's setup AP. The on-panel QR (LvglUi provisionSync) renders
// exactly this string.
inline std::string wifiQrPayload(const std::string& ssid, const std::string& pin) {
    return "WIFI:T:WPA;S:" + wifiQrEscape(ssid) + ";P:" + wifiQrEscape(pin) + ";;";
}

}  // namespace sb20proxy
