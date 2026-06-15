#pragma once
#include <cstdint>

#include "Cps.h"  // pure (no NimBLE): CrankCadence + the measurement codec
#include "ICrankOutput.h"

class NimBLECharacteristic;  // NimBLE-Arduino (global namespace); kept out of the header

namespace sb20proxy {

// ICrankOutput over NimBLE: advertises as the Stages crank (Cycling Power Service) and
// emits each corrected reading as a CPS measurement notification — with Crank Revolution
// Data (cadence) when the reading carries it. The real-hardware twin of MockCrank.
// Arduino/NimBLE-only (excluded from the host `native` build).
class BleCrankPeripheral : public ICrankOutput {
public:
    void begin() override;
    void publishPower(const PowerReading& r) override;

private:
    NimBLECharacteristic* meas_ = nullptr;
    CrankCadence cadence_;        // advances crank revs / event time from each reading's rpm
    uint32_t lastT_ = 0;          // previous reading's t_ms, for the cadence dt
    bool haveLastT_ = false;
};

}  // namespace sb20proxy
