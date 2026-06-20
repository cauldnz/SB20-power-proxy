#include "ble/FtmsErgClient.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <string>
#include <vector>

#include "Ftms.h"

using namespace sb20proxy;

static FtmsErgClient* g_ergClient = nullptr;
static NimBLERemoteCharacteristic* g_cp = nullptr;  // the connected control point

// Control-point indications (the machine's 0x80 responses) -> the client state machine.
static void cpIndicateCB(NimBLERemoteCharacteristic* /*c*/, uint8_t* data, size_t len,
                         bool /*isNotify*/) {
    if (g_ergClient) g_ergClient->onIndication(data, len);
}

class ErgClientCallbacks : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient* /*c*/, int /*reason*/) override {
        if (g_ergClient) g_ergClient->onDisconnected();
    }
};
static ErgClientCallbacks g_clientCb;

// Scan: match an FTMS machine by advertised service 0x1826 (name optional).
class ErgScanCallbacks : public NimBLEScanCallbacks {
 public:
    explicit ErgScanCallbacks(const char* wantName) : wantName_(wantName) {}
    void onResult(const NimBLEAdvertisedDevice* d) override {
        if (!d->isAdvertisingService(NimBLEUUID(UUID_FTMS))) return;
        if (wantName_ && wantName_[0] != '\0') {
            std::string n = d->getName();
            if (n.find(wantName_) == std::string::npos) return;
        }
        NimBLEDevice::getScan()->stop();
        if (g_ergClient) {
            g_ergClient->onFound(d->getAddress().toString().c_str(),
                                 d->getAddress().getType());
        }
    }

 private:
    const char* wantName_;
};
static ErgScanCallbacks* g_scanCb = nullptr;

void FtmsErgClient::begin(const char* targetName) {
    g_ergClient = this;
    g_scanCb = new ErgScanCallbacks(targetName);
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(g_scanCb, false);
    scan->start(0, false);  // scan continuously
}

void FtmsErgClient::onFound(const char* addr, uint8_t addrType) {
    strncpy(addr_, addr, sizeof(addr_) - 1);
    addrType_ = addrType;
    haveTarget_ = true;
}

void FtmsErgClient::onDisconnected() {
    connected_ = false;
    controlled_ = false;
    started_ = false;
    haveSent_ = false;
    g_cp = nullptr;
}

void FtmsErgClient::onIndication(const uint8_t* data, size_t len) {
    FtmsCpMessage m = decodeControlPoint(data, len);
    if (!m.isResponse || m.result != FTMS_CP_SUCCESS) return;
    if (m.requestOpcode == FTMS_CP_REQUEST_CONTROL) controlled_ = true;
    else if (m.requestOpcode == FTMS_CP_START_RESUME) started_ = true;
    else if (m.requestOpcode == FTMS_CP_SET_TARGET_POWER) { lastSent_ = desired_; haveSent_ = true; }
}

void FtmsErgClient::connectAndSetup() {
    if (!client_) {
        client_ = NimBLEDevice::createClient();
        client_->setClientCallbacks(&g_clientCb, false);
    }
    if (!client_->connect(NimBLEAddress(std::string(addr_), addrType_))) {
        haveTarget_ = false;
        NimBLEDevice::getScan()->start(0, false);
        return;
    }
    NimBLERemoteService* svc = client_->getService(UUID_FTMS);
    if (!svc) {
        client_->disconnect();
        return;
    }
    // read the supported power range so we clamp to the machine's limits
    NimBLERemoteCharacteristic* rangeCh = svc->getCharacteristic(UUID_SUPPORTED_POWER_RANGE);
    if (rangeCh && rangeCh->canRead()) {
        NimBLEAttValue rv = rangeCh->readValue();
        if (rv.size() >= 6) range_ = decodeSupportedPowerRange(rv.data(), rv.size());
    }
    NimBLERemoteCharacteristic* cp = svc->getCharacteristic(UUID_FTMS_CONTROL_POINT);
    if (!cp) {
        client_->disconnect();
        return;
    }
    cp->subscribe(false, cpIndicateCB);  // false = indications (not notifications)
    g_cp = cp;
    connected_ = true;
}

void FtmsErgClient::step() {
    if (!g_cp) return;
    std::vector<uint8_t> cmd;
    if (!controlled_) {
        cmd = encodeRequestControl();
    } else if (!started_) {
        cmd = encodeStart();
    } else {
        int16_t want = range_.clamp(desired_);
        if (!haveSent_ || want != lastSent_) {
            cmd = encodeSetTargetPower(want);
        }
    }
    if (!cmd.empty()) g_cp->writeValue(cmd.data(), cmd.size(), true);
}

void FtmsErgClient::loop() {
    if (!connected_) {
        if (haveTarget_) connectAndSetup();
        return;
    }
    // rate-limited convergence: one control-point write per ~400 ms
    uint32_t now = millis();
    if (now - lastStepMs_ >= 400) {
        lastStepMs_ = now;
        step();
    }
}
