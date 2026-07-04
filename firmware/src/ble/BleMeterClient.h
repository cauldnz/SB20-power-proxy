#pragma once
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

#include <vector>

#include "Config.h"            // compile-time defaults for the source match
#include "Cps.h"               // BalanceHold (sticky pedal-balance)
#include "IPowerSource.h"
#include "SourceCandidate.h"   // discovered-source list for the web picker

class NimBLEClient;           // NimBLE-Arduino (global namespace); kept out of the header
class NimBLEClientCallbacks;  // per-instance disconnect callback (routes to the owning client)

namespace sb20proxy {

// IPowerSource over NimBLE: scans for the configured power meter, connects, subscribes to
// its Cycling Power Measurement, and emits PowerReadings. The real-hardware twin of
// MockMeter. Arduino/NimBLE-only (excluded from the host `native` build).
//
// Matching is by ADVERTISED CPS SERVICE UUID (0x1818) first, name second: a Windows BLE
// peripheral (our winrt fake_meter, or bless) advertises the service UUID but not a custom
// local name, so a name-only filter would never see it. A real Assioma advertises both.
class BleMeterClient : public IPowerSource {
public:
    void onReading(ReadingCb cb) override { cb_ = cb; }
    void begin() override;
    void loop() override;

    bool connected() const { return connected_; }
    const char* sourceName() const { return name_; }  // the connected meter's advertised name

    // Forward a zero-offset / calibration to the connected source meter: write Start Offset Compensation
    // (CP 0x0C) to its Cycling Power Control Point (0x2A66). Used when the SB20/app triggers a zero-reset
    // on our spoof so the REAL meter (the Assioma) actually gets zeroed. Fire-and-forget — returns whether
    // the write was issued (the meter's offset result comes back async on the CP indication, logged).
    // MUST be called from loop() context, never from a BLE callback (avoids a re-entrant central op).
    bool requestZeroOffset();

    // Scan coordination (the NimBLE scan is a shared singleton, so multiple clients route through one
    // callback — see the registry in the .cpp). wantsTarget(): still looking for a meter to read.
    // claimedAddr(): the address this client has locked onto ("" if none), so another client doesn't
    // grab the same advertiser. Pure accessors over existing state — no behaviour change for one client.
    bool wantsTarget() const { return !connected_ && !haveTarget_; }
    const char* claimedAddr() const { return (haveTarget_ || connected_) ? addr_ : ""; }

    // Set which source to read at RUNTIME (from NVS / the web UI) — a pinned address (""=match by
    // name) and the name substring. Call before begin(). Falls back to the compile-time defaults.
    void setMatch(const std::string& addr, const std::string& nameFilter) {
        matchAddr_ = addr;
        // NimBLE advertises addresses lowercase; a pin from NVS/the web UI may be upper/mixed case.
        // Lowercase it so the exact-match in isTargetMeter actually fires (else a pinned source silently
        // never connects).
        for (char& ch : matchAddr_) ch = (char)std::tolower((unsigned char)ch);
        matchNameFilter_ = nameFilter;
    }

    // Tell the loop guard which name is OUR spoof, so we never read a copy of our own crank (the
    // spoof identity is now runtime — keep the guard in sync). Call before begin().
    void setSpoofName(const std::string& s) { matchSpoofName_ = s; }

    // The runtime source-match decision (delegates to the pure, host-tested isTargetMeter). Used by
    // the scan callback so the matching uses the NVS-configured source, not the compile-time const.
    bool isTarget(const std::string& name, bool cps, const std::string& addr) const;

    // Discovered-source list for the web picker: the scan callback records every advertiser it sees
    // (skipping our own spoof) into a bounded, deduped list; the /setup page reads it. clear() before
    // a fresh discovery pass. The dedup/cap logic is the pure addCandidate (SourceCandidate.h).
    void recordCandidate(const char* addr, const char* name, int rssi, bool cps, bool ftms);
    std::vector<SourceCandidate> candidates() const { return candidates_; }
    void clearCandidates() { candidates_.clear(); }

    // The most recent RAW source-meter CPS frames (hex, oldest→newest) for the /diag report — what
    // we need to add a new meter's codec offline. Bounded ring filled in onMeasurement().
    std::vector<std::string> recentFrames() const { return recentFrames_; }

    // Shared-scan FTMS routing (§14 phase 4): the erg client registers here so the ONE NimBLE scan
    // also feeds it FTMS (0x1826) advertisers — it must NOT install its own scan callbacks, which
    // would deafen the meter clients. Registered from main (which links both sides); the standalone
    // ftms envs never compile this .cpp, so the coupling stays app-only.
    static void setFtmsScanSink(class FtmsErgClient* sink);

    // called from NimBLE callbacks
    void onFound(const char* addr, uint8_t addrType, const char* name);
    void onMeasurement(const uint8_t* data, size_t len);  // decode power (+ cadence) and emit
    void onDisconnected();  // link dropped: clear state and rescan from loop()

private:
    ReadingCb cb_;
    NimBLEClient* client_ = nullptr;
    NimBLEClientCallbacks* clientCb_ = nullptr;  // per-instance disconnect routing (created in loop)
    bool haveTarget_ = false;
    bool connected_ = false;
    bool wantRescan_ = false;
    char addr_[24] = {0};
    char name_[32] = {0};         // matched meter's advertised name (for /log field observation)
    bool loggedFrame_ = false;    // log the raw CPS frame once per connection (learn the format)
    uint8_t addrType_ = 0;
    // Crank-revolution state, to recover the meter's cadence the way a head unit does.
    bool havePrevCrank_ = false;
    uint16_t prevRevs_ = 0;
    uint16_t prevEventTime_ = 0;
    BalanceHold balanceHold_;     // sticky L/R split: hold last good across balance-less frames
    std::string matchAddr_ = Config::METER_ADDRESS;          // runtime source pin ("" = by name/UUID)
    std::string matchNameFilter_ = Config::METER_NAME_FILTER; // runtime source name substring
    std::string matchSpoofName_ = Config::SPOOF_NAME;         // our spoof's name (loop-guard exclusion)
    std::vector<SourceCandidate> candidates_;                 // discovered sources for the web picker
    std::vector<std::string> recentFrames_;                   // recent raw CPS frames (hex) for /diag
    uint32_t lastReadingMs_ = 0;  // for the staleness watchdog (meter went silent -> rescan)
};

}  // namespace sb20proxy
