#pragma once
#include <cstdint>

#include "Config.h"  // SPOOF_CRANK_LENGTH_HALFMM default
#include "Cps.h"  // pure (no NimBLE): CrankCadence + the measurement codec
#include "ICrankOutput.h"

class NimBLECharacteristic;  // NimBLE-Arduino (global namespace); kept out of the header

namespace sb20proxy {

// ICrankOutput over NimBLE: advertises as the real Stages SPM2 crank (Cycling Power Service
// + the Stages proprietary service) and emits the captured 0x2F measurement frame — power +
// pedal-balance + accumulated-torque + crank-rev cadence — so the SB20 accepts it as genuine.
// The real-hardware twin of MockCrank. Arduino/NimBLE-only (excluded from the host build).
class BleCrankPeripheral : public ICrankOutput {
public:
    void begin() override;
    void publishPower(const PowerReading& r) override;

private:
    NimBLECharacteristic* meas_ = nullptr;
    CrankCadence cadence_;        // advances crank revs / event time from each reading's rpm
    uint16_t accumTorque_ = 0;    // accumulated torque (1/32 Nm), advanced per completed rev
    uint32_t lastT_ = 0;          // previous reading's t_ms, for the cadence dt
    bool haveLastT_ = false;
    uint16_t crankLengthHalfMm_ = Config::SPOOF_CRANK_LENGTH_HALFMM;  // CP 0x04 set / 0x05 read
};

}  // namespace sb20proxy
