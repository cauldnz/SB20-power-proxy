#pragma once
#include <functional>

#include "Status.h"  // pure ProxyStatus + renderStatusJson (host-tested)

class WebServer;  // ESP32 Arduino (global namespace); kept out of the header

namespace sb20proxy {

// WiFi observability + OTA for the proxy, mirroring the raedian-probe failsafe idiom:
// joins WiFi (creds from firmware/wifi_secret.h), serves the status JSON at GET / and an
// OTA upload form at /update, and self-resets if it never becomes healthy (so a bad flash
// that can't rejoin the network recovers on its own). Compiled only when USE_WIFI=1 (the
// esp32c3-ota env); the default build leaves it out entirely, so no creds are needed to
// build. Arduino-only — the status model it serves (Status.h) is host-tested.
class WifiLink {
public:
    using StatusProvider = std::function<ProxyStatus()>;

    // Arm the boot-guard, join WiFi, start OTA + the HTTP server. `provider` is called per
    // request to render the live status JSON.
    void begin(const char* hostname, StatusProvider provider);

    // Call from loop(): services HTTP + OTA and promotes to healthy (which cancels the
    // boot-guard and validates the running OTA image).
    void handle();

    bool isUp() const { return healthy_; }

private:
    WebServer* server_ = nullptr;
    StatusProvider provider_;
    bool healthy_ = false;
};

}  // namespace sb20proxy
