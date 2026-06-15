#pragma once
// Copy this to `wifi_secret.h` (gitignored) and fill in your 2.4 GHz network.
//
// Not used by the BLE proxy core yet — it's here for the next firmware step: WiFi for
//   * OTA flashing (the esp32c3-ota env), and
//   * serial-over-HTTP observability (the C3 Super Mini's native-USB serial is flaky, so
//     like raedian-probe's blecap.cpp we'll serve logs/status over a tiny HTTP endpoint).
//
// Pattern mirrors cauldnz/raedian-probe (firmware/wifi_secret.h, included as "../wifi_secret.h").

#define WIFI_SSID  "your-2.4GHz-ssid"
#define WIFI_PASS  "your-password"
