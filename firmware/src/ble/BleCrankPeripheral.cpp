#include "ble/BleCrankPeripheral.h"

#include <NimBLEDevice.h>

#include <string>
#include <vector>

#include "Config.h"
#include "Cps.h"

using namespace sb20proxy;

// Cycling Power Control Point: answer a Start Offset Compensation (zero-reset) with
// success + the captured offset — the BLE analogue of the ANT+ 0x01 0xAC reply.
class ControlPointCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*info*/) override {
        NimBLEAttValue v = c->getValue();
        if (v.size() == 0) return;
        uint8_t op = v[0];
        std::vector<uint8_t> resp;
        if (op == CP_OP_START_OFFSET_COMP) {
            resp = encodeCalibrationResponse(Config::SPOOF_CAL_OFFSET);
        } else {
            resp = {CP_OP_RESPONSE, op, CP_RESPONSE_NOT_SUPPORTED};
        }
        c->setValue(resp.data(), resp.size());
        c->indicate();
    }
};

void BleCrankPeripheral::begin() {
    NimBLEServer* server = NimBLEDevice::createServer();

    // --- Cycling Power Service ---
    NimBLEService* cps = server->createService(UUID_CPS);
    meas_ = cps->createCharacteristic(UUID_CP_MEAS, NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic* feat = cps->createCharacteristic(UUID_CP_FEATURE, NIMBLE_PROPERTY::READ);
    uint32_t features = CP_FEATURE_STAGES;  // 0x0008030B — exactly what the real crank reports
    feat->setValue((uint8_t*)&features, sizeof(features));

    NimBLECharacteristic* loc = cps->createCharacteristic(UUID_CP_SENSORLOC, NIMBLE_PROPERTY::READ);
    uint8_t location = SENSOR_LOCATION_OTHER;  // the real crank reports 0 ("other"), not 5 (left)
    loc->setValue(&location, 1);

    NimBLECharacteristic* cp = cps->createCharacteristic(
        UUID_CP_CONTROL, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE);
    cp->setCallbacks(new ControlPointCallbacks());
    cps->start();

    // --- Device Information Service (present as the real Stages SPM2) ---
    NimBLEService* dis = server->createService(UUID_DIS);
    dis->createCharacteristic(UUID_DIS_MANUF, NIMBLE_PROPERTY::READ)
        ->setValue(std::string(Config::SPOOF_MANUFACTURER));
    dis->createCharacteristic(UUID_DIS_MODEL, NIMBLE_PROPERTY::READ)
        ->setValue(std::string(Config::SPOOF_MODEL));
    dis->createCharacteristic(UUID_DIS_FW, NIMBLE_PROPERTY::READ)
        ->setValue(std::string(Config::SPOOF_FW));
    dis->createCharacteristic(UUID_DIS_SERIAL, NIMBLE_PROPERTY::READ)
        ->setValue(std::string(Config::SPOOF_SERIAL));
    dis->start();

    // --- Stages proprietary service (the real crank advertises + exposes this; the SB20 likely
    //     checks for it to confirm a genuine Stages). Contents opaque — presence is the point. ---
    NimBLEService* stages = server->createService(Config::STAGES_SVC);
    stages->createCharacteristic(Config::STAGES_CHAR_CTRL,
                                 NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::WRITE);
    stages->createCharacteristic(Config::STAGES_CHAR_DATA, NIMBLE_PROPERTY::NOTIFY);
    stages->start();

    // --- Battery Service (the real crank exposes 180F/2A19; the SB20 may read crank battery) ---
    NimBLEService* bat = server->createService("180F");
    NimBLECharacteristic* batLvl = bat->createCharacteristic("2A19", NIMBLE_PROPERTY::READ);
    uint8_t batteryPct = 90;  // healthy spoofed level
    batLvl->setValue(&batteryPct, 1);
    bat->start();

    // --- advertise as the crank, exactly like the real one: name + CPS (16-bit) in the PRIMARY
    //     advert, and the 128-bit Stages proprietary service in the SCAN RESPONSE. Putting the
    //     128-bit UUID in the primary packet crowds the name out of the 31-byte advert (the real
    //     crank's capture has name+1818 primary, d445fe01 in the scan response). ---
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setName(Config::SPOOF_NAME);
    adv->addServiceUUID(UUID_CPS);
    NimBLEAdvertisementData scanResp;
    scanResp.addServiceUUID(Config::STAGES_SVC);
    adv->enableScanResponse(true);
    adv->setScanResponseData(scanResp);
    adv->start();
}

void BleCrankPeripheral::publishPower(const PowerReading& r) {
    if (!meas_) return;

    // Advance crank-rev state (cadence) and accumulate torque per completed revolution, so the
    // frame is a faithful Stages 0x2F. When cadence is unknown the crank state just doesn't
    // advance (the SB20 sees 0 cadence) but power is still broadcast.
    if (r.cadence_rpm > 0) {
        uint32_t dt = haveLastT_ ? (r.t_ms - lastT_) : 0;
        uint16_t prevRevs = cadence_.cumulativeRevs;
        cadence_.advance((float)r.cadence_rpm, dt);
        uint16_t dRevs = (uint16_t)(cadence_.cumulativeRevs - prevRevs);
        if (dRevs > 0) {
            // Accumulated torque (1/32 Nm) per rev: T = P / (2*pi*rev_s) = P*60 / (2*pi*rpm).
            float torqueNm = (float)r.power_w * 60.0f / (6.2831853f * (float)r.cadence_rpm);
            accumTorque_ = (uint16_t)(accumTorque_ + (uint16_t)(dRevs * torqueNm * 32.0f + 0.5f));
        }
    }
    lastT_ = r.t_ms;
    haveLastT_ = true;

    // Pedal balance: real crank reports a left-referenced value; we send 50% (raw 100) as the
    // single source has no L/R split. The SB20 reads instantaneous power, not balance.
    std::vector<uint8_t> frame = encodeStagesCpsMeasurement(
        r.power_w, /*balanceHalfPct=*/100, accumTorque_, cadence_.cumulativeRevs,
        cadence_.lastEventTime);
    meas_->setValue(frame.data(), frame.size());
    meas_->notify();
}
