#pragma once
#include "ICrankOutput.h"

class NimBLECharacteristic;  // NimBLE-Arduino (global namespace); kept out of the header

namespace sb20proxy {

// ICrankOutput over NimBLE: advertises as the Stages crank (Cycling Power Service) and
// emits each corrected reading as a CPS measurement notification. The real-hardware twin
// of MockCrank. Arduino/NimBLE-only (excluded from the host `native` build).
class BleCrankPeripheral : public ICrankOutput {
public:
    void begin() override;
    void publishPower(const PowerReading& r) override;

private:
    NimBLECharacteristic* meas_ = nullptr;
};

}  // namespace sb20proxy
