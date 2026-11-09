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

// The board's OpenBikeControl name: "OBC-SB20-NNNN", NNNN the same MAC-derived digits as the crank
// default above, so a board's two names are recognisably one board ("Stages 91468" / "OBC-SB20-1468").
// It used to be the fixed string "OBC-SB20", so two devmode boards in one room collided (session 13;
// #367). Unlike the crank name it is never stored: nothing pairs to it by name, so it stays derived.
// The "OBC-" prefix is kept because our qz fork's listener (fork bfb84695f) still matches on it; the
// upstream-bound listener and the spec's reference apps match on the OBC service UUID instead.
constexpr const char* kObcNamePrefix = "OBC-SB20-";

inline std::string defaultObcName(const uint8_t mac[6]) {
    const unsigned low16 = (static_cast<unsigned>(mac[4]) << 8) | mac[5];
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%s%04u", kObcNamePrefix, low16 % kDerivedIdentityModulus);
    return std::string(buf);
}

// The board's OBC device id: the base MAC as 12 lowercase hex digits ("a4cb8fdae9cc"), MDNS.md's
// "unique device identifier (MAC address or serial)": the TXT record's id=, the same on every
// transport, and the same MAC BOARDS.md lists.
inline std::string obcDeviceId(const uint8_t mac[6]) {
    char buf[13];
    std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
                  mac[5]);
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

// --- The "Stages " prefix: what makes a spoof identity FINDABLE by the bike (#384) --------------
//
// The SB20 scans for its crank BY NAME, and a real Stages crank advertises as "Stages <id>" — the
// names above, and the "Stages 32059" an outside user read off his own right crank in #382. So a
// BARE id ("51862") is never found, and the failure is silent: the board advertises happily, the
// bike simply never pairs, and nothing in the UI or /log says why. That user lost a session to it.
//
// Typing the bare id is the natural mistake, not a careless one: the Stages app, the bike's own
// stored crank ids and our docs all name cranks by number ("51862 : 32059"). And because Config.h
// ships NO default name, entering the real crank's identity is the intended SPOOF workflow — so
// this is the common path for every new user, not an edge case.
//
// Hence two rules, applied on the way IN (ConfigPage.h's two form parsers):
//   1. an all-digits identity is NORMALISED to "Stages <digits>" — the intent is unambiguous, and
//      it is what the real crank calls itself;
//   2. anything else lacking the prefix is left ALONE but FLAGGED, so /setup can warn without
//      blocking a deliberately odd name.
// Blank is untouched by both: blank means "this board's own default", derived above at boot.
constexpr const char* kStagesNamePrefix = "Stages ";

inline bool isAllDigits(const std::string& s) {
    if (s.empty()) return false;
    for (const char ch : s) {
        if (ch < '0' || ch > '9') return false;
    }
    return true;
}

inline bool hasStagesPrefix(const std::string& name) {
    return name.rfind(kStagesNamePrefix, 0) == 0;
}

// Rule 1. "51862" -> "Stages 51862". Blank stays blank; an already-prefixed or non-numeric name is
// returned unchanged, so this is idempotent and safe to apply on every save. CORRECTOR identities
// must NOT pass through here — that mode advertises our own honest name, which is not a Stages id.
inline std::string normaliseSpoofName(const std::string& name) {
    if (isAllDigits(name)) return std::string(kStagesNamePrefix) + name;
    return name;
}

// Rule 2. True when a SPOOF identity is set but will not be found by the bike: a non-blank name
// without the prefix. (The numeric case is normalised away before this is asked, so what reaches
// here is a custom name.) Advisory only — the caller warns, it does not reject. Ask this ONLY in
// SPOOF mode: Config::CORRECTOR_NAME has no "Stages " prefix and is correct without one.
inline bool spoofNameMissingStagesPrefix(const std::string& name) {
    return !name.empty() && !hasStagesPrefix(name);
}

}  // namespace sb20proxy
