#pragma once
#include <cstddef>
#include <cstdint>
#include <WiFi.h>
#include <ESPmDNS.h>

namespace sb20proxy {

// Minimal OpenBikeControl network transport for the ESP (WiFi). A single consumer (a trainer app)
// discovers the device via mDNS (`_openbikecontrol._tcp`) and connects over TCP; we stream pre-encoded
// OBC messages (the same bytes as the BLE transport — see lib/proxy/Obc.h). ESP-only (this header pulls
// in <WiFi.h>, so it's never part of the host `native` build). Header-only; drive it from the main loop:
//   obcNet.begin(port, hostname);   // once WiFi STA is up
//   obcNet.poll();                  // each loop() — accept a consumer
//   obcNet.send(msg, len);          // on each debounced button event
class ObcNet {
public:
    void begin(uint16_t port, const char* hostname) {
        if (up_) return;
        port_ = port;
        server_.begin(port_);
        server_.setNoDelay(true);
        if (MDNS.begin(hostname)) {
            MDNS.addService("openbikecontrol", "tcp", port_);
        }
        up_ = true;
    }

    // Accept one consumer connection (drop extras — a bike has one app). Cheap; call from loop().
    void poll() {
        if (!up_) return;
        if (server_.hasClient()) {
            WiFiClient incoming = server_.available();
            if (client_ && client_.connected()) {
                incoming.stop();  // already have a consumer
            } else {
                client_ = incoming;
            }
        }
    }

    // Write a pre-encoded OBC message to the connected consumer (no-op if none). One write == one message.
    void send(const uint8_t* data, size_t len) {
        if (up_ && data != nullptr && len > 0 && client_ && client_.connected()) {
            client_.write(data, len);
        }
    }

    bool up() const { return up_; }
    bool hasClient() { return client_ && client_.connected(); }

private:
    WiFiServer server_{21587};  // rebound to the configured port in begin()
    WiFiClient client_;
    uint16_t port_ = 0;
    bool up_ = false;
};

}  // namespace sb20proxy
