#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Ftms.h"

// The SB20 full proxy's mirror cache (code/findings/sb20-full-proxy.md §3a/§3b/§3e, issue #291).
//
// At link-up the proxy reads the bike's static values once (its name, the DIS strings and the FTMS
// read-only characteristics) and serves them, unchanged, from its own GATT. This cache validates each
// read as it lands and says when the set is complete: the proxy advertises only once `ready()`, so an
// app never meets an empty bike.
//
// Required for ready():
//   * the bike's name (from its advert or GAP 0x2A00; it seeds the proxy's own name),
//   * DIS manufacturer 0x2A29 and model 0x2A24,
//   * Fitness Machine Feature 0x2ACC,
//   * each range characteristic the Feature's Target Setting bits promise (the FTMS spec makes them
//     mandatory then): power 0x2AD8 (bit 3), resistance 0x2AD6 (bit 2), inclination 0x2AD5 (bit 1).
//     The SB20 sets all three (Target 0x0000200e, QDZ-sb20-ftms-gatt-20260706-0739.jsonl).
// Optional (mirrored when present): DIS serial 0x2A25, firmware 0x2A26, hardware 0x2A27, software 0x2A28.
//
// The proxy's advertised name is the bike's name + " PXY" (owner, 2026-10-02, §3e), checked against
// the 31-byte legacy advertising packet that also carries the flags and the 16-bit FTMS UUID.
namespace sb20proxy {

// ---- the proxy's advertised name ------------------------------------------------------------------

constexpr const char* kProxyNameSuffix = " PXY";
constexpr size_t kLegacyAdvBudget = 31;  // a legacy advertising PDU's AdvData
constexpr size_t kAdvFlagsBytes = 3;     // len, 0x01 Flags, value
constexpr size_t kAdvUuid16Bytes = 4;    // len, 0x03 Complete 16-bit UUIDs, 0x1826 LE
constexpr size_t kAdvNameOverhead = 2;   // len, 0x09 Complete Local Name
constexpr size_t kMaxProxyNameLen = kLegacyAdvBudget - kAdvFlagsBytes - kAdvUuid16Bytes - kAdvNameOverhead;  // 22

// Bytes the primary advertising packet uses with a name of `nameLen` characters.
constexpr size_t proxyAdvertBytes(size_t nameLen) {
    return kAdvFlagsBytes + kAdvUuid16Bytes + kAdvNameOverhead + nameLen;
}

struct ProxyName {
    bool ok = false;         // false: no usable bike name (empty, unprintable, or already a proxy's)
    bool truncated = false;  // the bike's part was shortened to fit the packet
    std::string name;
    size_t advBytes = 0;     // the primary packet's size with this name
};

inline bool nameEndsWith(const std::string& s, const char* suffix) {
    const std::string x(suffix);
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}

inline bool isPrintableName(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s) {
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

// "Stages Bike 0105" -> "Stages Bike 0105 PXY" (20 chars, a 29-byte packet). A bike name too long to
// fit has its end trimmed so the suffix survives and the name still starts like the bike's (qz needs
// the "Stages Bike" prefix). A name already ending " PXY" is a proxy, not a bike: refused.
inline ProxyName deriveProxyName(const std::string& bikeName) {
    ProxyName p;
    if (!isPrintableName(bikeName) || nameEndsWith(bikeName, kProxyNameSuffix)) return p;
    std::string base = bikeName;
    const size_t suffixLen = std::string(kProxyNameSuffix).size();
    if (base.size() + suffixLen > kMaxProxyNameLen) {
        base.resize(kMaxProxyNameLen - suffixLen);
        while (!base.empty() && base.back() == ' ') base.pop_back();
        p.truncated = true;
    }
    p.name = base + kProxyNameSuffix;
    p.advBytes = proxyAdvertBytes(p.name.size());
    p.ok = true;
    return p;
}

// ---- the cache -----------------------------------------------------------------------------------

enum class MirrorField : uint8_t {
    Name,
    Manufacturer,      // 0x2A29
    Model,             // 0x2A24
    Serial,            // 0x2A25
    FirmwareRev,       // 0x2A26
    HardwareRev,       // 0x2A27
    SoftwareRev,       // 0x2A28
    Feature,           // 0x2ACC
    PowerRange,        // 0x2AD8
    ResistanceRange,   // 0x2AD6
    InclinationRange,  // 0x2AD5
    Count
};

// The characteristic a field mirrors (Name has none: it comes from the advert or GAP).
inline uint16_t mirrorFieldUuid(MirrorField f) {
    switch (f) {
        case MirrorField::Manufacturer: return 0x2A29;
        case MirrorField::Model: return 0x2A24;
        case MirrorField::Serial: return 0x2A25;
        case MirrorField::FirmwareRev: return 0x2A26;
        case MirrorField::HardwareRev: return 0x2A27;
        case MirrorField::SoftwareRev: return 0x2A28;
        case MirrorField::Feature: return 0x2ACC;
        case MirrorField::PowerRange: return 0x2AD8;
        case MirrorField::ResistanceRange: return 0x2AD6;
        case MirrorField::InclinationRange: return 0x2AD5;
        default: return 0;
    }
}

class BikeMirror {
 public:
    static constexpr size_t kMaxString = 32;  // the SB20's longest DIS string is 14 ("Stages Cycling")

    // Forget everything (call at link-up, before the reads, so a reconnect never serves stale values).
    void clear() {
        for (auto& v : values_) v.clear();
        for (auto& h : have_) h = false;
    }

    // The bike's advertised (or GAP) name. Rejects an empty/unprintable name and a proxy's own.
    bool setBikeName(const std::string& name) {
        std::string s = trimNuls(name);
        if (s.size() > kMaxString || !isPrintableName(s) || nameEndsWith(s, kProxyNameSuffix)) return false;
        store(MirrorField::Name, std::vector<uint8_t>(s.begin(), s.end()));
        return true;
    }

    // A read of characteristic `uuid16` from the bike. Validated, then stored; false = rejected or
    // not a characteristic we mirror (the previous good value, if any, is kept).
    bool onRead(uint16_t uuid16, const uint8_t* d, size_t len) {
        if (d == nullptr) return false;
        for (size_t i = 0; i < (size_t)MirrorField::Count; ++i) {
            const MirrorField f = (MirrorField)i;
            if (f == MirrorField::Name || mirrorFieldUuid(f) != uuid16) continue;
            std::vector<uint8_t> v(d, d + len);
            if (!valid(f, v)) return false;
            if (isString(f)) {
                std::string s = trimNuls(std::string(v.begin(), v.end()));
                v.assign(s.begin(), s.end());
            }
            store(f, v);
            return true;
        }
        return false;
    }

    bool has(MirrorField f) const { return have_[(size_t)f]; }
    const std::vector<uint8_t>& value(MirrorField f) const { return values_[(size_t)f]; }
    std::string text(MirrorField f) const {
        const auto& v = values_[(size_t)f];
        return std::string(v.begin(), v.end());
    }
    std::string bikeName() const { return text(MirrorField::Name); }
    FtmsFeature feature() const {
        const auto& v = values_[(size_t)MirrorField::Feature];
        return decodeFitnessMachineFeature(v.data(), v.size());
    }

    // The fields still missing before the proxy may advertise (empty = ready).
    std::vector<MirrorField> missing() const {
        std::vector<MirrorField> out;
        for (MirrorField f : {MirrorField::Name, MirrorField::Manufacturer, MirrorField::Model,
                              MirrorField::Feature}) {
            if (!has(f)) out.push_back(f);
        }
        if (has(MirrorField::Feature)) {
            const uint32_t target = feature().target;
            if ((target & FTMS_TGT_POWER) && !has(MirrorField::PowerRange)) out.push_back(MirrorField::PowerRange);
            if ((target & FTMS_TGT_RESISTANCE) && !has(MirrorField::ResistanceRange))
                out.push_back(MirrorField::ResistanceRange);
            if ((target & FTMS_TGT_INCLINATION) && !has(MirrorField::InclinationRange))
                out.push_back(MirrorField::InclinationRange);
        }
        return out;
    }
    bool ready() const { return missing().empty(); }

    // The proxy's name, derived from the mirrored bike name (ok=false until the name is in).
    ProxyName proxyName() const {
        if (!has(MirrorField::Name)) return ProxyName{};
        return deriveProxyName(bikeName());
    }

 private:
    static bool isString(MirrorField f) {
        return f == MirrorField::Manufacturer || f == MirrorField::Model || f == MirrorField::Serial ||
               f == MirrorField::FirmwareRev || f == MirrorField::HardwareRev || f == MirrorField::SoftwareRev;
    }

    static std::string trimNuls(std::string s) {
        while (!s.empty() && s.back() == '\0') s.pop_back();
        return s;
    }

    // Each range is s16 min, s16 max, u16 increment (FTMS 0x2AD8 / 0x2AD6 / 0x2AD5).
    static bool validRange(const std::vector<uint8_t>& v, bool nonNegative) {
        if (v.size() != 6) return false;
        const int16_t mn = (int16_t)(v[0] | (v[1] << 8));
        const int16_t mx = (int16_t)(v[2] | (v[3] << 8));
        const uint16_t inc = (uint16_t)(v[4] | (v[5] << 8));
        if (mn > mx || inc == 0) return false;
        if (nonNegative && mn < 0) return false;
        return true;
    }

    static bool valid(MirrorField f, const std::vector<uint8_t>& v) {
        if (isString(f)) {
            const std::string s = trimNuls(std::string(v.begin(), v.end()));
            return s.size() <= kMaxString && isPrintableName(s);
        }
        switch (f) {
            case MirrorField::Feature: return v.size() == 8;
            case MirrorField::PowerRange: return validRange(v, true);  // watts are never negative
            case MirrorField::ResistanceRange: return validRange(v, false);
            case MirrorField::InclinationRange: return validRange(v, false);  // the SB20's goes to -100.0 %
            default: return false;
        }
    }

    void store(MirrorField f, const std::vector<uint8_t>& v) {
        values_[(size_t)f] = v;
        have_[(size_t)f] = true;
    }

    std::vector<uint8_t> values_[(size_t)MirrorField::Count];
    bool have_[(size_t)MirrorField::Count] = {};
};

}  // namespace sb20proxy
