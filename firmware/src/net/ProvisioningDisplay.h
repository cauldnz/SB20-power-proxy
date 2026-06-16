#pragma once

namespace sb20proxy {

// A pluggable surface for telling the user how to provision WiFi. The proxy ships headless:
// the default impl (SerialProvisioningDisplay) just logs to the serial console, and the
// captive portal itself auto-pops on the phone. This seam lets a future OLED / e-paker /
// QR-code module drop in without touching the portal logic in WifiLink — implement these
// three calls and inject it via WifiLink::begin(..., display).
class IProvisioningDisplay {
public:
    virtual ~IProvisioningDisplay() = default;

    // Portal is up: join WiFi AP `apSsid`, then open `url` (shown as a QR code on a display).
    virtual void showPortal(const char* apSsid, const char* url) = 0;
    // Attempting to join the stored network `ssid`.
    virtual void showJoining(const char* ssid) = 0;
    // Joined; reachable at `ip`.
    virtual void showConnected(const char* ip) = 0;
};

}  // namespace sb20proxy

#if defined(ARDUINO)
#include <Arduino.h>

namespace sb20proxy {

// Default headless display: the serial console. Used when no display is injected.
class SerialProvisioningDisplay : public IProvisioningDisplay {
public:
    void showPortal(const char* apSsid, const char* url) override {
        Serial.printf("[wifi] SETUP: join WiFi network '%s', then open %s\n", apSsid, url);
    }
    void showJoining(const char* ssid) override {
        Serial.printf("[wifi] joining '%s'...\n", ssid);
    }
    void showConnected(const char* ip) override {
        Serial.printf("[wifi] connected; status at http://%s/\n", ip);
    }
};

}  // namespace sb20proxy
#endif  // ARDUINO
