#include "ble/BleMeterClient.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

#include "Config.h"
#include "Cps.h"

using namespace sb20proxy;

// A connected meter notifies continuously (~1 Hz even at zero power), so a gap this long means
// it's gone — but an abruptly-vanished peer (e.g. a killed host process) can leave the link up
// at the controller with no notifications, so we force a disconnect to recover and rescan.
static constexpr uint32_t kMeterStaleMs = 6000;

static BleMeterClient* g_meter = nullptr;

static void powerNotifyCB(NimBLERemoteCharacteristic* /*c*/, uint8_t* data, size_t len,
                          bool /*isNotify*/) {
    if (g_meter) g_meter->onMeasurement(data, len);
}

// Resume scanning when the meter link drops, so unplug/replug (or a flaky meter) recovers.
class MeterClientCallbacks : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient* /*c*/, int /*reason*/) override {
        if (g_meter) g_meter->onDisconnected();
    }
};
static MeterClientCallbacks g_clientCb;

class MeterScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* d) override {
        if (!g_meter) return;
        std::string name = d->getName();
        // Never read FROM a copy of the crank we impersonate — another proxy, a sibling test
        // board, or the real Stages crank we're replacing all advertise SPOOF_NAME + CPS, and
        // latching onto one would form a loop. The meter we want is the Assioma, not a "Stages".
        if (name == Config::SPOOF_NAME) return;
        // CPS service UUID first (a Windows winrt/bless peripheral advertises the UUID but no
        // name); fall back to the name substring (a real Assioma advertises both).
        bool match = d->isAdvertisingService(NimBLEUUID(UUID_CPS));
        if (!match) {
            match = !name.empty() && name.find(Config::METER_NAME_FILTER) != std::string::npos;
        }
        if (match) {
            NimBLEDevice::getScan()->stop();
            g_meter->onFound(d->getAddress().toString().c_str(), d->getAddress().getType());
        }
    }
};
static MeterScanCallbacks g_scanCb;

void BleMeterClient::onFound(const char* addr, uint8_t addrType) {
    strncpy(addr_, addr, sizeof(addr_) - 1);
    addrType_ = addrType;
    haveTarget_ = true;  // connect from loop(), off the scan-callback context
}

void BleMeterClient::onMeasurement(const uint8_t* data, size_t len) {
    if (!cb_) return;
    PowerReading r;
    r.power_w = decodeCpsPower(data, len);  // sint16 at bytes 2-3, regardless of flags
    r.t_ms = millis();

    // Recover cadence from Crank Revolution Data, the way a head unit does — but only when the
    // crank fields are at the fixed 4-7 offset (no balance/torque/wheel field precedes them,
    // which holds for our meters' power+cadence frame). Otherwise leave cadence unknown.
    const uint16_t flags = decodeCpsFlags(data, len);
    if ((flags & CPM_CRANK_REV_DATA_PRESENT) && !(flags & CPM_PRECEDING_DATA_BITS) && len >= 8) {
        const uint16_t revs = decodeCrankRevs(data, len);
        const uint16_t evt = decodeCrankEventTime(data, len);
        if (havePrevCrank_) {
            const float rpm = cadenceRpmFromCrank(prevRevs_, prevEventTime_, revs, evt);
            if (rpm > 0.0f) r.cadence_rpm = (int16_t)(rpm + 0.5f);
        }
        prevRevs_ = revs;
        prevEventTime_ = evt;
        havePrevCrank_ = true;
    }
    lastReadingMs_ = r.t_ms;  // feed the staleness watchdog
    cb_(r);
}

void BleMeterClient::onDisconnected() {
    connected_ = false;
    haveTarget_ = false;
    havePrevCrank_ = false;  // don't carry crank deltas across a reconnect
    lastReadingMs_ = 0;
    wantRescan_ = true;  // restart the scan from loop() (off the callback context)
}

void BleMeterClient::begin() {
    g_meter = this;
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&g_scanCb, false);
    scan->setActiveScan(true);  // also harvest scan responses (where WinRT puts the service UUID)
    scan->start(0, false);      // scan continuously
}

void BleMeterClient::loop() {
    if (wantRescan_) {
        wantRescan_ = false;
        NimBLEDevice::getScan()->start(0, false);
    }
    // Staleness watchdog: a connected meter that stops notifying is gone — drop the link and
    // rescan. We reset state HERE rather than waiting on the onDisconnect callback, because an
    // abruptly-vanished peer (a killed host process) can leave the controller link up with the
    // callback never firing — which would otherwise wedge us "connected" to a dead meter forever.
    if (connected_ && lastReadingMs_ != 0 && (millis() - lastReadingMs_) > kMeterStaleMs) {
        if (client_) client_->disconnect();
        onDisconnected();  // connected_=false, rescan — independent of the disconnect callback
        return;
    }
    if (!haveTarget_ || connected_) return;

    if (!client_) {
        client_ = NimBLEDevice::createClient();
        client_->setClientCallbacks(&g_clientCb, false);
    }
    if (client_->connect(NimBLEAddress(std::string(addr_), addrType_))) {
        NimBLERemoteService* svc = client_->getService(UUID_CPS);
        if (svc) {
            NimBLERemoteCharacteristic* ch = svc->getCharacteristic(UUID_CP_MEAS);
            if (ch && ch->canNotify()) {
                ch->subscribe(true, powerNotifyCB);
                connected_ = true;
            }
        }
    }
    if (!connected_) {
        client_->disconnect();
        haveTarget_ = false;
        NimBLEDevice::getScan()->start(0, false);  // resume scanning
    }
}
