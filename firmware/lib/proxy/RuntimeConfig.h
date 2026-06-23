#pragma once
#include <cstdio>
#include <string>
#include <vector>

#include "Config.h"
#include "Correction.h"  // CorrectionCurve — the meter-to-meter correction stored in NVS

namespace sb20proxy {

// The two product modes the same firmware serves (runtime-selectable from the web UI, persisted to
// NVS): SPOOF impersonates a Stages crank for the SB20; CORRECTOR rebroadcasts a corrected meter
// under our own identity (the meter-to-meter calibrator). See code/findings/meter-to-meter-proxy.md.
enum class ProxyMode { Spoof = 0, Corrector = 1 };

// Serialise a correction curve as "power:factor,power:factor,..." for one NVS line field. ':' and
// ',' never appear in a BLE address (its own field) or our names, so the '|'-delimited line stays
// unambiguous. Empty curve -> "".
inline std::string curveToString(const CorrectionCurve& curve) {
    std::string out;
    char buf[40];
    for (const auto& pt : curve.points) {
        if (!out.empty()) out += ',';
        std::snprintf(buf, sizeof(buf), "%.1f:%.4f", pt.power_w, pt.factor);
        out += buf;
    }
    return out;
}

inline CorrectionCurve curveFromString(const std::string& s) {
    CorrectionCurve curve;
    size_t i = 0;
    while (i < s.size()) {
        const size_t comma = s.find(',', i);
        const std::string tok = s.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
        const size_t colon = tok.find(':');
        if (colon != std::string::npos) {
            const float p = (float)std::atof(tok.substr(0, colon).c_str());
            const float f = (float)std::atof(tok.substr(colon + 1).c_str());
            if (f != 0.0f) curve.add(p, f);  // a 0 factor is never valid — skip a corrupt token
        }
        if (comma == std::string::npos) break;
        i = comma + 1;
    }
    return curve;
}

// The user-configurable runtime settings, persisted to NVS (the ConfigStore seam) and editable from
// the web UI: the meter SOURCE (DUT), the spoof/advertised identity, single-sided ×2, the product
// MODE, and — for CORRECTOR mode — the reference meter to calibrate against plus the fitted
// correction curve. Compile-time Config supplies the defaults. Pure + header-only, host-unit-tested
// like the rest of lib/proxy.
struct RuntimeConfig {
    std::string meterAddress;        // pin the source/DUT by BLE address ("" = match by name/UUID)
    std::string meterNameFilter;     // name substring the source/DUT must contain (when not pinned)
    bool singleSidedDouble = false;  // double a single-sided source (a surviving R crank) for total
    std::string spoofName;           // advertised identity (Stages name to spoof, or our own in CORRECTOR)
    std::string spoofSerial;         // DIS serial (char 0x2A25) presented to the head unit

    ProxyMode mode = ProxyMode::Spoof;   // SPOOF (SB20 crank) | CORRECTOR (meter-to-meter)
    std::string refMeterAddress;         // CORRECTOR: reference meter pinned by address (calibration)
    std::string refMeterNameFilter;      // CORRECTOR: reference meter name substring
    CorrectionCurve curve;               // CORRECTOR: fitted DUT->reference correction (empty = none)

    // The factory defaults, from compile-time Config (used when nothing is stored in NVS yet).
    static RuntimeConfig defaults() {
        RuntimeConfig c;
        c.meterAddress = Config::METER_ADDRESS;          // usually "" — match by name/UUID
        c.meterNameFilter = Config::METER_NAME_FILTER;    // e.g. "ASSIOMA"
        c.singleSidedDouble = false;
        c.spoofName = Config::SPOOF_NAME;                 // e.g. "Stages 62144"
        c.spoofSerial = Config::SPOOF_SERIAL;
        c.mode = ProxyMode::Spoof;                        // ships as the SB20 crank spoof
        return c;
    }

    // Compact one-line serialisation for NVS:
    //   "addr|nameFilter|double|spoofName|spoofSerial|mode|refAddr|refNameFilter|curve". '|' can't
    // appear in any field (BLE addresses are ':'-separated, names/filters don't use it, the curve
    // uses ':'/','), so it's a safe delimiter. A stored line written by the UI is a COMPLETE config.
    std::string toLine() const {
        return meterAddress + "|" + meterNameFilter + "|" + (singleSidedDouble ? "1" : "0") + "|" +
               spoofName + "|" + spoofSerial + "|" + (mode == ProxyMode::Corrector ? "1" : "0") +
               "|" + refMeterAddress + "|" + refMeterNameFilter + "|" + curveToString(curve);
    }

    // Parse a stored line. Backward-compatible: an old line (no mode/ref/curve) keeps SPOOF + no
    // curve — exactly today's behaviour; a 3-field pre-spoof-picker line keeps the default identity;
    // a malformed line (<3 fields) falls back to defaults() so a corrupt NVS value can never wedge
    // the device. Empty optional fields keep their defaults.
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
        RuntimeConfig c = defaults();  // identity + mode default unless the line carries them
        c.meterAddress = f[0];
        c.meterNameFilter = f[1];
        c.singleSidedDouble = (f[2] == "1");
        if (f.size() >= 4 && !f[3].empty()) c.spoofName = f[3];
        if (f.size() >= 5 && !f[4].empty()) c.spoofSerial = f[4];
        if (f.size() >= 6) c.mode = (f[5] == "1") ? ProxyMode::Corrector : ProxyMode::Spoof;
        if (f.size() >= 7) c.refMeterAddress = f[6];
        if (f.size() >= 8) c.refMeterNameFilter = f[7];
        if (f.size() >= 9) c.curve = curveFromString(f[8]);
        return c;
    }
};

}  // namespace sb20proxy
