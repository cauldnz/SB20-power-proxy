#pragma once
#include <string>

#include "Config.h"

namespace sb20proxy {

// The user-configurable runtime settings — the meter SOURCE (which device to read) + the
// single-sided doubling. Persisted to NVS (the ConfigStore seam) and editable from the web UI,
// so a tester picks their meter without a rebuild. Compile-time Config supplies the defaults and
// the fields that stay fixed (the spoof's Stages identity, the calibration replies). Pure +
// header-only, so it is host-unit-tested exactly like the rest of lib/proxy.
struct RuntimeConfig {
    std::string meterAddress;        // pin the source by BLE address ("" = match by name/UUID)
    std::string meterNameFilter;     // name substring the source must contain (when not pinned)
    bool singleSidedDouble = false;  // double a single-sided source (a surviving R crank) for total

    // The factory defaults, from compile-time Config (used when nothing is stored in NVS yet).
    static RuntimeConfig defaults() {
        RuntimeConfig c;
        c.meterAddress = Config::METER_ADDRESS;        // usually "" — match by name/UUID
        c.meterNameFilter = Config::METER_NAME_FILTER;  // e.g. "ASSIOMA"
        c.singleSidedDouble = false;
        return c;
    }

    // Compact one-line serialisation for NVS: "addr|nameFilter|double". '|' can't appear in a BLE
    // address (':'-separated hex) or our name filters, so it's a safe delimiter. A stored line is
    // a COMPLETE config (written by the UI), so parsing is literal — no default-merge.
    std::string toLine() const {
        return meterAddress + "|" + meterNameFilter + "|" + (singleSidedDouble ? "1" : "0");
    }

    // Parse a stored line. A malformed/empty line falls back to defaults() (so a corrupt NVS value
    // can never wedge the source matching).
    static RuntimeConfig fromLine(const std::string& s) {
        const size_t a = s.find('|');
        if (a == std::string::npos) return defaults();
        const size_t b = s.find('|', a + 1);
        if (b == std::string::npos) return defaults();
        RuntimeConfig c;
        c.meterAddress = s.substr(0, a);
        c.meterNameFilter = s.substr(a + 1, b - a - 1);
        c.singleSidedDouble = (s.substr(b + 1) == "1");
        return c;
    }
};

}  // namespace sb20proxy
