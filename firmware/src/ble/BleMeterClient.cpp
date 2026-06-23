#include "ble/BleMeterClient.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>
#include <vector>

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

// The NimBLE scan is a single shared resource, so every active client routes through ONE scan
// callback (and one registry). For the spoof this is exactly one client; the meter-to-meter
// calibrator runs two (a DUT + a reference) concurrently. Clients are static-lifetime (never removed).
static std::vector<BleMeterClient*> g_clients;

// True when no registered client still needs to discover a meter (all connected or already targeted)
// — the cue to stop the shared scan, exactly as the single-client path did on its one match.
static bool noClientNeedsScan() {
    for (auto* c : g_clients) {
        if (c->wantsTarget()) return false;
    }
    return true;
}

// An advertiser is claimed if some OTHER client has already locked onto its address — so two clients
// reading two different meters never both grab the same one.
static bool addrClaimedByOther(const BleMeterClient* self, const std::string& addr) {
    for (auto* c : g_clients) {
        if (c != self && addr == c->claimedAddr() && !addr.empty()) return true;
    }
    return false;
}

// Resume scanning when a meter link drops, so unplug/replug (or a flaky meter) recovers. Per-instance:
// each client owns one of these so a disconnect routes to the right client (not a shared global).
class MeterClientCallbacks : public NimBLEClientCallbacks {
 public:
    explicit MeterClientCallbacks(BleMeterClient* owner) : owner_(owner) {}
    void onDisconnect(NimBLEClient* /*c*/, int /*reason*/) override {
        if (owner_) owner_->onDisconnected();
    }

 private:
    BleMeterClient* owner_;
};

class MeterScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* d) override {
        const std::string name = d->getName();
        const std::string addr = d->getAddress().toString();
        const bool cps = d->isAdvertisingService(NimBLEUUID(UUID_CPS));
        const int rssi = d->getRSSI();
        // Record every advertiser for the web picker on each client (the scan runs continuously until
        // a match connects, so the list fills during setup). Done before the match check so the page
        // can offer sources even when none is configured yet.
        for (auto* c : g_clients) c->recordCandidate(addr.c_str(), name.c_str(), rssi, cps);
        // Assign the advertiser to the FIRST client that wants it and isn't being claimed elsewhere.
        // isTarget (pure, host-tested isTargetMeter) picks per RUNTIME config: a PINNED address wins;
        // else a nameless peripheral matches by CPS UUID and a NAMED device must contain the name
        // filter — so a real "Stages NNNN" crank is NOT grabbed (the source-bouncing bug from
        // bike-session 2), and we never read a copy of our own spoof (a loop). MATCH_ANY_CPS loosens
        // the named-device rule for the WinRT fake_meter rig (decisions.md 2026-06-22).
        for (auto* c : g_clients) {
            if (c->wantsTarget() && c->isTarget(name, cps, addr) && !addrClaimedByOther(c, addr)) {
                c->onFound(addr.c_str(), d->getAddress().getType(), name.c_str());
                break;  // one advertiser feeds one client
            }
        }
        // Stop the shared scan only once every client has its meter (single-client: its one match).
        if (noClientNeedsScan()) NimBLEDevice::getScan()->stop();
    }
};
static MeterScanCallbacks g_scanCb;
static bool g_scanCallbacksSet = false;

// Start the shared scan if it isn't already running (a no-op guard so two clients don't double-start).
static void ensureScanning() {
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (!scan->isScanning()) scan->start(0, false);
}

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
    g_clients.push_back(this);  // register with the shared scan (one client for the spoof; two for cal)
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (!g_scanCallbacksSet) {
        scan->setScanCallbacks(&g_scanCb, false);
        scan->setActiveScan(true);  // harvest scan responses too (a meter may carry its name there)
        g_scanCallbacksSet = true;
    }
    ensureScanning();  // continuous scan; shared across all registered clients
}

void BleMeterClient::loop() {
    if (wantRescan_) {
        wantRescan_ = false;
        ensureScanning();  // a meter dropped; resume the shared scan to re-find it
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
        clientCb_ = new MeterClientCallbacks(this);  // per-instance (static-lifetime client)
        client_->setClientCallbacks(clientCb_, false);
    }
    if (client_->connect(NimBLEAddress(std::string(addr_), addrType_))) {
        NimBLERemoteService* svc = client_->getService(UUID_CPS);
        if (svc) {
            NimBLERemoteCharacteristic* ch = svc->getCharacteristic(UUID_CP_MEAS);
            if (ch && ch->canNotify()) {
                // Per-instance notify (the std::function captures this), so two clients route their
                // own meter's frames to the right BleMeterClient — no shared global.
                ch->subscribe(true, [this](NimBLERemoteCharacteristic*, uint8_t* data, size_t len,
                                           bool) { onMeasurement(data, len); });
                connected_ = true;
            }
        }
    }
    if (!connected_) {
        client_->disconnect();
        haveTarget_ = false;
        ensureScanning();  // resume the shared scan
    }
}
