#pragma once
#include "RuntimeConfig.h"

namespace sb20proxy {

// Non-volatile (NVS) storage for the user's RuntimeConfig (the meter source + doubling), so a
// tester's pick survives reboots / OTA flashes. Thin wrapper over the ESP32 Preferences API
// (namespace "proxycfg"), mirroring WifiCreds. Arduino-only; the pure RuntimeConfig type it
// stores lives in lib/proxy/RuntimeConfig.h and is host-tested there.
class ConfigStore {
public:
    // Load the stored config; RuntimeConfig::defaults() when nothing has been saved yet.
    static RuntimeConfig load();
    // Persist `c` (overwrites any existing config).
    static void save(const RuntimeConfig& c);
    // Erase — next load() returns the compile-time defaults.
    static void clear();
};

}  // namespace sb20proxy
