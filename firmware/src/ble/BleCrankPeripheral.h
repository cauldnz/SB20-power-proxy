#pragma once
#include <cstdint>
#include <string>

#include "Config.h"  // SPOOF_* / CORRECTOR_* identity
#include "Cps.h"  // pure (no NimBLE): CrankCadence + the measurement codec
#include "ICrankOutput.h"
#include "RuntimeConfig.h"  // ProxyMode (Spoof | Corrector)

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

    // Set the advertised identity at RUNTIME (from NVS / the web UI) — the advertised name (the
    // "Stages NNNNN" the SB20 pairs to, or our own name in CORRECTOR mode) and the DIS serial. Call
    // before begin(). Defaults to Config.
    void setIdentity(const std::string& name, const std::string& serial) {
        spoofName_ = name;
        spoofSerial_ = serial;
    }
    const std::string& spoofName() const { return spoofName_; }

    // Product mode: SPOOF presents the full Stages crank (proprietary service + Stages DIS) so the
    // SB20 accepts it; CORRECTOR presents a plain, honest CPS power meter (generic DIS, no Stages
    // service) any head unit / Garmin pairs to. Call before begin(). Defaults to SPOOF.
    void setMode(ProxyMode m) { mode_ = m; }

private:
    ProxyMode mode_ = ProxyMode::Spoof;
    std::string spoofName_ = Config::SPOOF_NAME;      // advertised identity
    std::string spoofSerial_ = Config::SPOOF_SERIAL;  // DIS serial (0x2A25)
    NimBLECharacteristic* meas_ = nullptr;
    CrankCadence cadence_;        // advances crank revs / event time from each reading's rpm
    uint16_t accumTorque_ = 0;    // accumulated torque (1/32 Nm), advanced per completed rev
    uint32_t lastT_ = 0;          // previous reading's t_ms, for the cadence dt
    bool haveLastT_ = false;
    uint16_t crankLengthHalfMm_ = Config::SPOOF_CRANK_LENGTH_HALFMM;  // CP 0x04 set / 0x05 read
};

}  // namespace sb20proxy
