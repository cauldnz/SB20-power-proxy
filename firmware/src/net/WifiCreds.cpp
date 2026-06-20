// NVS-backed WiFi credential storage. The ENTIRE body is compiled only when USE_WIFI=1
// (the esp32c3-ota env), matching WifiLink.cpp — the default BLE build pulls in neither
// WiFi nor Preferences.
#if defined(USE_WIFI) && USE_WIFI

#include "net/WifiCreds.h"

#include <Preferences.h>

using namespace sb20proxy;

namespace {
constexpr const char* kNamespace = "wifi";
constexpr const char* kKeySsid = "ssid";
constexpr const char* kKeyPass = "pass";
constexpr const char* kKeyLog = "log";
}  // namespace

bool WifiCreds::load(WifiCredentials& out) {
    Preferences p;
    p.begin(kNamespace, /*readOnly=*/true);
    out.ssid = std::string(p.getString(kKeySsid, "").c_str());
    out.pass = std::string(p.getString(kKeyPass, "").c_str());
    p.end();
    return !out.ssid.empty();
}

void WifiCreds::save(const WifiCredentials& c) {
    Preferences p;
    p.begin(kNamespace, /*readOnly=*/false);
    p.putString(kKeySsid, c.ssid.c_str());
    p.putString(kKeyPass, c.pass.c_str());
    p.end();
}

void WifiCreds::clear() {
    Preferences p;
    p.begin(kNamespace, /*readOnly=*/false);
    p.clear();
    p.end();
}

bool WifiCreds::logEnabled(bool dflt) {
    Preferences p;
    p.begin(kNamespace, /*readOnly=*/true);
    bool v = p.getBool(kKeyLog, dflt);
    p.end();
    return v;
}

void WifiCreds::setLogEnabled(bool on) {
    Preferences p;
    p.begin(kNamespace, /*readOnly=*/false);
    p.putBool(kKeyLog, on);
    p.end();
}

#endif  // USE_WIFI
