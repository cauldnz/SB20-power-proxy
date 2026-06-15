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
    // Crank Revolution Data Supported (for cadence). TODO: the full Stages feature set (Session G).
    uint32_t features = CP_FEATURE_CRANK_REV_SUPPORTED;
    feat->setValue((uint8_t*)&features, sizeof(features));

    NimBLECharacteristic* loc = cps->createCharacteristic(UUID_CP_SENSORLOC, NIMBLE_PROPERTY::READ);
    uint8_t location = 5;  // 5 = left crank
    loc->setValue(&location, 1);

    NimBLECharacteristic* cp = cps->createCharacteristic(
        UUID_CP_CONTROL, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE);
    cp->setCallbacks(new ControlPointCallbacks());
    cps->start();

    // --- Device Information Service (present as Stages) ---
    NimBLEService* dis = server->createService(UUID_DIS);
    dis->createCharacteristic(UUID_DIS_MANUF, NIMBLE_PROPERTY::READ)
        ->setValue(std::string(Config::SPOOF_MANUFACTURER));
    dis->createCharacteristic(UUID_DIS_SERIAL, NIMBLE_PROPERTY::READ)
        ->setValue(std::string(Config::SPOOF_SERIAL));
    dis->start();

    // --- advertise as the crank ---
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(UUID_CPS);
    adv->setName(Config::SPOOF_NAME);
    adv->start();
}

void BleCrankPeripheral::publishPower(const PowerReading& r) {
    if (!meas_) return;
    std::vector<uint8_t> frame;
    if (r.cadence_rpm >= 0) {
        // Advance the crank-revolution state by the time since the last reading and emit
        // power + cadence. dt is 0 on the first reading (no event yet).
        uint32_t dt = haveLastT_ ? (r.t_ms - lastT_) : 0;
        lastT_ = r.t_ms;
        haveLastT_ = true;
        cadence_.advance((float)r.cadence_rpm, dt);
        frame = encodeCpsMeasurement(r.power_w, cadence_.cumulativeRevs, cadence_.lastEventTime);
    } else {
        frame = encodeCpsMeasurement(r.power_w);  // power-only when cadence is unknown
    }
    meas_->setValue(frame.data(), frame.size());
    meas_->notify();
}
