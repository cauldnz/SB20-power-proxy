#include "ble/FtmsTrainerServer.h"

#include <NimBLEDevice.h>

#include <vector>

#include "Ftms.h"  // pure (no NimBLE): the FTMS codec

using namespace sb20proxy;

// The FTMS Control Point: answer every write with an indication (Request Control / Start /
// Set Target Power / Reset). On Set Target Power we store the target and also emit a
// Fitness Machine Status "Target Power Changed" notification — exactly what a controller
// (Zwift, or our FtmsErgClient) expects. Mirrors the pure handler in Ftms.h/ftms_erg.py.
class FtmsControlPointCallbacks : public NimBLECharacteristicCallbacks {
 public:
    explicit FtmsControlPointCallbacks(FtmsTrainerServer* srv) : srv_(srv) {}
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*info*/) override {
        NimBLEAttValue v = c->getValue();
        if (v.size() == 0) return;
        FtmsCpMessage m = decodeControlPoint(v.data(), v.size());
        uint8_t result = FTMS_CP_SUCCESS;
        const uint8_t op = m.opcode;

        if (op == FTMS_CP_REQUEST_CONTROL) {
            srv_->controlled_ = true;
        } else if (!srv_->controlled_) {
            result = FTMS_CP_CONTROL_NOT_PERMITTED;
        } else if (op == FTMS_CP_START_RESUME) {
            srv_->started_ = true;
        } else if (op == FTMS_CP_SET_TARGET_POWER && m.hasTargetPower) {
            srv_->target_ = m.targetPower;
            srv_->hasTarget_ = true;
            if (srv_->statusChar_) {  // FTMS Status: Target Power Changed -> watts
                std::vector<uint8_t> st = {FTMS_ST_TARGET_POWER_CHANGED,
                                           (uint8_t)(srv_->target_ & 0xFF),
                                           (uint8_t)((srv_->target_ >> 8) & 0xFF)};
                srv_->statusChar_->setValue(st.data(), st.size());
                srv_->statusChar_->notify();
            }
        } else if (op == FTMS_CP_RESET) {
            srv_->controlled_ = false;
            srv_->started_ = false;
        } else {
            result = FTMS_CP_OP_NOT_SUPPORTED;
        }

        std::vector<uint8_t> resp = encodeControlPointResponse(op, result);
        c->setValue(resp.data(), resp.size());
        c->indicate();
    }

 private:
    FtmsTrainerServer* srv_;
};

void FtmsTrainerServer::begin(const char* deviceName) {
    NimBLEServer* server = NimBLEDevice::createServer();
    server->advertiseOnDisconnect(true);

    NimBLEService* ftms = server->createService(UUID_FTMS);

    ibd_ = ftms->createCharacteristic(UUID_INDOOR_BIKE_DATA, NIMBLE_PROPERTY::NOTIFY);
    statusChar_ = ftms->createCharacteristic(UUID_FTMS_STATUS, NIMBLE_PROPERTY::NOTIFY);

    // Feature: cadence + power measurement; target setting: power (erg capability).
    NimBLECharacteristic* feat = ftms->createCharacteristic(UUID_FTMS_FEATURE, NIMBLE_PROPERTY::READ);
    std::vector<uint8_t> fv = encodeFitnessMachineFeature(
        FTMS_FEAT_CADENCE | FTMS_FEAT_POWER_MEAS, FTMS_TGT_POWER);
    feat->setValue(fv.data(), fv.size());

    NimBLECharacteristic* range =
        ftms->createCharacteristic(UUID_SUPPORTED_POWER_RANGE, NIMBLE_PROPERTY::READ);
    std::vector<uint8_t> rv = encodeSupportedPowerRange(0, 1000, 1);
    range->setValue(rv.data(), rv.size());

    NimBLECharacteristic* cp = ftms->createCharacteristic(
        UUID_FTMS_CONTROL_POINT, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE);
    cp->setCallbacks(new FtmsControlPointCallbacks(this));

    ftms->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setName(deviceName);
    adv->addServiceUUID(UUID_FTMS);
    adv->start();
}

void FtmsTrainerServer::publishPower(int16_t power_w, float cadenceRpm) {
    if (!ibd_) return;
    std::vector<uint8_t> frame = encodeIndoorBikeData(power_w, cadenceRpm);
    ibd_->setValue(frame.data(), frame.size());
    ibd_->notify();
}
