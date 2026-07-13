#pragma once
// AntMasterScheduler — the PURE page-scheduling "brain" of an ANT+ Bike Power master: decides which
// 8-byte page to broadcast each period. Mirrors the Phase-0-validated Python
// `StagesAntTarget._next_page` (code/src/sb20proxy/targets/stages_ant.py:120):
//   1. a pending calibration response wins (answer the bike's zero-reset promptly);
//   2. a queued identity-commons burst (manufacturer 0x50 / product 0x51 / battery 0x52);
//   3. otherwise the power-only data page (0x10) — and every `commonsEvery` periods, enqueue a fresh
//      commons burst first (leading with commons at begin() so a display sees who we are immediately).
//
// Pure + header-only (no SoftDevice / Arduino) → host-tested in CI, exactly like AntBikePower.h. The
// hardware channel (src/ant/AntMasterChannel, S340) just calls nextPage() on each ANT EVENT_TX and
// hands the bytes to sd_ant_broadcast_message_tx.
#include <cstddef>
#include <cstdint>

#include "AntBikePower.h"

namespace nrfant {

// Cosmetic broadcaster identity for the common pages. Real spoof values come from a capture; for a
// mock/bring-up master any valid values make a head unit show "a power meter", so these are safe
// placeholders (serial defaults to the Stages device number).
struct MasterIdentity {
    uint8_t hwRev = 1;
    uint16_t mfgId = 1;     // ANT+ manufacturer id (1 = garmin/dev placeholder)
    uint16_t model = 1;
    uint8_t swMain = 1;
    uint32_t serial = 62144;  // Stages L crank device number
};

// commonsEvery = data pages between identity bursts. The ANT+ Bike Power profile requires the required
// common pages to appear at least every ~121 messages; 121 matches the reference cadence.
class AntMasterScheduler {
public:
    explicit AntMasterScheduler(uint16_t commonsEvery = 121) : commonsEvery_(commonsEvery) {}

    void setIdentity(const MasterIdentity& id) { id_ = id; }
    void setCalibrationOffset(int16_t off) { calOffset_ = off; }

    // Reset stream state; lead with the identity commons.
    void begin() {
        eventCount_ = 0;
        accumPower_ = 0;
        sinceCommons_ = 0;
        calPending_ = 0;
        pendHead_ = pendCount_ = 0;
        if (commonsEvery_) enqueueCommons();
    }

    // The bike sent a manual-zero (calibration) request → answer with `repeats` calibration broadcasts.
    void queueCalibration(uint8_t repeats = 3) { calPending_ = repeats; }

    // Fill out[8] with the next page to broadcast. cadence/balance < 0 = "not provided". Returns 8.
    size_t nextPage(int watts, int cadence, int balance, uint8_t out[8]) {
        if (calPending_ > 0) {
            --calPending_;
            return encodeCalibrationResponse(calOffset_, CAL_ID_MANUAL_ZERO_SUCCESS, 0, out);
        }
        if (pendCount_ > 0) return popPending(out);
        if (commonsEvery_ && ++sinceCommons_ >= commonsEvery_) {
            sinceCommons_ = 0;
            enqueueCommons();
            return popPending(out);
        }
        return dataPage(watts, cadence, balance, out);
    }

private:
    size_t dataPage(int watts, int cadence, int balance, uint8_t out[8]) {
        const uint16_t p = (uint16_t)(watts < 0 ? 0 : watts);
        eventCount_ = (uint8_t)(eventCount_ + 1);
        accumPower_ = (uint16_t)(accumPower_ + p);
        const uint8_t pedal = (balance >= 0) ? pedalPowerByte(balance) : ANT_RESERVED;
        return encodePowerOnly(eventCount_, p, accumPower_, cadence, pedal, out);
    }
    void enqueueCommons() {
        uint8_t page[8];
        encodeManufacturerInfo(id_.hwRev, id_.mfgId, id_.model, page);
        pushPending(page);
        encodeProductInfo(id_.swMain, id_.serial, ANT_RESERVED, page);
        pushPending(page);
        encodeBatteryStatus(/*batteryId=*/0, /*opTimeLsb=*/0, /*voltFrac=*/0, /*statusByte=*/0, page);
        pushPending(page);
    }

    // A tiny FIFO of pages (a commons burst is 3; enqueued only when empty, so 4 is ample).
    static constexpr int kMaxPend = 4;
    void pushPending(const uint8_t p[8]) {
        if (pendCount_ >= kMaxPend) return;
        const int slot = (pendHead_ + pendCount_) % kMaxPend;
        for (int i = 0; i < 8; ++i) pending_[slot][i] = p[i];
        ++pendCount_;
    }
    size_t popPending(uint8_t out[8]) {
        for (int i = 0; i < 8; ++i) out[i] = pending_[pendHead_][i];
        pendHead_ = (pendHead_ + 1) % kMaxPend;
        --pendCount_;
        return 8;
    }

    uint16_t commonsEvery_;
    uint16_t sinceCommons_ = 0;
    uint8_t eventCount_ = 0;
    uint16_t accumPower_ = 0;
    uint8_t calPending_ = 0;
    int16_t calOffset_ = 903;  // the captured Stages manual-zero offset (decisions.md)
    MasterIdentity id_;
    uint8_t pending_[kMaxPend][8] = {};
    int pendHead_ = 0, pendCount_ = 0;
};

}  // namespace nrfant
