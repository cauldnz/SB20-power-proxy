#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Obc.h"

namespace sb20proxy {

// The OpenBikeControl pieces BETWEEN the wire codec (Obc.h) and the transport seams (NimBLE in
// src/ble/BleCrankPeripheral, WiFiServer/ESPmDNS in src/net/ObcNet.h): framing the app->device
// messages off a TCP byte stream, remembering the latest App Information per link, handing button
// messages from the BLE task to the loop that owns the sockets, and the mDNS TXT record. Pure +
// header-only, host-tested in test/test_obc. See code/findings/obc-protocol.md.

// ---- TCP framing for app -> device messages -------------------------------------------------------
// TCP is a byte stream: one write from the app may arrive split or glued to the next. BLE writes are
// already one message each and go straight to ObcAppState::onMessage. Known types frame themselves:
// Haptic is exactly 4 bytes, App Information carries its own lengths (Obc.h obcAppInfoLength). Any
// other leading byte (a device->app type, or noise) is skipped one byte at a time to resynchronise.
class ObcStreamParser {
public:
    // The longest App Information the length bytes can describe: 3 + 255 + 1 + 255 + 1 + 255.
    static constexpr size_t kCap = 770;

    // Feed received bytes; `onMsg(bytes, len)` fires once per complete Haptic / App Information.
    template <typename OnMsg>
    void feed(const uint8_t* d, size_t n, OnMsg&& onMsg) {
        if (d == nullptr) return;
        for (size_t k = 0; k < n; ++k) {
            const uint8_t b = d[k];
            const size_t i = have_;  // index of this byte within the current message
            if (i == 0) {
                if (b == OBC_MSG_HAPTIC) {
                    total_ = OBC_HAPTIC_LEN;
                } else if (b == OBC_MSG_APP_INFO) {
                    total_ = 0;  // learnt from the length bytes as they arrive
                    verLenAt_ = cntAt_ = kUnset;
                } else {
                    ++skipped_;  // not an app->device message: drop it and look again
                    continue;
                }
            }
            buf_[have_++] = b;
            if (buf_[0] == OBC_MSG_APP_INFO && total_ == 0) {
                if (i == 2) verLenAt_ = 3 + (size_t)b;
                else if (i == verLenAt_) cntAt_ = i + 1 + (size_t)b;
                else if (i == cntAt_) total_ = i + 1 + (size_t)b;
            }
            if (total_ != 0 && have_ == total_) {
                onMsg(buf_, have_);
                ++messages_;
                have_ = 0;
                total_ = 0;
            }
        }
    }

    void reset() { have_ = 0; total_ = 0; }
    uint32_t messages() const { return messages_; }
    uint32_t skipped() const { return skipped_; }  // bytes dropped while resynchronising

private:
    static constexpr size_t kUnset = (size_t)-1;
    uint8_t buf_[kCap];
    size_t have_ = 0;
    size_t total_ = 0;
    size_t verLenAt_ = kUnset;
    size_t cntAt_ = kUnset;
    uint32_t messages_ = 0;
    uint32_t skipped_ = 0;
};

// ---- App -> device state ----------------------------------------------------------------------------
// What the connected app(s) told us. One instance per transport seam, touched only from that seam's
// context (the NimBLE host task for BLE, the loop for TCP), so it needs no locking. A "link" is the
// seam's own handle for one consumer (a BLE connection handle, a TCP client slot).
enum class ObcAppMsg : uint8_t { Ignored = 0, Haptic, AppInfo };

class ObcAppState {
public:
    ObcAppState() { app_.clear(); last_ = {0, 0, 0}; }

    // One complete message from `link`. Haptic: we have no motor — accepted and counted (BLE.md: a
    // device without haptics accepts the write and does nothing). App Information: replaces whatever
    // any link sent before (BLE.md: a new message replaces the previous app information).
    ObcAppMsg onMessage(int link, const uint8_t* d, size_t n) {
        ObcHaptic h;
        if (decodeHaptic(d, n, h)) {
            last_ = h;
            ++haptics_;
            return ObcAppMsg::Haptic;
        }
        ObcAppInfo a;
        if (decodeAppInfo(d, n, a)) {
            app_ = a;
            appLink_ = link;
            hasApp_ = true;
            return ObcAppMsg::AppInfo;
        }
        ++ignored_;
        return ObcAppMsg::Ignored;
    }

