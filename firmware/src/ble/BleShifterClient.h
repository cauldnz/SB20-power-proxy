#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

class NimBLEClient;  // NimBLE-Arduino (global namespace)

namespace sb20proxy {

// A NimBLE central that connects to the Stages SB20 and subscribes to its vendor shifter button
// characteristic (0c46be60, service 0c46be5f — code/findings/shifter-ble-protocol.md): the READ half
// of "sink the SB20's buttons -> re-broadcast as OpenBikeControl". Each raw notification is handed to
// onNotify (main wires it to ObcShifterSource -> BleCrankPeripheral::notifyObc). The box is the SB20's
// power-meter PERIPHERAL, so reading the shifter needs this SEPARATE central role — bench-gated.
//
// Mirrors FtmsErgClient: a shared-scan SINK central. The ONE NimBLE scan belongs to BleMeterClient's
// hub; main registers this via BleMeterClient::setShifterScanSink and the hub feeds onSb20Advert (it
// must NOT install its own scan callbacks, which would deafen the meter clients). Arduino/NimBLE-only
// (excluded from the host `native` build); the decode/map/encode it feeds is the pure ObcShifterSource.
class BleShifterClient {
 public:
    using NotifyCb = std::function<void(const uint8_t*, size_t)>;
    void onNotify(NotifyCb cb) { cb_ = cb; }

    // Register with the shared scan and start hunting for the SB20 by advertised-name substring
    // (e.g. "Stages Bike"). Call before the hub's scan is relied upon; loop() (re)connects.
    void beginShared(const char* targetName);
    void loop();
    bool wantsTarget() const { return begun_ && !connected_ && !haveTarget_; }
    bool connected() const { return connected_; }
    void onSb20Advert(const char* addr, uint8_t addrType, const char* name);  // from the scan hub

    // called from NimBLE callbacks
    void onFound(const char* addr, uint8_t addrType);
    void onNotification(const uint8_t* data, size_t len);  // raw 0c46be60 frame -> onNotify
    void onDisconnected();

 private:
    void connectAndSetup();

    NotifyCb cb_;
    NimBLEClient* client_ = nullptr;
    bool begun_ = false;
    bool haveTarget_ = false;
    bool connected_ = false;
    char addr_[24] = {0};
    char want_[32] = {0};          // SB20 advertised-name substring filter
    uint8_t addrType_ = 0;
    uint32_t lastScanKickMs_ = 0;  // rate-limit the rescue rescan after a disconnect
};

}  // namespace sb20proxy
