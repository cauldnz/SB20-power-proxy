#pragma once
// FleetIdentity.h — the per-board default advertised identity + the fleet's real-crank names.
//
// Every board used to boot as "Stages 62144" (the old Config::SPOOF_NAME), which is bike 1's REAL
// left crank: a fresh or NVS-erased board next to that bike could pair the SB20 to an unfed spoof
// (0 W) or refuse pairing outright, and two boards flashed from one image were indistinguishable on
// the air (session 13 G0; docs/system-reference.md §7; #330). So there is no compile-time default
// name any more: a board with NOTHING stored derives one from its own MAC, deterministic and unique
// per board. A stored identity is never touched (RuntimeConfig::resolveIdentity).
//
// Pure + header-only (no Arduino): the seam reads the MAC (esp_read_mac in main.cpp) and passes it
// in, the same way SetupPin.h's setupApSsid() gets the bytes behind the "Setup-XXXX" SSID. Host-tested
// in test/test_fleetidentity; the Python twin is code/src/sb20proxy/qa/acceptance.py, and the two are
// parity-locked on shared golden vectors (code/tests/test_qa_acceptance.py).
#include <cstdint>
#include <cstdio>
#include <string>

namespace sb20proxy {

// The derived-default namespace: "Stages 9NNNN".
//
// ASSUMPTION (docs/system-reference.md §11): the Stages app has only been PROVEN to accept our own
// crank id 62145 on the bike (session 8). Whether it pairs a 9xxxx id is untested. The '9' prefix is
// chosen so that no derived id can ever equal a real crank's id in this fleet (62144 / 4963 on bike 1)
// and so a derived id is recognisable at a glance in a scan — not because 9xxxx is known-good. If the
// app rejects it, the remedy is a stored per-board identity in /setup; the derivation rule stays.
constexpr const char* kDerivedIdentityPrefix = "Stages 9";
// NNNN = the last 16 bits of the MAC, modulo this, zero-padded to 4 digits: 10000 distinct values
// (two boards whose MAC tails differ by a multiple of 10000 would collide; check /status, not luck).
constexpr unsigned kDerivedIdentityModulus = 10000;

// The identity a board advertises when nothing is stored. `mac` is the 6-byte BASE (efuse) MAC — the
// one `esptool read-mac` prints and BOARDS.md lists, whose last two bytes are ALSO the "Setup-XXXX"
// SSID suffix, so a board's default identity is predictable from either. (The BLE address a scanner
// shows is base + 2 on the ESP32 family; the base is the one humans have on paper.)
inline std::string defaultSpoofName(const uint8_t mac[6]) {
    const unsigned low16 = (static_cast<unsigned>(mac[4]) << 8) | mac[5];
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%s%04u", kDerivedIdentityPrefix, low16 % kDerivedIdentityModulus);
    return std::string(buf);
}

// Bike 1's real Stages cranks (left 62144, right 4963; captured over BLE — decisions.md 2026-06-17,
// session 9). A board must never advertise one of these NEXT TO that bike unless it is deliberately
// standing in for that crank (the single-right-crank rescue): the QA acceptance card fails on it
// unless told the rescue is intended (qa_board.py --crank-rescue).
constexpr const char* kRealCrankNames[] = {"Stages 62144", "Stages 4963"};

inline bool isRealCrankIdentity(const std::string& name) {
    for (const char* real : kRealCrankNames) {
        if (name == real) return true;
    }
    return false;
}

}  // namespace sb20proxy
