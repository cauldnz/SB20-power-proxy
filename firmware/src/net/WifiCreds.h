#pragma once
#include "Provisioning.h"  // WifiCredentials

namespace sb20proxy {

// Non-volatile (NVS) storage for the WiFi credentials provisioned by the captive portal,
// so a network is set once and survives reboots / OTA flashes. Thin wrapper over the ESP32
// Preferences API (namespace "wifi"). Arduino-only — compiled only when USE_WIFI=1; the
// pure WifiCredentials type and the portal logic it serves live in Provisioning.h and are
// host-tested there.
class WifiCreds {
public:
    // Load stored creds into `out`. Returns true iff a non-empty SSID was stored.
    static bool load(WifiCredentials& out);
    // Persist `c` (overwrites any existing creds).
    static void save(const WifiCredentials& c);
    // Erase stored creds (forces the portal on next boot).
    static void clear();
    // True iff a non-empty SSID is stored.
    static bool has();
};

}  // namespace sb20proxy