    // The link went away. The spec empties the app information on every disconnect — but only that
    // app's: with two consumers (the SB20 and qz on one board), the SB20 dropping must not forget qz.
    void onDisconnect(int link) {
        if (hasApp_ && appLink_ == link) {
            hasApp_ = false;
            app_.clear();
        }
    }

    bool hasAppInfo() const { return hasApp_; }
    const ObcAppInfo& appInfo() const { return app_; }  // cleared (all buttons) when none
    int appInfoLink() const { return appLink_; }
    uint32_t hapticCount() const { return haptics_; }
    const ObcHaptic& lastHaptic() const { return last_; }
    uint32_t ignoredCount() const { return ignored_; }

private:
    ObcAppInfo app_;
    int appLink_ = -1;
    bool hasApp_ = false;
    ObcHaptic last_;
    uint32_t haptics_ = 0;
    uint32_t ignored_ = 0;
};

// ---- Device -> app fan-out queue --------------------------------------------------------------------
// A fixed FIFO of whole Button-State messages. The BLE side notifies inline, but the TCP sockets
// belong to the loop, and presses arrive on other contexts (the shifter's notify callback runs on the
// NimBLE host task), so they are queued here and the loop drains them to the sockets. Every message
// keeps its own slot: nothing is merged, re-ordered or rate-limited (PROTOCOL.md: every physical
// press is its own press/release, MUST for 0x30/0x31). When full, the NEW message is refused and
// counted rather than overwriting a queued release. Not itself thread-safe: the seam guards push/pop
// with its own lock (a FreeRTOS spinlock on the ESP32).
template <size_t Slots>
class ObcOutbox {
public:
    bool push(const uint8_t* d, size_t n) {
        if (d == nullptr || n == 0 || n > OBC_MAX_MSG) return false;
        if (count_ == Slots) {
            ++dropped_;
            return false;
        }
        Slot& s = slots_[(head_ + count_) % Slots];
        for (size_t i = 0; i < n; ++i) s.bytes[i] = d[i];
        s.len = (uint8_t)n;
        ++count_;
        return true;
    }
    // Copy the oldest message into `out` (at least OBC_MAX_MSG bytes); false when empty.
    bool pop(uint8_t* out, size_t& len) {
        if (count_ == 0 || out == nullptr) return false;
        const Slot& s = slots_[head_];
        for (size_t i = 0; i < s.len; ++i) out[i] = s.bytes[i];
        len = s.len;
        head_ = (head_ + 1) % Slots;
        --count_;
        return true;
    }
    size_t size() const { return count_; }
    uint32_t dropped() const { return dropped_; }

private:
    struct Slot {
        uint8_t bytes[OBC_MAX_MSG];
        uint8_t len;
    };
    Slot slots_[Slots];
    size_t head_ = 0;
    size_t count_ = 0;
    uint32_t dropped_ = 0;
};

// ---- mDNS identity ----------------------------------------------------------------------------------
// Who the board says it is to OBC consumers. Our own honest identity (the OBC side never impersonates:
// that is only the crank spoof's job, on a different service).
inline constexpr const char* OBC_MANUFACTURER = "SB20Proxy";  // == Config::CORRECTOR_MANUFACTURER
inline constexpr const char* OBC_MODEL = "SB20 Proxy";

// The TXT record MDNS.md requires, in the spec's order: version, id, name, service-uuids, manufacturer,
// model. `name` and `id` are the same strings the BLE side uses (FleetIdentity.h defaultObcName /
// obcDeviceId), so one board reads as one device on both transports.
inline std::vector<std::pair<std::string, std::string>> obcTxtRecord(const std::string& name,
                                                                     const std::string& id) {
    std::vector<std::pair<std::string, std::string>> t;
    t.push_back(std::make_pair(std::string("version"), std::string(OBC_PROTOCOL_VERSION)));
    t.push_back(std::make_pair(std::string("id"), id));
    t.push_back(std::make_pair(std::string("name"), name));
    t.push_back(std::make_pair(std::string("service-uuids"), std::string(OBC_BLE_SERVICE_UUID)));
    t.push_back(std::make_pair(std::string("manufacturer"), std::string(OBC_MANUFACTURER)));
    t.push_back(std::make_pair(std::string("model"), std::string(OBC_MODEL)));
    return t;
}

// MDNS.md: "Send periodic device status updates (type 0x02) every 30-60 seconds".
inline constexpr uint32_t OBC_STATUS_PERIOD_MS = 30000;

}  // namespace sb20proxy
