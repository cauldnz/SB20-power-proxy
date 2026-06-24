#pragma once
#include <stddef.h>
#include <stdint.h>

#include <string>

// monocypher (vendored) at file scope — keyed BLAKE2b is our MAC primitive. See OtaVerify.h.
#include "monocypher.h"

namespace sb20proxy {

// Derive the device's WiFi-setup AP password: a deterministic 8-digit numeric PIN from the chip's MAC
// and a firmware-baked secret. Keyed BLAKE2b (secret = key) over the MAC, folded to 8 decimal digits.
//
// - 8 digits because WPA2-PSK requires a >= 8-char passphrase — 8 is the shortest legal, digits-only.
// - Deterministic + per-device: re-derivable from the MAC if you know the secret, unique per board.
// - SECURITY NOTE: the secret lives in the firmware image, so this protects the brief onboarding
//   window against a casual RF snooper, NOT an attacker who has the firmware binary + the (broadcast)
//   MAC. Real per-device secrecy would need the secret in efuses (a production hardening, deferred).
inline std::string deriveSetupPin(const uint8_t* mac, size_t macLen, const std::string& secret) {
    uint8_t h[8];
    crypto_blake2b_keyed(h, sizeof(h), reinterpret_cast<const uint8_t*>(secret.data()),
                         secret.size(), mac, macLen);
    uint64_t v = 0;
    for (size_t i = 0; i < sizeof(h); ++i) v = (v << 8) | h[i];
    uint32_t pin = static_cast<uint32_t>(v % 100000000ULL);  // 8 decimal digits
    char buf[9];
    for (int i = 7; i >= 0; --i) {
        buf[i] = static_cast<char>('0' + (pin % 10));
        pin /= 10;
    }
    buf[8] = '\0';
    return std::string(buf);
}

}  // namespace sb20proxy
