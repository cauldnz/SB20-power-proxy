#pragma once
// AntMasterChannel — the S340 hardware seam for an ANT+ Bike Power MASTER. Assigns + opens the ANT
// channel under the captured Stages-crank identity (device 62144, Bike Power 0x0B, tx-type 5, RF 57,
// period 8182), and on each EVENT_TX broadcasts the next page from the pure AntMasterScheduler; on an
// acknowledged RX carrying the SB20's manual-zero request it queues a calibration response.
//
// INERT unless the licensed S340 SoftDevice + ANT+ key are provisioned (both gitignored) — the guard
// keeps the default S140/BLE build byte-identical. Defines NRF_HAS_ANT for main.cpp to gate its hooks.
#if defined(S340) && __has_include("ant_network_key.h")
#define NRF_HAS_ANT 1

#include <cstdint>

extern "C" {
#include "ant_interface.h"
#include "ant_parameters.h"
#include "nrf_error.h"
}

#include "ant_network_key.h"   // ANT_PLUS_NETWORK_KEY (vendor/softdevice, gitignored)
#include "AntBikePower.h"
#include "AntMasterScheduler.h"

namespace nrfant {

class AntMasterChannel {
public:
    // The captured Stages-crank ANT+ Bike Power master contract (mirrors ant/master.py:ChannelParams).
    static constexpr uint8_t  kChannel = 0;
    static constexpr uint16_t kDeviceNumber = 62144;   // Stages L crank
    static constexpr uint8_t  kDeviceType = 0x0B;      // Bike Power
    static constexpr uint8_t  kTransType = 5;
    static constexpr uint8_t  kRfFreq = 57;            // 2457 MHz
    static constexpr uint16_t kChannelPeriod = 8182;   // ~4.06 Hz
    static constexpr uint8_t  kNetwork = 0;

    // Assign + open the master channel + seed the first broadcast. Returns the first non-zero SD error
    // (NRF_SUCCESS = 0 = up). Must be called AFTER the SoftDevice is enabled (i.e. after Bluefruit.begin).
    uint32_t begin() {
        uint8_t key[8] = ANT_PLUS_NETWORK_KEY;
        uint32_t err;
        beginStep_ = 1; if ((err = sd_ant_network_address_set(kNetwork, key)) != NRF_SUCCESS) return (beginErr_ = err);
        beginStep_ = 2; if ((err = sd_ant_channel_assign(kChannel, CHANNEL_TYPE_MASTER, kNetwork, /*ext*/ 0)) != NRF_SUCCESS) return (beginErr_ = err);
        beginStep_ = 3; if ((err = sd_ant_channel_id_set(kChannel, kDeviceNumber, kDeviceType, kTransType)) != NRF_SUCCESS) return (beginErr_ = err);
        beginStep_ = 4; if ((err = sd_ant_channel_radio_freq_set(kChannel, kRfFreq)) != NRF_SUCCESS) return (beginErr_ = err);
        beginStep_ = 5; if ((err = sd_ant_channel_period_set(kChannel, kChannelPeriod)) != NRF_SUCCESS) return (beginErr_ = err);
        beginStep_ = 6; if ((err = sd_ant_channel_open(kChannel)) != NRF_SUCCESS) return (beginErr_ = err);
        beginStep_ = 0;  // 0 = all steps passed
        sched_.begin();
        broadcastNext();  // seed the buffer so the first on-air message isn't a zero page
        opened_ = true;
        return (beginErr_ = NRF_SUCCESS);
    }

    // The current power reading to broadcast (cadence/balance < 0 = "not provided").
    void setReading(int watts, int cadence, int balance) {
        watts_ = watts;
        cadence_ = cadence;
        balance_ = balance;
    }

    // Drain the ANT event queue; call frequently from loop(). Rebroadcasts on each EVENT_TX.
    void poll() {
        if (!opened_) return;
        uint8_t chan = 0, event = 0;
        uint8_t msg[MESG_BUFFER_SIZE];
        while (sd_ant_event_get(&chan, &event, msg) == NRF_SUCCESS) {
            ++eventCount_;
            lastEvent_ = event;
            if (chan != kChannel) continue;
            if (event == EVENT_TX) {
                ++txCount_;
                broadcastNext();
            } else if (event == EVENT_RX) {
                ++rxCount_;
                // Standard data message: [size][id][chan][payload 0..7][checksum] → the ANT page byte
                // is at msg[3]. (Layout verify-on-hardware; the RX/cal handshake is secondary to the
                // broadcast bring-up milestone.)
                if (msg[3] == PAGE_CALIBRATION && msg[4] == CAL_ID_MANUAL_ZERO_REQUEST)
                    sched_.queueCalibration();
            }
        }
    }

    bool opened() const { return opened_; }

    // Diagnostics (surfaced over the USB-serial `ANT` command) — so a headless bench can tell
    // channel-open failure (beginErr) from a channel that opened but never fires (txCount stuck).
    uint32_t beginErr() const { return beginErr_; }
    uint8_t  beginStep() const { return beginStep_; }   // 0 = all passed; 1..6 = the step that failed
    uint32_t eventCount() const { return eventCount_; }
    uint32_t txCount() const { return txCount_; }
    uint32_t rxCount() const { return rxCount_; }
    uint8_t  lastEvent() const { return lastEvent_; }
    uint32_t lastTxErr() const { return lastTxErr_; }

private:
    void broadcastNext() {
        uint8_t page[8];
        sched_.nextPage(watts_, cadence_, balance_, page);
        lastTxErr_ = sd_ant_broadcast_message_tx(kChannel, ANT_STANDARD_DATA_PAYLOAD_SIZE, page);
    }

    AntMasterScheduler sched_;
    int watts_ = 0, cadence_ = -1, balance_ = -1;
    bool opened_ = false;
    uint32_t beginErr_ = 0xFFFFFFFF;  // 0xFFFFFFFF = begin() never ran; 0 = success
    uint8_t  beginStep_ = 0;
    uint32_t eventCount_ = 0, txCount_ = 0, rxCount_ = 0, lastTxErr_ = 0;
    uint8_t  lastEvent_ = 0;
};

}  // namespace nrfant
#endif  // S340 && ant_network_key.h
