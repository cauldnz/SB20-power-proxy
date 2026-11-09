#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <WiFi.h>
#include <ESPmDNS.h>  // also brings in IDF's mdns.h (mdns_service_instance_name_set)

#include "Obc.h"
#include "ObcApp.h"        // ObcStreamParser / ObcAppState / obcTxtRecord (pure, host-tested)
#include "LogBuffer.h"     // toHex
#include "net/DebugLog.h"  // logf -> /log

namespace sb20proxy {

// The OpenBikeControl network transport for the ESP (MDNS.md): advertise `_openbikecontrol._tcp` with
// the spec's TXT record, accept TCP consumers (trainer apps — MyWhoosh on Apple TV, the spec's
// mdns_trainer_app.py), stream them the SAME Button-State bytes the BLE transport notifies, send
// DeviceStatus on connect and every 30 s, and read their Haptic / App Information messages through the
// same pure decoders as BLE. ESP-only (this header pulls in <WiFi.h>, so it's never part of the host
// `native` build); everything protocol-shaped is in lib/proxy and host-tested. Driven from main's loop:
//   obcNet.begin(port, hostname, obcName, obcId);  // once WiFi STA is up and OBC is enabled
//   obcNet.loop();                                 // each loop(): accept, read, periodic status
//   obcNet.send(msg, len);                         // each Button-State message, to every consumer
// Single-context: begin/loop/send all run on the loop task (presses from the NimBLE task reach send()
// through main's ObcOutbox), so no locking here.
class ObcNet {
public:
    // MDNS.md: "Support multiple simultaneous connections". Three: an app, a bench reader, one spare.
    static constexpr int kMaxClients = 3;

    void begin(uint16_t port, const char* hostname, const std::string& name, const std::string& id) {
        if (up_) return;
        port_ = port;
        server_.begin(port_);
        server_.setNoDelay(true);  // a press goes out now, not when Nagle decides
        // MDNS.begin is idempotent when ArduinoOTA already started the responder (mdns_init returns OK
        // when it is running), so this is safe on both the push-OTA and the signed-pull builds.
        MDNS.begin(hostname);
        if (MDNS.addService("openbikecontrol", "tcp", port_)) {
            // Service instance = the device name (MDNS.md "<Device Name>._openbikecontrol._tcp.local.");
            // only this service is renamed — the host name and the _arduino OTA service keep theirs.
            mdns_service_instance_name_set("_openbikecontrol", "_tcp", name.c_str());
            const auto txt = obcTxtRecord(name, id);
            for (size_t i = 0; i < txt.size(); ++i)
                MDNS.addServiceTxt("openbikecontrol", "tcp", txt[i].first.c_str(), txt[i].second.c_str());
            logf("[obc] mdns '%s' _openbikecontrol._tcp :%u id=%s", name.c_str(), (unsigned)port_, id.c_str());
        } else {
            logf("[obc] mdns addService failed (TCP :%u still listening)", (unsigned)port_);
        }
        up_ = true;
    }

    void loop() {
        if (!up_) return;
        acceptNew();
        for (int i = 0; i < kMaxClients; ++i) serviceSlot(i);
        const uint32_t now = millis();
        if (now - lastStatusMs_ >= OBC_STATUS_PERIOD_MS) {
            lastStatusMs_ = now;
            for (int i = 0; i < kMaxClients; ++i)
                if (slots_[i].live) sendStatus(i);
        }
    }

    // Write one pre-encoded OBC message to every connected consumer. One write == one message.
    void send(const uint8_t* data, size_t len) {
        if (!up_ || data == nullptr || len == 0) return;
        for (int i = 0; i < kMaxClients; ++i)
            if (slots_[i].live && slots_[i].client.connected()) slots_[i].client.write(data, len);
    }

    bool up() const { return up_; }
    int clients() const {
        int n = 0;
        for (int i = 0; i < kMaxClients; ++i) n += slots_[i].live ? 1 : 0;
        return n;
    }
    const ObcAppState& app() const { return app_; }

private:
    struct Slot {
        WiFiClient client;
        ObcStreamParser parser;
        bool live = false;
    };

    void acceptNew() {
        if (!server_.hasClient()) return;
        WiFiClient incoming = server_.accept();
        for (int i = 0; i < kMaxClients; ++i) {
            if (!slots_[i].live) {
                slots_[i].client = incoming;
                slots_[i].client.setNoDelay(true);
                slots_[i].parser.reset();
                slots_[i].live = true;
                logf("[obc] tcp consumer %d connected from %s", i, incoming.remoteIP().toString().c_str());
                sendStatus(i);  // MDNS.md: status on connect (the reference device does the same)
                return;
            }
        }
        logf("[obc] tcp consumer refused: %d already connected", kMaxClients);
        incoming.stop();
    }

    void serviceSlot(int i) {
        Slot& s = slots_[i];
        if (!s.live) return;
        if (!s.client.connected()) {
            s.client.stop();
            s.live = false;
            app_.onDisconnect(i);  // MDNS.md: app information is cleared when the connection closes
            logf("[obc] tcp consumer %d disconnected", i);
            return;
        }
        uint8_t buf[64];
        int guard = 8;  // bounded per loop pass: a chatty app can't starve the crank loop
        while (s.client.available() > 0 && guard-- > 0) {
            const int n = s.client.read(buf, sizeof(buf));
            if (n <= 0) break;
            s.parser.feed(buf, (size_t)n, [this, i](const uint8_t* m, size_t len) { onAppMessage(i, m, len); });
        }
    }

    void onAppMessage(int i, const uint8_t* m, size_t len) {
        const ObcAppMsg kind = app_.onMessage(i, m, len);
        if (kind == ObcAppMsg::AppInfo) {
            const ObcAppInfo& a = app_.appInfo();
            logf("[obc] tcp app '%s' %s (%u ids%s)", a.appId, a.appVersion, (unsigned)a.buttonCount,
                 a.allButtons() ? " = all" : "");
        } else if (kind == ObcAppMsg::Haptic) {
            logf("[obc] tcp haptic pattern=%u (no motor: accepted)", (unsigned)app_.lastHaptic().pattern);
        } else {
            logf("[obc] tcp message ignored %s", toHex(m, len).c_str());
        }
    }

    void sendStatus(int i) {
        uint8_t st[3];
        const size_t n = encodeDeviceStatus(OBC_BATTERY_NA, true, st, sizeof(st));
        if (slots_[i].client.connected()) slots_[i].client.write(st, n);
    }

    WiFiServer server_{OBC_DEFAULT_PORT};  // rebound to the configured port in begin()
    Slot slots_[kMaxClients];
    ObcAppState app_;  // what the TCP consumers told us (loop task only)
    uint16_t port_ = 0;
    uint32_t lastStatusMs_ = 0;
    bool up_ = false;
};

}  // namespace sb20proxy
