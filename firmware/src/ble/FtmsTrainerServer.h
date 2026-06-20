#pragma once
#include <cstddef>
#include <cstdint>

// FTMS trainer-server seam: a NimBLE peripheral that presents itself as a Fitness
// Machine (Indoor Bike Data + a Control Point that accepts Set Target Power). The
// real-hardware twin of the pure Ftms.h codec; Arduino/NimBLE-only (excluded from the
// host build). Flag-gated (esp32c3-ftms-server env). Validated on-air by the bench
// loop (F6); SPEC-BUILT pending Session 4 Part C.

class NimBLECharacteristic;  // NimBLE-Arduino (global namespace)

namespace sb20proxy {

class FtmsTrainerServer {
 public:
    // Stand up the FTMS service + advertise under `deviceName`.
    void begin(const char* deviceName);
    // Push an Indoor Bike Data notification (instantaneous power + cadence).
    void publishPower(int16_t power_w, float cadenceRpm);

    // observed control state (for the test main's serial log + the bench assert)
    int16_t targetPower() const { return target_; }
    bool hasTarget() const { return hasTarget_; }
    bool controlled() const { return controlled_; }
    bool started() const { return started_; }

    // mutated by the control-point callback
    NimBLECharacteristic* statusChar_ = nullptr;
    int16_t target_ = 0;
    bool hasTarget_ = false;
    bool controlled_ = false;
    bool started_ = false;

 private:
    NimBLECharacteristic* ibd_ = nullptr;
};

}  // namespace sb20proxy
