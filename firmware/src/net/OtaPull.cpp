// Implementation of the OTA HTTPS fetch + Update flash seam (see OtaPull.h). Bench-validated, not
// host-tested (it's the radio/flash glue); the decision/verify logic it serves is the host-tested
// OtaUpdater. Compiled only with USE_WIFI.
#if defined(USE_WIFI) && USE_WIFI

#include "net/OtaPull.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>

#include "net/DebugLog.h"

using namespace sb20proxy;

namespace {
// A WiFiClientSecure pinned to our root CA. Returns false (fail-closed) if no CA is configured.
bool makePinnedClient(WiFiClientSecure& client, const char* rootCaPem) {
    if (rootCaPem == nullptr || rootCaPem[0] == '\0') {
        logf("[ota] refused: no pinned CA configured (fail-closed)\n");
        return false;
    }
    client.setCACert(rootCaPem);
    return true;
}
}  // namespace

bool HttpsFetcher::fetchText(const std::string& url, std::string& out) {
    WiFiClientSecure client;
    if (!makePinnedClient(client, rootCaPem_)) return false;
    HTTPClient http;
    if (!http.begin(client, url.c_str())) return false;
    const int code = http.GET();
    if (code != 200) {
        logf("[ota] manifest GET %s -> %d\n", url.c_str(), code);
        http.end();
        return false;
    }
    out = std::string(http.getString().c_str());
    http.end();
    return true;
}

bool HttpsFetcher::fetchStream(const std::string& url,
                               const std::function<bool(const uint8_t*, size_t)>& sink) {
    WiFiClientSecure client;
    if (!makePinnedClient(client, rootCaPem_)) return false;
    HTTPClient http;
    if (!http.begin(client, url.c_str())) return false;
    const int code = http.GET();
    if (code != 200) {
        logf("[ota] image GET %s -> %d\n", url.c_str(), code);
        http.end();
        return false;
    }
    int remaining = http.getSize();  // -1 if the server sent no Content-Length (chunked)
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    bool ok = true;
    while (http.connected() && (remaining > 0 || remaining == -1)) {
        const size_t avail = stream->available();
        if (avail == 0) { delay(1); continue; }
        const int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
        if (n <= 0) break;
        if (!sink(buf, (size_t)n)) { ok = false; break; }  // writer failed -> stop
        if (remaining > 0) remaining -= n;
    }
    http.end();
    return ok && remaining <= 0;  // fully consumed (remaining hits 0, or -1 then loop ends on EOF)
}

bool UpdateWriter::begin(size_t totalSize) {
    return Update.begin(totalSize > 0 ? totalSize : UPDATE_SIZE_UNKNOWN);
}

bool UpdateWriter::write(const uint8_t* data, size_t len) {
    return Update.write(const_cast<uint8_t*>(data), len) == len;
}

bool UpdateWriter::end() {
    return Update.end(true);  // true = set the boot partition to the new image
}

void UpdateWriter::abort() { Update.abort(); }

#endif  // USE_WIFI
