// Recent-log ring + Serial mirror, served at GET /log. Compiled only when USE_WIFI=1, like
// the rest of src/net (the default BLE build keeps Serial-only logging).
#if defined(USE_WIFI) && USE_WIFI

#include "net/DebugLog.h"

#include <Arduino.h>

#include <cstdarg>
#include <cstdio>

using namespace sb20proxy;

LogBuffer& sb20proxy::debugLog() {
    static LogBuffer buf(60);  // ~60 recent lines; oldest drop
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

#endif  // USE_WIFI
