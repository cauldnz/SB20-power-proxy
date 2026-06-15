#include "ble/BleMeterClient.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

#include "Config.h"
#include "Cps.h"

using namespace sb20proxy;

static BleMeterClient* g_meter = nullptr;

static void powerNotifyCB(NimBLERemoteCharacteristic* /*c*/, uint8_t* data, size_t len,
                          bool /*isNotify*/) {
    if (g_meter) g_meter->onPower(decodeCpsPower(data, len));
}

class MeterScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* d) override {
        if (!g_meter) return;
        if (d->getName().find(Config::METER_NAME_FILTER) != std::string::npos) {
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

void BleMeterClient::onPower(int16_t power_w) {
    if (!cb_) return;
    PowerReading r;
    r.power_w = power_w;
    r.t_ms = millis();
    cb_(r);
}

void BleMeterClient::begin() {
    g_meter = this;
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&g_scanCb, false);
    scan->setActiveScan(true);
    scan->start(0, false);  // scan continuously
}

void BleMeterClient::loop() {
    if (!haveTarget_ || connected_) return;

    NimBLEClient* client = NimBLEDevice::createClient();
    if (client->connect(NimBLEAddress(std::string(addr_), addrType_))) {
        NimBLERemoteService* svc = client->getService(UUID_CPS);
        if (svc) {
            NimBLERemoteCharacteristic* ch = svc->getCharacteristic(UUID_CP_MEAS);
            if (ch && ch->canNotify()) {
                ch->subscribe(true, powerNotifyCB);
                connected_ = true;
            }
        }
    }
    if (!connected_) {
        NimBLEDevice::deleteClient(client);
        haveTarget_ = false;
        NimBLEDevice::getScan()->start(0, false);  // resume scanning
    }
}
