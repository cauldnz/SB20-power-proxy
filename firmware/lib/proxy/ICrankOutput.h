#pragma once
#include "PowerReading.h"

namespace sb20proxy {

// The target half — the spoofed Stages crank. ProxyCore publishes corrected power to
// THIS. MockCrank (host tests) records what it's asked to publish; BleCrankPeripheral
// (real) emits it as a BLE Cycling Power notification.
class ICrankOutput {
public:
    virtual ~ICrankOutput() {}

    virtual void begin() = 0;                         // start advertising as the crank
    virtual void publishPower(const PowerReading& r) = 0;
};

}  // namespace sb20proxy
