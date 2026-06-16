#pragma once
#include <cstddef>
#include <cstdint>

#include "IPowerSource.h"

class NimBLEClient;  // NimBLE-Arduino (global namespace); kept out of the header

namespace sb20proxy {

// IPowerSource over NimBLE: scans for the configured power meter, connects, subscribes to
// its Cycling Power Measurement, and emits PowerReadings. The real-hardware twin of
// MockMeter. Arduino/NimBLE-only (excluded from the host `native` build).
//
// Matching is by ADVERTISED CPS SERVICE UUID (0x1818) first, name second: a Windows BLE
// peripheral (our winrt fake_meter, or bless) advertises the service UUID but not a custom
// local name, so a name-only filter would never see it. A real Assioma advertises both.
class BleMeterClient : public IPowerSource {
public:
    void onReading(ReadingCb cb) override { cb_ = cb; }
    void begin() override;
    void loop() override;

    bool connected() const { return connected_; }

    // called from NimBLE callbacks
    void onFound(const char* addr, uint8_t addrType);
    void onMeasurement(const uint8_t* data, size_t len);  // decode power (+ cadence) and emit
    void onDisconnected();  // link dropped: clear state and rescan from loop()

private:
    ReadingCb cb_;
    NimBLEClient* client_ = nullptr;
    bool haveTarget_ = false;
    bool connected_ = false;
    bool wantRescan_ = false;
    char addr_[24] = {0};
    uint8_t addrType_ = 0;
    // Crank-revolution state, to recover the meter's cadence the way a head unit does.
    bool havePrevCrank_ = false;
    uint16_t prevRevs_ = 0;
    uint16_t prevEventTime_ = 0;
    uint32_t lastReadingMs_ = 0;  // for the staleness watchdog (meter went silent -> rescan)
};

}  // namespace sb20proxy
