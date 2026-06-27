#pragma once
#include <string>

namespace sb20proxy {

// Non-volatile (NVS) storage for the loaded workout JSON, so a workout survives reboots / OTA flashes
// (mirrors ConfigStore / WifiCreds). Arduino-only; the pure WorkoutEngine that parses the JSON lives
// in lib/proxy and is host-tested there.
class WorkoutStore {
public:
    static std::string load();              // stored workout JSON, or "" if none
    static void save(const std::string& json);
    static void clear();
};

}  // namespace sb20proxy
