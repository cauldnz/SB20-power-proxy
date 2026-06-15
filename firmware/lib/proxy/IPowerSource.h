#pragma once
#include <functional>

#include "PowerReading.h"

namespace sb20proxy {

// The source half — a power meter. ProxyCore drives THIS, never BLE directly, so the
// same core runs against MockMeter (host tests) or BleMeterClient (real, behind NimBLE).
// Mirrors the raedian-probe IChargerControl pattern (interface + mock + real impl).
class IPowerSource {
public:
    using ReadingCb = std::function<void(const PowerReading&)>;
    virtual ~IPowerSource() {}

    virtual void onReading(ReadingCb cb) = 0;  // register the sink (ProxyCore)
    virtual void begin() = 0;                  // open I/O (scan/connect, or nothing for mock)
    virtual void loop() = 0;                   // service the source (reconnect, etc.)
};

}  // namespace sb20proxy
