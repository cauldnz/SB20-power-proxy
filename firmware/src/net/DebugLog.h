#pragma once
#include "LogBuffer.h"

namespace sb20proxy {

// The process-wide recent-log ring, mirrored from Serial so it can be served at GET /log when
// the C3's native-USB serial is unreliable. Always compiled (the BLE peripheral logs consumer
// writes through it); only the /log endpoint that serves it is WiFi-gated. NEVER log secrets
// through this — /log is readable over the open setup AP (passwords must never be passed to logf).
LogBuffer& debugLog();

// printf-style: append one line to debugLog() AND echo it to Serial.
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

}  // namespace sb20proxy
