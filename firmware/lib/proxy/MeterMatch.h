#pragma once
#include <string>

// Which scanned BLE device should the proxy READ power FROM. Pure + host-tested so the selection
// precedence is regression-proof; the NimBLE scan callback just supplies the fields.
namespace sb20proxy {

// Precedence (fixes the bike-session-2 bug where the meter client bounced between the Assioma and
// the real Stages cranks — all of which advertise the CPS service 0x1818):
//   1. Never read a copy of the crank we impersonate (name == spoofName) — that would form a loop.
//   2. If a meter address is PINNED, match only that exact address (deterministic single source;
//      also how the single-right-crank use case targets a chosen crank).
//   3. A NAMELESS peripheral is matched by the CPS service flag.
//   4. BENCH ONLY (matchAnyCps, off in production): match ANY CPS-advertiser that is not our spoof
//      and not a "Stages "-named crank. This is how the WinRT fake_meter rig is read: under an ACTIVE
//      scan Windows stamps the PC's name (e.g. "CAULDT9H", NOT "ASSIOMA") into the scan response, so
//      the rig is neither nameless (path 3 misses it) nor name-filter-matching (path 5 misses it) —
//      yet it DOES advertise CPS 0x1818 in the primary advert. See decisions.md 2026-06-22.
//   5. A NAMED device must contain the meter-name filter — so a real "Stages NNNN" crank (named,
//      CPS-advertising) is NOT grabbed; only the configured meter (e.g. "ASSIOMA") is.
inline bool isTargetMeter(const std::string& name, bool advertisesCps, const std::string& addr,
                          const std::string& pinAddr, const std::string& spoofName,
                          const std::string& nameFilter, bool matchAnyCps = false) {
    if (!name.empty() && name == spoofName) return false;  // never read our own spoof (loop guard)
    if (!pinAddr.empty()) return addr == pinAddr;           // pinned: exactly one source
    if (name.empty()) return advertisesCps;                 // nameless: match by CPS UUID
    if (matchAnyCps) {
        // Exclude any "Stages " crank (the SB20's native cranks); everything else CPS is fair game.
        const bool isStagesCrank = name.rfind("Stages ", 0) == 0;
        return advertisesCps && !isStagesCrank;
    }
    return name.find(nameFilter) != std::string::npos;      // named: must be the configured meter
}

}  // namespace sb20proxy
