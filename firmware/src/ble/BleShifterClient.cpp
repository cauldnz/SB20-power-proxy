#include "ble/BleShifterClient.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

using namespace sb20proxy;

// The SB20's vendor button service + characteristic (code/findings/shifter-ble-protocol.md).
static const char* kSb20VendorSvc = "0c46be5f-9c22-48ff-ae0e-c6eae1a2f4e5";
static const char* kSb20ButtonChar = "0c46be60-9c22-48ff-ae0e-c6eae1a2f4e5";

// At most one shifter client (like the erg client); the notify + disconnect callbacks route to it.
static BleShifterClient* g_shifter = nullptr;

static void shifterNotifyCB(NimBLERemoteCharacteristic* /*c*/, uint8_t* data, size_t len,
                            bool /*isNotify*/) {
    if (g_shifter) g_shifter->onNotification(data, len);
}

class ShifterClientCallbacks : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient* /*c*/, int /*reason*/) override {
        if (g_shifter) g_shifter->onDisconnected();
    }
};
static ShifterClientCallbacks g_clientCb;

void BleShifterClient::beginShared(const char* targetName) {
    g_shifter = this;
    begun_ = true;
    strncpy(want_, targetName ? targetName : "", sizeof(want_) - 1);
    // The hub (BleMeterClient's scan) owns the callbacks; just make sure the scan is running.
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (!scan->isScanning()) scan->start(0, false);
}

void BleShifterClient::onSb20Advert(const char* addr, uint8_t addrType, const char* name) {
    if (want_[0] != '\0') {
        const std::string n = name ? name : "";
        if (n.find(want_) == std::string::npos) return;  // not the SB20
    }
    onFound(addr, addrType);
}

void BleShifterClient::onFound(const char* addr, uint8_t addrType) {
    strncpy(addr_, addr, sizeof(addr_) - 1);
    addrType_ = addrType;
    haveTarget_ = true;
}

void BleShifterClient::onDisconnected() { connected_ = false; }

void BleShifterClient::onNotification(const uint8_t* data, size_t len) {
    if (cb_) cb_(data, len);  // main feeds this into ObcShifterSource -> notifyObc
}

void BleShifterClient::connectAndSetup() {
    if (!client_) {
        client_ = NimBLEDevice::createClient();
        client_->setClientCallbacks(&g_clientCb, false);
    }
    if (!client_->connect(NimBLEAddress(std::string(addr_), addrType_))) {
        haveTarget_ = false;
        NimBLEDevice::getScan()->start(0, false);
        return;
    }
    NimBLERemoteService* svc = client_->getService(NimBLEUUID(kSb20VendorSvc));
    if (!svc) {  // the peer isn't the SB20 (no vendor service) — drop it and keep hunting
        client_->disconnect();
        haveTarget_ = false;
        return;
    }
    NimBLERemoteCharacteristic* ch = svc->getCharacteristic(NimBLEUUID(kSb20ButtonChar));
    if (!ch || !ch->canNotify()) {
        client_->disconnect();
        haveTarget_ = false;
        return;
    }
    const bool sub = ch->subscribe(true, shifterNotifyCB);  // true = notifications
    connected_ = true;
    Serial.printf("[shifter] SB20 connected ('%s'); button subscribe=%s\n", addr_,
                  sub ? "ok" : "FAILED");
}

void BleShifterClient::loop() {
    if (connected_) return;
    if (haveTarget_) {
        connectAndSetup();
    } else if (begun_) {
        // SB20 lost / not yet found and the shared scan may have been stopped once every other
        // client was satisfied — kick it back on, rate-limited (mirrors FtmsErgClient).
        const uint32_t now = millis();
        if (now - lastScanKickMs_ >= 3000) {
            lastScanKickMs_ = now;
            NimBLEScan* scan = NimBLEDevice::getScan();
            if (!scan->isScanning()) scan->start(0, false);
        }
    }
}
