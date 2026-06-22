#pragma once
#include <string>
#include <vector>

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
    std::string spoofName;           // advertised crank identity (e.g. "Stages 62144") the SB20 pairs to
    std::string spoofSerial;         // DIS serial (char 0x2A25) presented as the crank

    // The factory defaults, from compile-time Config (used when nothing is stored in NVS yet).
    static RuntimeConfig defaults() {
        RuntimeConfig c;
        c.meterAddress = Config::METER_ADDRESS;          // usually "" — match by name/UUID
        c.meterNameFilter = Config::METER_NAME_FILTER;    // e.g. "ASSIOMA"
        c.singleSidedDouble = false;
        c.spoofName = Config::SPOOF_NAME;                 // e.g. "Stages 62144"
        c.spoofSerial = Config::SPOOF_SERIAL;
        return c;
    }

    // Compact one-line serialisation for NVS: "addr|nameFilter|double|spoofName|spoofSerial". '|'
    // can't appear in a BLE address (':'-separated hex), our name filters, or a "Stages NNNNN" name,
    // so it's a safe delimiter. A stored line written by the UI is a COMPLETE config.
    std::string toLine() const {
        return meterAddress + "|" + meterNameFilter + "|" + (singleSidedDouble ? "1" : "0") + "|" +
               spoofName + "|" + spoofSerial;
    }

    // Parse a stored line. Backward-compatible: an old 3-field line (no spoof identity) keeps the
    // default identity; a malformed line (<3 fields) falls back to defaults() so a corrupt NVS value
    // can never wedge the device. An empty spoof field also keeps the default (we must advertise one).
    static RuntimeConfig fromLine(const std::string& s) {
        std::vector<std::string> f;
        size_t pos = 0;
        while (true) {
            const size_t bar = s.find('|', pos);
            f.push_back(bar == std::string::npos ? s.substr(pos) : s.substr(pos, bar - pos));
            if (bar == std::string::npos) break;
            pos = bar + 1;
        }
        if (f.size() < 3) return defaults();
        RuntimeConfig c = defaults();  // spoof identity defaults unless the line carries it
        c.meterAddress = f[0];
        c.meterNameFilter = f[1];
        c.singleSidedDouble = (f[2] == "1");
        if (f.size() >= 4 && !f[3].empty()) c.spoofName = f[3];
        if (f.size() >= 5 && !f[4].empty()) c.spoofSerial = f[4];
        return c;
    }
};

}  // namespace sb20proxy
