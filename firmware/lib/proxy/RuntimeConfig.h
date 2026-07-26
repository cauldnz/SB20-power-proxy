#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "Config.h"
#include "Correction.h"      // CorrectionCurve — the meter-to-meter correction stored in NVS
#include "Sb20ButtonMap.h"   // the configurable SB20-shifter-button -> action binding

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
            // Accept any WELL-FORMED factor (incl. 0.0 — a low-power point clamped to 0 by a
            // negative-offset fit is legitimate). strtof's end pointer distinguishes a real number
            // from a corrupt token (atof can't: "abc" and "0" both give 0.0), so we don't silently
            // drop a valid breakpoint and lose the curve's anchor.
            const std::string fstr = tok.substr(colon + 1);
            char* end = nullptr;
            const float f = std::strtof(fstr.c_str(), &end);
            if (end != fstr.c_str() && *end == '\0') curve.add(p, f);
        }
        if (comma == std::string::npos) break;
        i = comma + 1;
    }
    return curve;
}

// Strip the NVS line delimiter '|' from a field value. Names, filters and serials are all
// user-supplied and land in '|'-delimited slots of RuntimeConfig::toLine — a literal '|' would
// inject an extra delimiter and shift every later field (mode/curve/calibrating) on reload.
//
// toLine() applies this to every field itself. That is deliberate: it used to be the *caller's*
// job, hand-written at six form-parsing sites in ConfigPage.h/CalibrationPage.h, and two fields
// (meterAddress, refMeterAddress) had no strip at all. One forgotten call silently corrupted the
// stored config. The serialiser owns its own delimiter invariant now; the form parsers keep
// calling it too, so what the user sees echoed back matches what gets stored.
inline std::string stripConfigDelims(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c != '|') o += c;
    }
    return o;
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
    bool calibrating = false;            // transient: this boot is a live calibration session (both
                                         // meters pinned, feeding the wizard) — set by /calibrate/start,
                                         // cleared on save/cancel. The wizard reboots in/out of it.
    std::string trainerNameFilter;       // FTMS trainer to erg-drive from the workout engine
                                         // ("" = erg off). Name substring, like meterNameFilter.
    bool obcEnabled = false;             // re-present the SB20 shifter buttons as OpenBikeControl
                                         // (BLE on ESP+nRF; mDNS/TCP on the ESP). See obc-protocol.md.
    uint16_t obcPort = 21587;            // OBC mDNS/TCP listen port (ESP network transport)
    bool obcDevmode = false;             // Devmode: advertise as an "OBC-…" controller (not the Stages
                                         // crank) so an OBC listener (e.g. qz) can discover + connect,
                                         // and drive virtual button presses over HTTP (GET /obc/press).
    bool obcSinkShifter = false;         // sink the SB20's own shifter buttons (a BLE central to the
                                         // SB20's vendor char 0c46be60) and re-broadcast them as OBC —
                                         // the "OBC bike add-on". Implies the OBC service.
    Sb20ButtonMap obcButtons = Sb20ButtonMap::defaults();  // per-button action binding (web-configurable)

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

    // Compact one-line serialisation for NVS, with a leading schema tag:
    //   "v2|addr|nameFilter|double|spoofName|spoofSerial|mode|refAddr|refNameFilter|curve|…"
    //
    // The tag exists because the layout used to be *untagged* positional fields whose meaning was
    // inferred purely from how many there were — "count == age". That made the append-only rule an
    // unwritten convention with nothing enforcing it. With a tag, a reader knows what it is holding.
    //
    // Fields remain APPEND-ONLY: new settings go on the end, so a reader that knows fewer fields
    // than the writer still parses every field it does know. Never insert or reorder — that is the
    // change the version tag is there to make thinkable, and it needs a new parse branch, not just
    // a bumped number.
    //
    // Every field is passed through stripConfigDelims here, so no caller can inject a delimiter.
    static constexpr int kLineVersion = 2;

    std::string toLine() const {
        const auto s = [](const std::string& v) { return stripConfigDelims(v); };
        return "v" + std::to_string(kLineVersion) + "|" + s(meterAddress) + "|" + s(meterNameFilter) +
               "|" + (singleSidedDouble ? "1" : "0") + "|" + s(spoofName) + "|" + s(spoofSerial) +
               "|" + (mode == ProxyMode::Corrector ? "1" : "0") + "|" + s(refMeterAddress) + "|" +
               s(refMeterNameFilter) + "|" + s(curveToString(curve)) + "|" + (calibrating ? "1" : "0") +
               "|" + s(trainerNameFilter) + "|" + (obcEnabled ? "1" : "0") + "|" +
               std::to_string(obcPort) + "|" + (obcDevmode ? "1" : "0") + "|" +
               (obcSinkShifter ? "1" : "0") + "|" + s(obcButtons.toString());
    }

    // Parse a stored line, tagged or not.
    //
    // A leading "v<digits>" field is the schema tag and is consumed; anything else means the line
    // predates the tag (v1) and is parsed exactly as before. The discriminator is safe because slot
    // 0 is meterAddress, which is either empty or a ':'-separated BLE address — never "v2".
    //
    // A tag NEWER than this build still parses positionally, which is correct under the append-only
    // rule and is the behaviour that matters on an OTA rollback: the rider keeps their meter pairing
    // instead of silently reverting to defaults.
    //
    // Otherwise backward-compatible as before: an old line (no mode/ref/curve) keeps SPOOF + no
    // curve; a 3-field pre-spoof-picker line keeps the default identity; a malformed line (<3
    // fields) falls back to defaults() so a corrupt NVS value can never wedge the device. Empty
    // optional fields keep their defaults.
    static RuntimeConfig fromLine(const std::string& s) {
        std::vector<std::string> f;
        size_t pos = 0;
        while (true) {
            const size_t bar = s.find('|', pos);
            f.push_back(bar == std::string::npos ? s.substr(pos) : s.substr(pos, bar - pos));
            if (bar == std::string::npos) break;
            pos = bar + 1;
        }
        if (!f.empty() && isVersionTag(f[0])) f.erase(f.begin());
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
        if (f.size() >= 10) c.calibrating = (f[9] == "1");
        if (f.size() >= 11) c.trainerNameFilter = f[10];
        if (f.size() >= 12) c.obcEnabled = (f[11] == "1");
        if (f.size() >= 13 && !f[12].empty()) c.obcPort = (uint16_t)std::atoi(f[12].c_str());
        if (f.size() >= 14) c.obcDevmode = (f[13] == "1");
        if (f.size() >= 15) c.obcSinkShifter = (f[14] == "1");
        if (f.size() >= 16 && !f[15].empty()) c.obcButtons = Sb20ButtonMap::fromString(f[15]);
        return c;
    }

  private:
    // "v" followed by at least one digit and nothing else.
    static bool isVersionTag(const std::string& t) {
        if (t.size() < 2 || t[0] != 'v') return false;
        for (size_t i = 1; i < t.size(); ++i) {
            if (t[i] < '0' || t[i] > '9') return false;
        }
        return true;
    }
};

}  // namespace sb20proxy
