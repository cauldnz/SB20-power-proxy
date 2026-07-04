#pragma once
// ImuCapture — the pure, fixed-capacity IMU capture buffer (no Arduino/SoftDevice).
//
// LINEAR capture, not a ring: on the track you arm it at the line and want a predictable
// recording from that instant — it stops when full (capacity is telemetered over BLE so the
// web app can show remaining time). Samples are 6 x int16 (ax ay az gx gy gz) at the LSM6DS3's
// native scaling; timestamps are implicit (startMs + n/rateHz), which halves the memory cost.
#include <cstddef>
#include <cstdint>

namespace nrfbridge {

template <size_t CAPACITY_SAMPLES>
class ImuCapture {
 public:
    static constexpr size_t kAxes = 6;

    void start(uint32_t nowMs, uint8_t rateHz) {
        count_ = 0;
        startMs_ = nowMs;
        rateHz_ = rateHz;
        active_ = true;
    }
    void stop() { active_ = false; }
    void erase() {
        active_ = false;
        count_ = 0;
    }

    // Append one sample; returns false (and auto-stops) when full.
    bool add(const int16_t s[kAxes]) {
        if (!active_ || count_ >= CAPACITY_SAMPLES) {
            active_ = false;
            return false;
        }
        int16_t* dst = &buf_[count_ * kAxes];
        for (size_t i = 0; i < kAxes; ++i) dst[i] = s[i];
        ++count_;
        if (count_ >= CAPACITY_SAMPLES) active_ = false;  // full: stop, keep the data
        return true;
    }

    bool active() const { return active_; }
    uint32_t count() const { return (uint32_t)count_; }
    static constexpr uint32_t capacity() { return (uint32_t)CAPACITY_SAMPLES; }
    uint32_t startMs() const { return startMs_; }
    uint8_t rateHz() const { return rateHz_; }
    const int16_t* sample(size_t i) const { return &buf_[i * kAxes]; }

    // CRC32 (reflected, poly 0xEDB88320) over the recorded bytes — the download trailer.
    uint32_t crc32() const {
        uint32_t crc = 0xFFFFFFFFu;
        const uint8_t* p = (const uint8_t*)buf_;
        const size_t n = count_ * kAxes * sizeof(int16_t);
        for (size_t i = 0; i < n; ++i) {
            crc ^= p[i];
            for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }

 private:
    int16_t buf_[CAPACITY_SAMPLES * kAxes];
    size_t count_ = 0;
    uint32_t startMs_ = 0;
    uint8_t rateHz_ = 52;
    bool active_ = false;
};

}  // namespace nrfbridge
