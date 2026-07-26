#include "ble/BleCrankPeripheral.h"

#include <NimBLEDevice.h>

#include <string>
#include <vector>

#include "Config.h"
#include "Cps.h"
#include "spoofs/StagesSpm2.h"  // the captured Stages crank bytes this peripheral impersonates
#include "Obc.h"           // OBC BLE service/characteristic UUIDs
#include "LogBuffer.h"     // toHex
#include "net/DebugLog.h"  // logf -> /log (learn the SB20's interactive protocol by observation)

using namespace sb20proxy;

// Cycling Power Control Point: answer EVERY write with an indication — the SB20 TERMINATES the link
// if a procedure goes unanswered (bike-session 2: disconnect reason=531). handleControlPoint (pure,
// host-tested) builds the reply: simple offset for 0x0C, the spec-correct ENHANCED reply (offset +
// mfg company id) for the 0x10 the Stages app actually sends (bike-session 3: the old 0x0C-shaped 0x10
// reply left the calibrate UI spinning), set/return crank length (0x04/0x05), "not supported" for the
// rest. Every write is logged raw first — that's how we capture the SB20's handshake (un-sniffable otherwise).
class ControlPointCallbacks : public NimBLECharacteristicCallbacks {
 public:
    ControlPointCallbacks(uint16_t* crankLenHalfMm, std::function<void()>* onZeroReset)
        : crankLen_(crankLenHalfMm), onZeroReset_(onZeroReset) {}
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*info*/) override {
        NimBLEAttValue v = c->getValue();
        if (v.size() == 0) return;
        logf("[cp] write %s", toHex(v.data(), v.size()).c_str());
        // mfg-specific data from the real crank's captured 0x10 reply (session 8 G1) — replayed so our
        // Enhanced Offset Compensation reply is byte-identical to the Stages SPM2 crank's.
        static const std::vector<uint8_t> mfgData(
            Config::SPOOF_MFG_DATA, Config::SPOOF_MFG_DATA + sizeof(Config::SPOOF_MFG_DATA));
        CpResult r = handleControlPoint(v.data(), v.size(), *crankLen_,
                                        (int16_t)Config::SPOOF_CAL_OFFSET,
                                        Config::SPOOF_MFG_COMPANY_ID, mfgData);
        if (r.crankLengthChanged) {
            *crankLen_ = r.crankLengthHalfMm;
            logf("[cp] crank length set = %u (1/2 mm)", (unsigned)*crankLen_);
        }
        // Answer the SB20 FIRST (it drops an unanswered CP write — reason 531), THEN fire-and-forget a
        // REAL zero to the source meter on an offset-comp/zero-reset (0x0C/0x10). The handler only flags
        // work for loop() — never a re-entrant central BLE op from this NimBLE host-task callback.
        c->setValue(r.response.data(), r.response.size());
        c->indicate();
        if (r.requestSourceZero && onZeroReset_ && *onZeroReset_) {
            logf("[cp] offset-comp -> forwarding zero to source meter");
            (*onZeroReset_)();
        }
    }

 private:
    uint16_t* crankLen_;
    std::function<void()>* onZeroReset_;
};

// The Stages proprietary control char (fe02) — opaque protocol. We don't yet know what the SB20
// writes here (if anything); log it raw so tomorrow's session captures it for us to decode.
class PropWriteCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*info*/) override {
        NimBLEAttValue v = c->getValue();
        logf("[prop fe02] write %s", toHex(v.data(), v.size()).c_str());
    }
};

// Log connect/disconnect to study the SB20's bonding + reconnection behaviour (does it reconnect
// cleanly after a drop, does it bond, what disconnect reasons appear).
class CrankServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* /*s*/, NimBLEConnInfo& info) override {
        logf("[srv] connect from %s", info.getAddress().toString().c_str());
    }
    void onDisconnect(NimBLEServer* /*s*/, NimBLEConnInfo& /*info*/, int reason) override {
        logf("[srv] disconnect reason=%d", reason);
    }
};

