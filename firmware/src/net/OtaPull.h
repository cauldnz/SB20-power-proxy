#pragma once
// The Arduino seam behind OtaUpdater's interfaces: a real HTTPS fetch (WiFiClientSecure pinned to our
// back end's root CA) and the ESP32 Update flash writer. The decision/verify logic is the pure,
// host-tested OtaUpdater (lib/proxy/OtaUpdater.h); this file is the thin, bench-validated radio/flash
// glue. Compiled only with USE_WIFI. See code/findings/ota-update-plan.md (P2).
#if defined(USE_WIFI) && USE_WIFI

#include <functional>
#include <string>

#include "OtaUpdater.h"

namespace sb20proxy {

// Fetch over TLS, validating the server against a pinned root CA (our back end's Let's-Encrypt root).
// FAIL-CLOSED: if no CA is configured it refuses to fetch (never falls back to an unauthenticated
// connection), so a half-configured device can't be fed an unverified manifest/image.
class HttpsFetcher : public IFirmwareFetcher {
public:
    explicit HttpsFetcher(const char* rootCaPem) : rootCaPem_(rootCaPem) {}
    bool fetchText(const std::string& url, std::string& out) override;
    bool fetchStream(const std::string& url,
                     const std::function<bool(const uint8_t*, size_t)>& sink) override;

private:
    const char* rootCaPem_;  // PEM of the trusted root; nullptr/"" => refuse (fail-closed)
};

// Writes the downloaded image into the inactive OTA slot via the Arduino Update API.
class UpdateWriter : public IFirmwareWriter {
public:
    bool begin(size_t totalSize) override;
    bool write(const uint8_t* data, size_t len) override;
    bool end() override;
    void abort() override;
};

}  // namespace sb20proxy

#endif  // USE_WIFI
