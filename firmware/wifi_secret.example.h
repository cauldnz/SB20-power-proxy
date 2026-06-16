#pragma once
// OPTIONAL — you usually do NOT need this file.
//
// WiFi credentials are provisioned at runtime via the captive portal: on first boot (or
// whenever the stored network can't be joined) the device raises the open AP 'SB20-Setup'
// and serves a setup page at http://192.168.4.1/ — pick your 2.4 GHz network there and it is
// saved to NVS. The esp32c3-ota build compiles fine with this file absent.
//
// Copy this to `wifi_secret.h` (gitignored) ONLY if you want to bake in a network to SEED the
// very first boot (e.g. a known bench AP) instead of using the portal. NVS (the portal) always
// takes precedence, so once provisioned this file is ignored.
//
// Pattern mirrors cauldnz/raedian-probe (firmware/wifi_secret.h, included as "../wifi_secret.h").

#define WIFI_SSID  "your-2.4GHz-ssid"
#define WIFI_PASS  "your-password"
