#include "ble/BleMeterClient.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

#include "Config.h"
#include "Cps.h"
#include "LogBuffer.h"     // toHex
#include "MeterMatch.h"    // pure, host-tested meter-selection (which device to read)
#include "net/DebugLog.h"  // logf -> /log (learn each meter's frame format by observation)

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
        const std::string name = d->getName();
        const std::string addr = d->getAddress().toString();
        const bool cps = d->isAdvertisingService(NimBLEUUID(UUID_CPS));
        // Record every advertiser for the web picker (the scan runs continuously until a match
        // connects, so the list fills during setup). Done before the match check so the page can
        // offer sources even when none is configured yet.
        g_meter->recordCandidate(addr.c_str(), name.c_str(), d->getRSSI(), cps);
        // isTarget (pure, host-tested isTargetMeter under the hood) picks the source from the RUNTIME
        // config: a PINNED address wins; else a nameless peripheral matches by CPS UUID and a NAMED
        // device must contain the name filter — so a real "Stages NNNN" crank (also CPS-advertising)
        // is NOT grabbed (the source-bouncing bug from bike-session 2), and we never read a copy of
        // our own spoof (a loop). The bench flag MATCH_ANY_CPS loosens the named-device rule for the
        // WinRT fake_meter rig (advertises under the PC's name, not "ASSIOMA" — decisions.md 2026-06-22).
        if (!g_meter->isTarget(name, cps, addr)) {
            return;
        }
        NimBLEDevice::getScan()->stop();
        g_meter->onFound(addr.c_str(), d->getAddress().getType(), name.c_str());
    }
};
static MeterScanCallbacks g_scanCb;

bool BleMeterClient::isTarget(const std::string& name, bool cps, const std::string& addr) const {
    return isTargetMeter(name, cps, addr, matchAddr_, matchSpoofName_, matchNameFilter_,
                         Config::MATCH_ANY_CPS);
}

void BleMeterClient::recordCandidate(const char* addr, const char* name, int rssi, bool cps) {
    const std::string n = name ? name : "";
    if (n == matchSpoofName_) return;  // never offer our own spoof as a source
    SourceCandidate c;
    c.address = addr ? addr : "";
    c.name = n;
    c.rssi = rssi;
    c.isCps = cps;
    c.isStagesCrank = (n.rfind("Stages ", 0) == 0);  // a native crank (incl. the surviving-crank case)
    addCandidate(candidates_, c, 16);  // pure, host-tested dedup + cap
}

void BleMeterClient::onFound(const char* addr, uint8_t addrType, const char* name) {
    strncpy(addr_, addr, sizeof(addr_) - 1);
    strncpy(name_, name ? name : "", sizeof(name_) - 1);
    addrType_ = addrType;
    haveTarget_ = true;     // connect from loop(), off the scan-callback context
    loggedFrame_ = false;   // log this meter's first frame once connected
    logf("[meter] found '%s' %s", name_, addr_);
}

void BleMeterClient::onMeasurement(const uint8_t* data, size_t len) {
    if (!cb_) return;

    // Log the raw frame once per connection: this is how a field unit teaches us each meter's
    // CPS format (Garmin/Wahoo/Assioma) — and it directly answers whether this meter carries
    // cadence (the crank-rev flag) so the rebroadcast/OLED cadence is real, not assumed.
    if (!loggedFrame_) {
        loggedFrame_ = true;
        const uint16_t flags = decodeCpsFlags(data, len);
        const CpsBalance b0 = decodeCpsBalance(data, len);
        logf("[meter] cps flags=0x%04x cadence=%s balanceHalfPct=%d %s", flags,
             (flags & CPM_CRANK_REV_DATA_PRESENT) ? "yes" : "no",
             b0.present ? (int)b0.halfPct : -1, toHex(data, len).c_str());
    }

    // Keep the last few RAW frames for the /diag report — the exact bytes we need to add a new
    // meter's codec offline (real-data-first). A small bounded ring; oldest drops off.
    recentFrames_.push_back(toHex(data, len));
    if (recentFrames_.size() > 8) recentFrames_.erase(recentFrames_.begin());

    PowerReading r;
    r.power_w = decodeCpsPower(data, len);  // sint16 at bytes 2-3, regardless of flags
    r.t_ms = millis();

    // Forward the source meter's real L/R pedal balance (the Assioma DUO reports it — CPS flags
    // bit0, the byte at offset 4) so the spoofed crank shows the genuine split, not an implicit
    // 50/50. Held sticky: a meter that omits balance on some frames keeps its last good split
    // rather than flapping to the default (balanceHold_ is cleared on disconnect).
    r.balance_half_pct = balanceHold_.update(decodeCpsBalance(data, len));

    // Recover cadence from Crank Revolution Data the way a head unit does. The generic decoder
    // finds the crank-rev fields whatever optional fields precede them — the Assioma sends
    // pedal-balance (+ maybe torque) first, which the old fixed-offset path couldn't handle.
    const CpsCrankData ck = decodeCrankData(data, len);
    if (ck.present) {
        if (havePrevCrank_) {
            const float rpm = cadenceRpmFromCrank(prevRevs_, prevEventTime_, ck.cumulativeRevs,
                                                  ck.lastEventTime);
            if (rpm > 0.0f) r.cadence_rpm = (int16_t)(rpm + 0.5f);
        }
        prevRevs_ = ck.cumulativeRevs;
        prevEventTime_ = ck.lastEventTime;
        havePrevCrank_ = true;
    }
    lastReadingMs_ = r.t_ms;  // feed the staleness watchdog
    cb_(r);
}

void BleMeterClient::onDisconnected() {
    connected_ = false;
    haveTarget_ = false;
    havePrevCrank_ = false;  // don't carry crank deltas across a reconnect
    balanceHold_.reset();    // re-learn the new meter's L/R split on reconnect
    loggedFrame_ = false;    // re-log the frame format on the next connection
    lastReadingMs_ = 0;
    wantRescan_ = true;  // restart the scan from loop() (off the callback context)
    logf("[meter] disconnected");
}

void BleMeterClient::begin() {
    g_meter = this;
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&g_scanCb, false);
    scan->setActiveScan(true);  // harvest scan responses too (a real meter may carry its name there)
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
