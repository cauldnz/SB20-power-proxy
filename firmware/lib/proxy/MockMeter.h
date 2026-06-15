#pragma once
#include "IPowerSource.h"

namespace sb20proxy {

// A synthetic power source — the firmware twin of the Python PowerMeterTwin. Used by host
// tests (call emit()) and by the on-device bench demo (main.cpp drives emit() on a timer)
// so the spoofed crank can be flashed and witnessed on a phone/Garmin with no real meter.
class MockMeter : public IPowerSource {
public:
    void onReading(ReadingCb cb) override { cb_ = cb; }
    void begin() override {}
    void loop() override {}

    void emit(int16_t power_w, int16_t cadence_rpm = 85, uint32_t t_ms = 0) {
        PowerReading r;
        r.power_w = power_w;
        r.cadence_rpm = cadence_rpm;
        r.t_ms = t_ms;
        if (cb_) cb_(r);
    }

private:
    ReadingCb cb_;
};

}  // namespace sb20proxy
