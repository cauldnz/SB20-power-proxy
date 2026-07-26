// PeerRole.h — which of the bridge's four BLE peers is this?
//
// The nRF bridge runs up to four concurrent central links, all multiplexed by peer *name*:
//
//   Source       the power meter / DUT we read, correct and re-broadcast (the point of the device)
//   Trainer      an FTMS trainer we drive in erg
//   Reference    a second meter, connected only during calibration, to fit the DUT against
//   Sb20Shifter  the SB20 itself, so its handlebar buttons can be re-presented over OBC
//
// The routing ladder used to live inline in *both* `scanCb` (should I connect to this advert?)
// and `centralConnectCb` (what is this link I just opened?), which meant one policy written
// twice. The two copies had drifted — see `classifyConnection` below. This header is the single
// pure, host-testable statement of the ladder; the radio callbacks become thin adapters over it.
//
// Deliberately free of Arduino/Bluefruit so it compiles into `pio test -e native`.
#pragma once

#include <cstdint>
#include <cstring>

namespace bridge {

// The SB20's own advertised name prefix, matched to sink its handlebar buttons.
inline constexpr const char* kSb20PeerName = "Stages Bike";

enum class PeerRole : uint8_t {
    Source = 0,   // default: anything not claimed by a more specific role
    Trainer,
    Reference,
    Sb20Shifter,
};

// Name filters, as configured. An empty string means "this role is not configured".
//
// NOTE the asymmetry, which is load-bearing and easy to get wrong: an empty *source* filter
// means "accept any CPS advertiser" (the desk fake meter), whereas an empty trainer/reference
// filter disables that role entirely. That is why `source` is only ever consulted behind an
// explicit `[0]` check by the caller.
struct PeerFilters {
    const char* trainer = "";
    const char* reference = "";
    const char* source = "";
};

// Live connection state the ladder branches on.
struct PeerRoleState {
    bool calibrating = false;
    bool sinkShifter = false;    // OBC button sink enabled
    bool ergConnected = false;   // a trainer link is already up
    bool refConnected = false;   // a reference link is already up
    bool sb20Connected = false;  // an SB20 link is already up
    bool srcConnected = false;   // a source link is already up
    bool isSourceLink = false;   // connect-time only: this handle IS the source link
};

// `strstr` semantics, spelled out because they carry the empty-filter behaviour: strstr(x, "")
// returns x, so an empty filter matches *everything*. Callers must gate on `filter[0]` when
// "empty means disabled" is what they want.
inline bool nameContains(const char* name, const char* needle) {
    if (name == nullptr || needle == nullptr) return false;
    return std::strstr(name, needle) != nullptr;
}

// The role ladder, shared by both call sites. Ordering is significant: a peer whose name matches
// several filters is claimed by the most specific role first.
//
// `isSourceLink` short-circuits everything — the link we are already reading power from keeps its
// role no matter what its name happens to match, or the bridge would tear down the very meter it
// exists to read. It is only meaningful at connect time; scan-time callers leave it false.
inline PeerRole classifyByName(const char* name, const PeerFilters& f, const PeerRoleState& s) {
    if (s.isSourceLink) return PeerRole::Source;

    // 1. The erg trainer, when configured and not already held.
    if (f.trainer[0] && !s.ergConnected && nameContains(name, f.trainer)) return PeerRole::Trainer;

    // 2. The calibration reference. Excluded when the name would *also* match the source filter, so
    //    one meter can't be latched as both DUT and reference (the fit would compare it against
    //    itself). The `f.source[0] &&` is load-bearing: strstr(x, "") matches everything, so without
    //    it an empty source filter excluded *every* peer and no reference could ever be latched.
    if (s.calibrating && !s.refConnected && f.reference[0] && nameContains(name, f.reference) &&
        !(f.source[0] && nameContains(name, f.source)))
        return PeerRole::Reference;

    // 3. The SB20 itself, for the button sink.
    if (s.sinkShifter && !s.sb20Connected && nameContains(name, kSb20PeerName))
        return PeerRole::Sb20Shifter;

    return PeerRole::Source;
}

// ---------------------------------------------------------------------------------------------
// Scan time: we have an advertisement. Should we connect, and as what?
// ---------------------------------------------------------------------------------------------
// NB: an explicit constructor rather than a default-member-initialiser aggregate. The device
// toolchain builds this header as gnu++11, where an NSDMI makes a struct a non-aggregate and
// `return {true, PeerRole::Trainer};` stops compiling. Same reason the callers in main.cpp assign
// PeerFilters/PeerRoleState fields one at a time instead of brace-initialising them.
struct AdvertDecision {
    bool connect;
    PeerRole role;
    AdvertDecision(bool connect_ = false, PeerRole role_ = PeerRole::Source)
        : connect(connect_), role(role_) {}
};

inline AdvertDecision classifyAdvert(const char* name, bool haveName, bool advertisesCps,
                                     const PeerFilters& f, const PeerRoleState& s) {
    // Every specific role is matched by name, so an unnamed advert can only ever be a source.
    const PeerRole role = haveName ? classifyByName(name, f, s) : PeerRole::Source;
    if (role != PeerRole::Source) return AdvertDecision(true, role);

    // Source already up — keep scanning (a reference may still be wanted) but connect nothing.
    if (s.srcConnected) return AdvertDecision(false, PeerRole::Source);

    // Otherwise this is a source candidate. With a filter set we match on name; without one we take
    // any CPS advertiser. NB no `filterUuid` upstream: the name lives in the scan RESPONSE while
    // 0x1818 lives in the ADV packet, so a UUID filter would drop the very reports a name filter
    // needs.
    const bool take = f.source[0] ? (haveName && nameContains(name, f.source)) : advertisesCps;
    return AdvertDecision(take, PeerRole::Source);
}

// ---------------------------------------------------------------------------------------------
// Connect time: a central link just came up. What is it?
// ---------------------------------------------------------------------------------------------
// The same ladder, so the two can no longer disagree. They previously did: the connect-time copy
// was missing the `!refConnected` check and the source-filter exclusion, so a link the scanner
// would never have opened as a reference was still *classified* as one, overwriting the reference
// handle and orphaning the existing link.
inline PeerRole classifyConnection(const char* peerName, const PeerFilters& f,
                                   const PeerRoleState& s) {
    return classifyByName(peerName, f, s);
}

}  // namespace bridge
