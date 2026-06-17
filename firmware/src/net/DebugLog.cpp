// Recent-log ring + Serial mirror, served at GET /log when WiFi is up. Always compiled (the ring
// is cheap and the BLE peripheral logs the consumer's writes through it for field observation);
// only the /log HTTP endpoint that *serves* it is WiFi-gated, over in WifiLink. Excluded from the
// host build like the rest of src/. NEVER log secrets — /log is readable over the open setup AP.

#include "net/DebugLog.h"

#include <Arduino.h>

#include <cstdarg>
#include <cstdio>

using namespace sb20proxy;

LogBuffer& sb20proxy::debugLog() {
    static LogBuffer buf(120);  // ~120 recent lines; oldest drop. Roomier for field capture.
    return buf;
}

void sb20proxy::logf(const char* fmt, ...) {
    char line[LogBuffer::kMaxLine + 1];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    Serial.println(line);
    debugLog().add(std::string(line));
}