void BleCrankPeripheral::begin() {
    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(new CrankServerCallbacks());
    // Re-advertise after a disconnect so the SB20 reconnects without an ESP reboot (the NimBLE
    // default is m_advertiseOnDisconnect=false, which left the SB20 stuck "searching" in session 2).
    server->advertiseOnDisconnect(true);

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
    cp->setCallbacks(new ControlPointCallbacks(&crankLengthHalfMm_, &onZeroReset_));
    cps->start();

    const bool corrector = (mode_ == ProxyMode::Corrector);

    // --- Device Information Service. SPOOF presents as the real Stages SPM2; CORRECTOR presents our
    //     own honest identity (a head unit / Garmin accepts any CPS meter, so we don't impersonate). ---
    NimBLEService* dis = server->createService(UUID_DIS);
    dis->createCharacteristic(UUID_DIS_MANUF, NIMBLE_PROPERTY::READ)
        ->setValue(std::string(corrector ? Config::CORRECTOR_MANUFACTURER : Config::SPOOF_MANUFACTURER));
    dis->createCharacteristic(UUID_DIS_MODEL, NIMBLE_PROPERTY::READ)
        ->setValue(std::string(corrector ? Config::CORRECTOR_MODEL : Config::SPOOF_MODEL));
    dis->createCharacteristic(UUID_DIS_FW, NIMBLE_PROPERTY::READ)
        ->setValue(std::string(Config::SPOOF_FW));
    dis->createCharacteristic(UUID_DIS_SERIAL, NIMBLE_PROPERTY::READ)
        ->setValue(spoofSerial_);  // runtime serial; defaults to Config
    dis->start();

    // --- Stages proprietary service: SPOOF only. The real crank advertises + exposes this and the
    //     SB20 likely checks for it to confirm a genuine Stages; a CORRECTOR meter must NOT pretend
    //     to be a Stages, so it omits this service entirely. Contents opaque — presence is the point. ---
    if (!corrector) {
        NimBLEService* stages = server->createService(Config::STAGES_SVC);
        stages->createCharacteristic(Config::STAGES_CHAR_CTRL,
                                     NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::WRITE)
            ->setCallbacks(new PropWriteCallbacks());
        stages->createCharacteristic(Config::STAGES_CHAR_DATA, NIMBLE_PROPERTY::NOTIFY);
        stages->start();
    }

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
    // OpenBikeControl (OBC) service — a Button-State notify char so our re-presented SB20 buttons drive
    // OBC-speaking apps over BLE (lib/proxy/Obc.h). Gated on config; discoverable on connect.
    if (obcEnabled_ || obcDevmode_) {
        NimBLEService* obc = server->createService(OBC_BLE_SERVICE_UUID);
        obcButtonChar_ = obc->createCharacteristic(
            OBC_BLE_BUTTON_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
        obc->start();
    }

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    // Devmode advertises as an OBC controller so an OBC listener finds us by an "OBC-"-prefixed name
    // (the crank identity would otherwise read as "Stages …"); normal builds keep the runtime identity.
    adv->setName(obcDevmode_ ? "OBC-SB20" : spoofName_.c_str());
    adv->addServiceUUID(UUID_CPS);
    if (!corrector) {
        // SPOOF: the 128-bit Stages proprietary UUID rides in the scan response (mirrors the real
        // crank's capture: name+1818 primary, d445fe01 in the scan response). CORRECTOR omits it —
        // a plain CPS advert with just our name + 0x1818, which is all a Garmin needs.
        NimBLEAdvertisementData scanResp;
        scanResp.addServiceUUID(Config::STAGES_SVC);
        adv->enableScanResponse(true);
        adv->setScanResponseData(scanResp);
    }
    adv->start();
}

void BleCrankPeripheral::notifyObc(const uint8_t* data, size_t len) {
    if (obcButtonChar_ == nullptr || data == nullptr || len == 0) return;
    obcButtonChar_->setValue(data, len);
    obcButtonChar_->notify();
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

    // Pedal balance: forward the source meter's REAL left-referenced L/R split (the Assioma DUO
    // reports it via CPS bit0) so the SB20 / Stages app shows the genuine balance. Fall back to
    // 50 % (raw 100) when the source carries no split (single-sided meter / mock). balance_half_pct
    // is the left pedal's 1/2-% value, exactly what the Stages 0x2F balance byte expects.
    const uint8_t balanceOut =
        (r.balance_half_pct >= 0) ? (uint8_t)r.balance_half_pct : (uint8_t)100;
    std::vector<uint8_t> frame = encodeStagesCpsMeasurement(
        r.power_w, balanceOut, accumTorque_, cadence_.cumulativeRevs,
        cadence_.lastEventTime);
    meas_->setValue(frame.data(), frame.size());
    meas_->notify();
}
