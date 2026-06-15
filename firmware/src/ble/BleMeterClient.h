#pragma once
#include <cstdint>

#include "IPowerSource.h"

namespace sb20proxy {

// IPowerSource over NimBLE: scans for the configured power meter, connects, subscribes to
// its Cycling Power Measurement, and emits PowerReadings. The real-hardware twin of
// MockMeter. Arduino/NimBLE-only (excluded from the host `native` build).
class BleMeterClient : public IPowerSource {
public:
    void onReading(ReadingCb cb) override { cb_ = cb; }
    void begin() override;
    void loop() override;

    // called from NimBLE callbacks
    void onFound(const char* addr, uint8_t addrType);
    void onPower(int16_t power_w);

private:
    ReadingCb cb_;
    bool haveTarget_ = false;
    bool connected_ = false;
    char addr_[24] = {0};
    uint8_t addrType_ = 0;
};

}  // namespace sb20proxy
