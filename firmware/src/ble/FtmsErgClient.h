#pragma once
#include <cstddef>
#include <cstdint>

#include "Ftms.h"  // pure: FtmsPowerRange + the codec

// FTMS erg-client seam: a NimBLE central that connects to a Fitness Machine and drives
// its resistance via Set Target Power (Request Control -> Start -> Set Target Power),
// reading the Supported Power Range to clamp. The on-device twin of ftms_erg.py's
// ErgController. Arduino/NimBLE-only; flag-gated (esp32c3-ftms-ergclient env). Validated
// on-air by the bench loop (F6); SPEC-BUILT pending Session 4 Part C.

class NimBLEClient;  // NimBLE-Arduino (global namespace)

namespace sb20proxy {

class FtmsErgClient {
 public:
    // Scan for an FTMS machine (by advertised 0x1826, name `targetName` if non-empty) and
    // connect. Call loop() regularly to (re)connect and converge to the desired power.
    // STANDALONE ONLY (the esp32c3-ftms-ergclient env): installs its own scan callbacks, which
    // would deafen the meter clients in the app builds — there, use beginShared() instead.
    void begin(const char* targetName);
    void loop();

    // App-build entry (§14 phase 4): the ONE NimBLE scan belongs to BleMeterClient's hub; main
    // registers this client via BleMeterClient::setFtmsScanSink and the hub feeds onFtmsAdvert.
    void beginShared(const char* targetName);
    bool wantsTarget() const { return begun_ && !connected_ && !haveTarget_; }
    void onFtmsAdvert(const char* addr, uint8_t addrType, const char* name);  // from the hub

    void setDesiredPower(int16_t watts) { desired_ = watts; }
    bool connected() const { return connected_; }
    bool controlled() const { return controlled_; }
    int16_t lastSent() const { return lastSent_; }

    // called from NimBLE callbacks
    void onFound(const char* addr, uint8_t addrType);
    void onIndication(const uint8_t* data, size_t len);
    void onDisconnected();

 private:
    void connectAndSetup();
    void step();  // emit the next control-point write toward the desired target

    NimBLEClient* client_ = nullptr;
    bool haveTarget_ = false;
    bool connected_ = false;
    char addr_[24] = {0};
    char name_[32] = {0};
    uint8_t addrType_ = 0;

    bool controlled_ = false;
    bool started_ = false;
    int16_t desired_ = 0;
    int16_t lastSent_ = 0;
    bool haveSent_ = false;
    FtmsPowerRange range_{0, 1000, 1};
    uint32_t lastStepMs_ = 0;
    bool begun_ = false;          // beginShared() called (the hub may feed us)
    char want_[32] = {0};         // trainer-name substring filter (shared-scan mode)
    uint32_t lastScanKickMs_ = 0; // rate-limit the rescue rescan after a disconnect
};

}  // namespace sb20proxy
