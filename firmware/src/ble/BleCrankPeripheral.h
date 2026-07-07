#pragma once
#include <cstdint>
#include <functional>
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

    // Enable an OpenBikeControl (OBC) BLE service (a Button-State notify char, d273f681) alongside the
    // crank, so the SB20's re-presented handlebar buttons can drive OBC-speaking apps (MyWhoosh/qz) over
    // BLE. Call before begin(). See lib/proxy/Obc.h + code/findings/obc-protocol.md.
    void setObcEnabled(bool e) { obcEnabled_ = e; }
    // Devmode: advertise as an "OBC-…" controller (name overridden, not the Stages crank) so an OBC
    // listener (e.g. qz) can discover us by an OBC-prefixed name and drive virtual button presses over
    // HTTP (GET /obc/press). Implies the OBC service. Call before begin().
    void setObcDevmode(bool e) { obcDevmode_ = e; }
    // Notify the OBC Button-State characteristic with a pre-encoded OBC message (from Obc.h). No-op if
    // OBC is disabled / no subscriber.
    void notifyObc(const uint8_t* data, size_t len);

    // Hook invoked (from the control-point write callback) when the SB20/app requests an offset-
    // compensation / zero-reset (CP 0x0C or 0x10). The seam wires this to forward a REAL zero to the
    // source meter (the Assioma). The handler MUST be cheap + non-blocking — it runs on the NimBLE host
    // task, so it should just flag work for loop() to drain (never issue a central BLE op re-entrantly).
    // Call before begin(). Optional; defaults to no-op.
    void setZeroResetHandler(std::function<void()> cb) { onZeroReset_ = std::move(cb); }

private:
    ProxyMode mode_ = ProxyMode::Spoof;
    std::string spoofName_ = Config::SPOOF_NAME;      // advertised identity
    std::string spoofSerial_ = Config::SPOOF_SERIAL;  // DIS serial (0x2A25)
    NimBLECharacteristic* meas_ = nullptr;
    bool obcEnabled_ = false;
    bool obcDevmode_ = false;                        // advertise as an OBC controller (test/bring-up)
    NimBLECharacteristic* obcButtonChar_ = nullptr;  // OBC Button-State (notify), created when obcEnabled_
    CrankCadence cadence_;        // advances crank revs / event time from each reading's rpm
    uint16_t accumTorque_ = 0;    // accumulated torque (1/32 Nm), advanced per completed rev
    uint32_t lastT_ = 0;          // previous reading's t_ms, for the cadence dt
    bool haveLastT_ = false;
    uint16_t crankLengthHalfMm_ = Config::SPOOF_CRANK_LENGTH_HALFMM;  // CP 0x04 set / 0x05 read
    std::function<void()> onZeroReset_;  // fired on a 0x0C/0x10 CP write -> seam forwards a zero to source
};

}  // namespace sb20proxy
