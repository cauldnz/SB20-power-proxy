#include "ble/BleCrankPeripheral.h"

#include <NimBLEDevice.h>

#include <string>
#include <vector>

#include "Config.h"
#include "CpApply.h"  // applyCpResult — the mandated reply-then-zero order, host-tested
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
//
// This class is the ICpSink (CpApply.h): applyCpResult drives it in the order the SB20 demands —
// persist crank length, THEN indicate the reply, THEN forward the zero. That order used to be three
// adjacent statements here where no test could see it; it is now pinned by test_loopdrain.
class ControlPointCallbacks : public NimBLECharacteristicCallbacks, public ICpSink {
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
        // Order is protocol, not style — see CpApply.h. Answer the SB20 FIRST (it drops an
        // unanswered CP write, reason 531), THEN fire-and-forget a REAL zero to the source meter.
        cp_ = c;
        applyCpResult(r, *this);
        cp_ = nullptr;
    }

    // --- ICpSink -------------------------------------------------------------------------------
    void setCrankLength(uint16_t halfMm) override {
        *crankLen_ = halfMm;
        logf("[cp] crank length set = %u (1/2 mm)", (unsigned)halfMm);
    }

    void reply(const uint8_t* data, size_t len) override {
        if (!cp_) return;
        cp_->setValue(data, len);
        cp_->indicate();
    }

    void requestSourceZero() override {
        // The handler only flags work for loop() — never a re-entrant central BLE op from this
        // NimBLE host-task callback.
        if (onZeroReset_ && *onZeroReset_) {
            logf("[cp] offset-comp -> forwarding zero to source meter");
            (*onZeroReset_)();
        }
    }

 private:
    uint16_t* crankLen_;
    std::function<void()>* onZeroReset_;
    NimBLECharacteristic* cp_ = nullptr;  // valid only for the duration of one onWrite
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
// A disconnect also empties that link's OBC App Information (BLE.md: the written value is emptied on
// every disconnect) — only that link's, so the SB20 dropping does not forget a connected OBC app.
class CrankServerCallbacks : public NimBLEServerCallbacks {
 public:
    explicit CrankServerCallbacks(ObcAppState* obcApp) : obcApp_(obcApp) {}
    void onConnect(NimBLEServer* /*s*/, NimBLEConnInfo& info) override {
        logf("[srv] connect from %s", info.getAddress().toString().c_str());
    }
    void onDisconnect(NimBLEServer* /*s*/, NimBLEConnInfo& info, int reason) override {
        logf("[srv] disconnect reason=%d", reason);
        obcApp_->onDisconnect((int)info.getConnHandle());
    }

 private:
    ObcAppState* obcApp_;
};

// OBC Haptic (d273f682) + App Information (d273f683) writes: one write is one message, decoded by the
// pure ObcAppState. Haptic is accepted and logged (no motor — BLE.md says accept, do nothing); the
// latest AppInfo is kept per link. Runs on the NimBLE host task, the only context touching obcApp.
class ObcWriteCallbacks : public NimBLECharacteristicCallbacks {
 public:
    explicit ObcWriteCallbacks(ObcAppState* obcApp) : obcApp_(obcApp) {}
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        NimBLEAttValue v = c->getValue();
        const ObcAppMsg kind = obcApp_->onMessage((int)info.getConnHandle(), v.data(), v.size());
        if (kind == ObcAppMsg::AppInfo) {
            const ObcAppInfo& a = obcApp_->appInfo();
            logf("[obc] ble app '%s' %s (%u ids%s)", a.appId, a.appVersion, (unsigned)a.buttonCount,
                 a.allButtons() ? " = all" : "");
        } else if (kind == ObcAppMsg::Haptic) {
            logf("[obc] ble haptic pattern=%u (no motor: accepted)", (unsigned)obcApp_->lastHaptic().pattern);
        } else {
            logf("[obc] ble write ignored %s", toHex(v.data(), v.size()).c_str());
        }
    }

 private:
    ObcAppState* obcApp_;
};

// OBC Button-State subscribe: tell that consumer we are here and ready — DeviceStatus
// [0x02, 0xFF (not battery-powered), 0x01 (connected)], sent to the subscribing link only.
class ObcButtonCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* c, NimBLEConnInfo& info, uint16_t subValue) override {
        if ((subValue & 0x0001) == 0) return;  // notifications off
        uint8_t st[3];
        const size_t n = encodeDeviceStatus(OBC_BATTERY_NA, true, st, sizeof(st));
        c->notify(st, n, info.getConnHandle());
        logf("[obc] ble subscribe from %s -> status %s", info.getAddress().toString().c_str(),
             toHex(st, n).c_str());
    }
};

void BleCrankPeripheral::begin() {
    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(new CrankServerCallbacks(&obcApp_));
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
    // OpenBikeControl (OBC) service (BLE.md): the Button-State notify char so our re-presented SB20
    // buttons drive OBC-speaking apps over BLE, plus the Haptic and App Information write chars the spec
    // defines (lib/proxy/Obc.h, ObcApp.h). Gated on config. It is created LAST, so the handles of the
    // crank's services above are the same with or without it (nothing the SB20 cached moves).
    if (obcEnabled_ || obcDevmode_) {
        NimBLEService* obc = server->createService(OBC_BLE_SERVICE_UUID);
        obcButtonChar_ = obc->createCharacteristic(
            OBC_BLE_BUTTON_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
        obcButtonChar_->setCallbacks(new ObcButtonCallbacks());
        ObcWriteCallbacks* obcWrites = new ObcWriteCallbacks(&obcApp_);
        obc->createCharacteristic(OBC_BLE_HAPTIC_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR)
            ->setCallbacks(obcWrites);
        obc->createCharacteristic(OBC_BLE_APPINFO_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR)
            ->setCallbacks(obcWrites);
        obc->start();
    }

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    // Devmode advertises as an OBC controller (this board's OBC name, not the crank's "Stages …"), with
    // the OBC service UUID in the scan response so a UUID-matching listener (the spec's reference apps,
    // upstream qz) finds it; the "OBC-" name still serves our qz fork's name matcher. Every other mode
    // keeps its advert bytes exactly as before: the OBC UUID next to the crank spoof belongs on the full
    // proxy's second advertising set (sb20-full-proxy.md §3d, #291), not in the crank's own packets.
    adv->setName(obcDevmode_ ? obcName_.c_str() : spoofName_.c_str());
    adv->addServiceUUID(UUID_CPS);
    if (obcDevmode_) {
        NimBLEAdvertisementData scanResp;
        scanResp.addServiceUUID(OBC_BLE_SERVICE_UUID);
        adv->enableScanResponse(true);
        adv->setScanResponseData(scanResp);
    } else if (!corrector) {
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
